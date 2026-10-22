#include "PianoProNode.h"

#include "PianoLibrary.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Checks.h"
#include "gigchain/engine/SidechainCurves.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

namespace {

constexpr QByteArrayView kStateMagic = "GCP1";
constexpr qsizetype kMaxStateBytes = qsizetype{1024} * 1024;

} // namespace

PluginInfo PianoProNode::info()
{
    PluginInfo p;
    p.id = QString::fromLatin1(kPluginId);
    p.name = u"Piano Pro"_s;
    p.vendor = branding::brand();
    p.kind = PluginKind::Instrument;
    p.subCategories = u"Instrument|Piano"_s;
    p.format = PluginFormat::Builtin;
    return p;
}

std::shared_ptr<PianoProNode> PianoProNode::create(double sampleRate, int maxBlock)
{
    auto node = std::make_shared<PianoProNode>();
    (void)node->prepare(sampleRate, maxBlock); // cannot fail
    return node;
}

core::Result<void> PianoProNode::prepare(double sampleRate, int maxBlock)
{
    if (sampleRate != m_rate) m_asked.fill(false); // another rate: its own recordings
    m_rate = sampleRate;
    m_synth.prepare(sampleRate, maxBlock);
    // (Its piano is asked for once its type is known: after a restore, or at
    // the engine's next poll. Not now: a song's saved type may need another.)
    return {};
}

void PianoProNode::askForRecordings()
{
    const piano::Source source = m_synth.source();
    if (!piano::recorded(source) || m_rate <= 0.0) return;
    bool& asked = m_asked.at(static_cast<std::size_t>(source));
    if (asked) return;
    asked = true;
    m_synth.useRecordings(source, piano::PianoLibrary::instance().request(source, m_rate));
}

void PianoProNode::process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time)
{
    m_synth.process(events, io, time);
    m_holdsNotes.store(m_synth.holdsNotes(), std::memory_order_relaxed);
}

void PianoProNode::queueParameter(uint32_t id, double value, int32_t /*sampleOffset*/) noexcept
{
    // (A type changed here, from a keyboard knob, has its piano asked for at
    // the engine's next poll, serviceMainThread; the FM piano plays meanwhile.)
    m_synth.setParam(id, value);
}

double PianoProNode::parameterValue(uint32_t id) const
{
    if (id == piano::kRecordingsParameter) {
        const piano::Source source = m_synth.source();
        if (!piano::recorded(source)) return 1.0;
        switch (piano::PianoLibrary::instance().state(source, m_rate)) {
        case piano::PianoLibrary::State::Ready: return 1.0;
        case piano::PianoLibrary::State::NotAsked: // (asked for at the engine's next poll)
        case piano::PianoLibrary::State::Loading: return 0.5;
        case piano::PianoLibrary::State::Failed: return 0.0;
        }
        return 0.0;
    }
    if (id == piano::kSourceParameter) return static_cast<double>(m_synth.source()) / (piano::kSources - 1);
    return m_synth.param(id);
}

bool PianoProNode::holdsNotes() const noexcept
{
    return m_holdsNotes.load(std::memory_order_relaxed);
}

std::vector<PluginNode::Parameter> PianoProNode::parameters() const
{
    // What players see: the type, the knobs and the sidechain, then the
    // layers' lights and levels, then the master level.
    std::vector<Parameter> list;
    for (int m = 0; m < piano::kMacros; ++m) {
        const uint32_t id = piano::paramId(static_cast<piano::Macro>(m));
        list.push_back({.id = id, .name = piano::paramName(id), .choices = {}});
    }
    for (int l = 0; l < piano::kLayers; ++l) {
        for (const piano::LayerParam p : {piano::LayerParam::On, piano::LayerParam::Level}) {
            const uint32_t id = piano::paramId(static_cast<piano::Layer>(l), p);
            list.push_back({.id = id, .name = piano::paramName(id), .choices = {}});
        }
    }
    const uint32_t level = piano::paramId(piano::GlobalParam::Level);
    list.push_back({.id = level, .name = piano::paramName(level), .choices = {}});
    const auto names = [](const auto& items, auto name) {
        QStringList all;
        for (const auto& item : items) all << QString::fromLatin1(name(item));
        return all;
    };
    const auto choose = [&](piano::Macro m, QStringList choices) { list.at(static_cast<std::size_t>(m)).choices = std::move(choices); };
    choose(piano::Macro::Type, names(piano::pianoTypes(), [](const piano::PianoType& t) { return t.name; }));
    choose(piano::Macro::SidechainRate, names(sidechain::kRates, [](const sidechain::Rate& r) { return r.name; }));
    choose(piano::Macro::SidechainCurve, names(sidechain::kCurves, [](const sidechain::Curve& c) { return c.name; }));
    choose(piano::Macro::SidechainTrigger, names(sidechain::kTriggers, [](const char* t) { return t; }));
    return list;
}

