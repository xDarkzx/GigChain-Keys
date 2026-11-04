#include "PadsProNode.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Checks.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

namespace {

constexpr QByteArrayView kStateMagic = "GCP1";
constexpr qsizetype kMaxStateBytes = qsizetype{1024} * 1024;

} // namespace

PluginInfo PadsProNode::info()
{
    PluginInfo p;
    p.id = QString::fromLatin1(kPluginId);
    p.name = u"Pads Pro"_s;
    p.vendor = branding::brand();
    p.kind = PluginKind::Instrument;
    p.subCategories = u"Instrument|Synth"_s;
    p.format = PluginFormat::Builtin;
    return p;
}

std::shared_ptr<PadsProNode> PadsProNode::create(double sampleRate, int maxBlock)
{
    auto node = std::make_shared<PadsProNode>();
    (void)node->prepare(sampleRate, maxBlock); // cannot fail
    return node;
}

core::Result<void> PadsProNode::prepare(double sampleRate, int maxBlock)
{
    m_synth.prepare(sampleRate, maxBlock);
    return {};
}

void PadsProNode::process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& /*time*/)
{
    m_synth.process(events, io);
    m_holdsNotes.store(m_synth.holdsNotes(), std::memory_order_relaxed);
}

void PadsProNode::queueParameter(uint32_t id, double value, int32_t /*sampleOffset*/) noexcept
{
    m_synth.setParam(id, value);
}

double PadsProNode::parameterValue(uint32_t id) const
{
    return m_synth.param(id);
}

bool PadsProNode::holdsNotes() const noexcept
{
    return m_holdsNotes.load(std::memory_order_relaxed);
}

std::vector<PluginNode::Parameter> PadsProNode::parameters() const
{
    std::vector<Parameter> list;
    list.reserve(pads::kParamCount);
    for (uint32_t id = 0; id < static_cast<uint32_t>(pads::kParamCount); ++id) list.push_back({.id = id, .name = pads::paramName(id), .choices = {}});
    QStringList names;
    for (const pads::PadType& t : pads::padTypes()) names << QString::fromLatin1(t.name);
    list.front().choices = names; // (the Type)
    return list;
}

std::optional<uint32_t> PadsProNode::takeTouchedParameter()
{
    GC_ONLY_MAIN_THREAD();
    return std::exchange(m_touched, std::nullopt);
}

core::Result<void> PadsProNode::setParameter(uint32_t id, double value)
{
    GC_ONLY_MAIN_THREAD();
    if (id >= static_cast<uint32_t>(pads::kParamCount) || !std::isfinite(value)) {
        return core::fail(core::ErrorCode::InvalidData, u"Pads Pro has no parameter %1 (or %2 is not a value)"_s.arg(id).arg(value));
    }
    m_synth.setParam(id, value);
    m_edited.store(true, std::memory_order_relaxed);
    m_touched = id;
    return {};
}

core::Result<QByteArray> PadsProNode::saveEncodedState() const
{
    GC_ONLY_MAIN_THREAD();
    const uint32_t typeId = pads::paramId(pads::Param::Type);
    QJsonObject values{{u"type"_s, QString::fromLatin1(pads::padType(pads::typeIndex(m_synth.param(typeId))).key)}};
    for (uint32_t id = 0; id < static_cast<uint32_t>(pads::kParamCount); ++id) {
        if (id != typeId) values.insert(pads::paramKey(id), m_synth.param(id));
    }
    const QJsonObject state{{u"version"_s, 1}, {u"params"_s, values}};
    return kStateMagic.toByteArray() + qCompress(QJsonDocument(state).toJson(QJsonDocument::Compact), 9);
}

core::Result<void> PadsProNode::restoreEncodedState(const QByteArray& bytes)
{
    GC_ONLY_MAIN_THREAD();
    const auto damaged = [](const QString& why) {
        return core::fail(core::ErrorCode::InvalidData, u"The saved Pads Pro settings are damaged (%1)"_s.arg(why));
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
    const auto types = pads::padTypes();
    const auto found = std::ranges::find_if(types, [&](const pads::PadType& t) { return key == QLatin1StringView(t.key); });
    if (found == types.end()) return damaged(u"no pad \"%1\" (a newer version's, maybe)"_s.arg(key));
    const int index = static_cast<int>(found - types.begin());
    // The pad first (it brings its knobs), then every setting as saved; one
    // missing (saved by an older version) stays as the pad has it.
    const uint32_t typeId = pads::paramId(pads::Param::Type);
    const auto start = pads::typeParams(index);
    m_synth.setParam(typeId, pads::typeValue(index));
    for (uint32_t id = 0; id < static_cast<uint32_t>(pads::kParamCount); ++id) {
        if (id == typeId) continue;
        const QJsonValue v = values.value(pads::paramKey(id));
        m_synth.setParam(id, v.isDouble() ? v.toDouble() : start.at(static_cast<std::size_t>(id)));
    }
    return {};
}

QString PadsProNode::bundlePath() const
{
    return QString::fromLatin1(kPluginId);
}

QString PadsProNode::name() const
{
    return u"Pads Pro"_s;
}

} // namespace gigchain::engine
