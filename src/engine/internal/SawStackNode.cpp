#include "SawStackNode.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Checks.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

namespace {

constexpr QByteArrayView kStateMagic = "GCB1";
constexpr qsizetype kMaxStateBytes = qsizetype{1024} * 1024;

} // namespace

PluginInfo SawStackNode::info()
{
    PluginInfo p;
    p.id = QString::fromLatin1(kPluginId);
    p.name = u"Saw Stack Pro"_s;
    p.vendor = branding::brand();
    p.kind = PluginKind::Instrument;
    p.subCategories = u"Instrument|Synth"_s;
    p.format = PluginFormat::Builtin;
    return p;
}

std::shared_ptr<SawStackNode> SawStackNode::create(double sampleRate, int maxBlock)
{
    auto node = std::make_shared<SawStackNode>();
    (void)node->prepare(sampleRate, maxBlock); // cannot fail
    return node;
}

core::Result<void> SawStackNode::prepare(double sampleRate, int maxBlock)
{
    m_synth.prepare(sampleRate, maxBlock);
    return {};
}

void SawStackNode::process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& /*time*/)
{
    m_synth.process(events, io);
    m_holdsNotes.store(m_synth.holdsNotes(), std::memory_order_relaxed);
}

void SawStackNode::queueParameter(uint32_t id, double value, int32_t /*sampleOffset*/) noexcept
{
    m_synth.setParam(id, value);
}

double SawStackNode::parameterValue(uint32_t id) const
{
    return m_synth.param(id);
}

bool SawStackNode::holdsNotes() const noexcept
{
    return m_holdsNotes.load(std::memory_order_relaxed);
}

std::vector<PluginNode::Parameter> SawStackNode::parameters() const
{
    // Only what players see: the type and the five knobs (the layers are the type's).
    std::vector<Parameter> list;
    list.reserve(sawstack::kMacros);
    for (int m = 0; m < sawstack::kMacros; ++m) {
        const uint32_t id = sawstack::paramId(static_cast<sawstack::Macro>(m));
        list.push_back({.id = id, .name = sawstack::paramName(id), .choices = {}});
    }
    QStringList types;
    for (const sawstack::StackType& t : sawstack::stackTypes()) types << QString::fromLatin1(t.name);
    list.front().choices = types; // the type
    return list;
}

std::optional<uint32_t> SawStackNode::takeTouchedParameter()
{
    GC_ONLY_MAIN_THREAD();
    return std::exchange(m_touched, std::nullopt);
}

core::Result<void> SawStackNode::setParameter(uint32_t id, double value)
{
    GC_ONLY_MAIN_THREAD();
    if (id >= static_cast<uint32_t>(sawstack::kParamCount) || !std::isfinite(value)) {
        return core::fail(core::ErrorCode::InvalidData, u"Saw Stack Pro has no parameter %1 (or %2 is not a value)"_s.arg(id).arg(value));
    }
    m_synth.setParam(id, value);
    m_edited.store(true, std::memory_order_relaxed);
    m_touched = id;
    return {};
}

core::Result<QByteArray> SawStackNode::saveEncodedState() const
{
    GC_ONLY_MAIN_THREAD();
    // The type by its name (types may be added or reordered), and the knobs.
    const int type = sawstack::typeIndex(m_synth.param(sawstack::paramId(sawstack::Macro::Type)));
    QJsonObject values{{u"type"_s, QString::fromLatin1(sawstack::stackType(type).key)}};
    for (int m = 1; m < sawstack::kMacros; ++m) {
        const uint32_t id = sawstack::paramId(static_cast<sawstack::Macro>(m));
        values.insert(sawstack::paramKey(id), m_synth.param(id));
    }
    const QJsonObject state{{u"version"_s, 1}, {u"params"_s, values}};
    return kStateMagic.toByteArray() + qCompress(QJsonDocument(state).toJson(QJsonDocument::Compact), 9);
}

core::Result<void> SawStackNode::restoreEncodedState(const QByteArray& bytes)
{
    GC_ONLY_MAIN_THREAD();
    const auto damaged = [](const QString& why) {
        return core::fail(core::ErrorCode::InvalidData, u"The saved Saw Stack Pro settings are damaged (%1)"_s.arg(why));
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
    const auto types = sawstack::stackTypes();
    const auto found = std::ranges::find_if(types, [&](const sawstack::StackType& t) { return key == QLatin1StringView(t.key); });
    if (found == types.end()) return damaged(u"no stack type \"%1\" (a newer version's, maybe)"_s.arg(key));
    const int index = static_cast<int>(found - types.begin());
    // The type first (its knobs where it starts them), then the knobs as
    // saved; one missing (saved by an older version) stays where the type starts it.
    const auto start = sawstack::typeParams(index);
    m_synth.setParam(sawstack::paramId(sawstack::Macro::Type), sawstack::typeValue(index));
    for (int m = 1; m < sawstack::kMacros; ++m) {
        const uint32_t id = sawstack::paramId(static_cast<sawstack::Macro>(m));
        const QJsonValue v = values.value(sawstack::paramKey(id));
        m_synth.setParam(id, v.isDouble() ? v.toDouble() : start.at(static_cast<std::size_t>(id)));
    }
    return {};
}

QString SawStackNode::bundlePath() const
{
    return QString::fromLatin1(kPluginId);
}

QString SawStackNode::name() const
{
    return u"Saw Stack Pro"_s;
}

} // namespace gigchain::engine