std::optional<uint32_t> PianoProNode::takeTouchedParameter()
{
    GC_ONLY_MAIN_THREAD();
    return std::exchange(m_touched, std::nullopt);
}

core::Result<void> PianoProNode::setParameter(uint32_t id, double value)
{
    GC_ONLY_MAIN_THREAD();
    if (id >= static_cast<uint32_t>(piano::kParamCount) || !std::isfinite(value)) {
        return core::fail(core::ErrorCode::InvalidData, u"Piano Pro has no parameter %1 (or %2 is not a value)"_s.arg(id).arg(value));
    }
    m_synth.setParam(id, value);
    if (id == piano::paramId(piano::Macro::Type)) askForRecordings();
    m_edited.store(true, std::memory_order_relaxed);
    m_touched = id;
    return {};
}

core::Result<QByteArray> PianoProNode::saveEncodedState() const
{
    GC_ONLY_MAIN_THREAD();
    // The type by its name (types may be added or reordered), then every setting.
    const uint32_t typeId = piano::paramId(piano::Macro::Type);
    const int type = piano::typeIndex(m_synth.param(typeId));
    QJsonObject values{{u"type"_s, QString::fromLatin1(piano::pianoType(type).key)}};
    for (uint32_t id = 0; id < static_cast<uint32_t>(piano::kParamCount); ++id) {
        if (id != typeId) values.insert(piano::paramKey(id), m_synth.param(id));
    }
    const QJsonObject state{{u"version"_s, 1}, {u"params"_s, values}};
    return kStateMagic.toByteArray() + qCompress(QJsonDocument(state).toJson(QJsonDocument::Compact), 9);
}

core::Result<void> PianoProNode::restoreEncodedState(const QByteArray& bytes)
{
    GC_ONLY_MAIN_THREAD();
    const auto damaged = [](const QString& why) {
        return core::fail(core::ErrorCode::InvalidData, u"The saved Piano Pro settings are damaged (%1)"_s.arg(why));
    };
    if (!bytes.startsWith(kStateMagic)) return damaged(u"not in the expected format"_s);
    const QByteArray packed = bytes.mid(kStateMagic.size());
    if (packed.size() < 5) return damaged(u"too short"_s);
    const auto rawSize = qFromBigEndian<quint32>(packed.constData());
    if (rawSize == 0 || rawSize > kMaxStateBytes) return damaged(u"impossible size"_s);
    const QByteArray raw = qUncompress(packed);
    if (raw.size() != static_cast<qsizetype>(rawSize)) return damaged(u"could not be unpacked"_s);
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return damaged(error.errorString());
    const QJsonObject values = doc.object().value(u"params"_s).toObject();
    if (values.isEmpty()) return damaged(u"no settings in it"_s);
    const QString key = values.value(u"type"_s).toString();
    const auto types = piano::pianoTypes();
    const auto found = std::ranges::find_if(types, [&](const piano::PianoType& t) { return key == QLatin1StringView(t.key); });
    if (found == types.end()) return damaged(u"no piano type \"%1\" (a newer version's, maybe)"_s.arg(key));
    const int index = static_cast<int>(found - types.begin());
    const uint32_t typeId = piano::paramId(piano::Macro::Type);
    const auto start = piano::typeParams(index);
    m_synth.setParam(typeId, piano::typeValue(index));
    for (uint32_t id = 0; id < static_cast<uint32_t>(piano::kParamCount); ++id) {
        if (id == typeId) continue;
        const QJsonValue v = values.value(piano::paramKey(id));
        m_synth.setParam(id, v.isDouble() ? v.toDouble() : start.at(static_cast<std::size_t>(id)));
    }
    askForRecordings();
    return {};
}

QString PianoProNode::bundlePath() const
{
    return QString::fromLatin1(kPluginId);
}

QString PianoProNode::name() const
{
    return u"Piano Pro"_s;
}

} // namespace gigchain::engine
