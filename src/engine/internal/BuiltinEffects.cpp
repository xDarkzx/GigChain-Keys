#include "BuiltinEffects.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Checks.h"
#include "gigchain/engine/SidechainCurves.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <span>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

namespace {

constexpr QByteArrayView kStateMagic = "GCF1";
constexpr qsizetype kMaxStateBytes = qsizetype{64} * 1024;

int choiceOf(double v, int count)
{
    return std::clamp(static_cast<int>(std::lround(v * (count - 1))), 0, count - 1);
}
double fromChoice(int i, int count)
{
    return count > 1 ? static_cast<double>(i) / (count - 1) : 0.0;
}

// A block's channel as a span (its frames).
std::span<float> channel(float* samples, int frames)
{
    return {samples, static_cast<std::size_t>(std::max(frames, 0))};
}

template <typename Items, typename Name>
std::vector<const char*> namesOf(const Items& items, Name name)
{
    std::vector<const char*> all(std::size(items));
    std::ranges::transform(items, all.begin(), name);
    return all;
}

PluginInfo effectInfo(const char* id, const QString& name, const QString& category)
{
    PluginInfo p;
    p.id = QString::fromLatin1(id);
    p.name = name;
    p.vendor = branding::brand();
    p.kind = PluginKind::Effect;
    p.subCategories = category;
    p.format = PluginFormat::Builtin;
    return p;
}

template <typename Node>
std::shared_ptr<Node> made(double sampleRate, int maxBlock)
{
    auto node = std::make_shared<Node>();
    (void)node->prepare(sampleRate, maxBlock); // cannot fail
    return node;
}

} // namespace

// ---- What the three share ------------------------------------------------

BuiltinEffectNode::BuiltinEffectNode(std::vector<Spec> specs) : m_specs(std::move(specs))
{
    Q_ASSERT(m_specs.size() <= kMaxSettings);
    for (std::size_t i = 0; i < m_specs.size(); ++i) m_values.at(i).store(m_specs.at(i).start, std::memory_order_relaxed);
}

core::Result<void> BuiltinEffectNode::prepare(double sampleRate, int /*maxBlock*/)
{
    prepareEffect(sampleRate);
    return {};
}

void BuiltinEffectNode::queueParameter(uint32_t id, double value, int32_t /*sampleOffset*/) noexcept
{
    if (id < m_specs.size() && std::isfinite(value)) m_values.at(id).store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
}

double BuiltinEffectNode::parameterValue(uint32_t id) const
{
    if (id < m_specs.size()) return setting(id);
    return id >= kReadings ? reading(id) : -1.0;
}

std::vector<PluginNode::Parameter> BuiltinEffectNode::parameters() const
{
    std::vector<Parameter> list;
    list.reserve(m_specs.size());
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        QStringList choices;
        for (const char* c : m_specs.at(i).choices) choices << QString::fromLatin1(c);
        list.push_back({.id = static_cast<uint32_t>(i), .name = QString::fromLatin1(m_specs.at(i).name), .choices = choices});
    }
    return list;
}

core::Result<void> BuiltinEffectNode::setParameter(uint32_t id, double value)
{
    GC_ONLY_MAIN_THREAD();
    if (id >= m_specs.size() || !std::isfinite(value)) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 has no setting %2 (or %3 is not a value)"_s.arg(name()).arg(id).arg(value));
    }
    m_values.at(id).store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
    m_edited.store(true, std::memory_order_relaxed);
    m_touched = id;
    return {};
}

std::optional<uint32_t> BuiltinEffectNode::takeTouchedParameter()
{
    GC_ONLY_MAIN_THREAD();
    return std::exchange(m_touched, std::nullopt);
}

core::Result<QByteArray> BuiltinEffectNode::saveEncodedState() const
{
    GC_ONLY_MAIN_THREAD();
    QJsonObject values;
    for (std::size_t i = 0; i < m_specs.size(); ++i) values.insert(QString::fromLatin1(m_specs.at(i).key), setting(i));
    const QJsonObject state{{u"version"_s, 1}, {u"params"_s, values}};
    return kStateMagic.toByteArray() + qCompress(QJsonDocument(state).toJson(QJsonDocument::Compact), 9);
}

core::Result<void> BuiltinEffectNode::restoreEncodedState(const QByteArray& bytes)
{
    GC_ONLY_MAIN_THREAD();
    const auto damaged = [this](const QString& why) {
        return core::fail(core::ErrorCode::InvalidData, u"The saved %1 settings are damaged (%2)"_s.arg(name(), why));
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
    // One missing (saved by an older version) starts where it always does.
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        const QJsonValue v = values.value(QString::fromLatin1(m_specs.at(i).key));
        m_values.at(i).store(v.isDouble() ? std::clamp(v.toDouble(), 0.0, 1.0) : m_specs.at(i).start, std::memory_order_relaxed);
    }
    return {};
}

// ---- Squash -----------------------------------------------------------------

PluginInfo OttEffectNode::info()
{
    return effectInfo(kPluginId, u"Squash"_s, u"Fx|Dynamics"_s);
}

std::shared_ptr<OttEffectNode> OttEffectNode::create(double sampleRate, int maxBlock)
{
    return made<OttEffectNode>(sampleRate, maxBlock);
}

OttEffectNode::OttEffectNode()
    : BuiltinEffectNode({{.key = "depth", .name = "Depth", .start = 0.6},
                         {.key = "time", .name = "Time", .start = 0.5},
                         {.key = "upward", .name = "Upward", .start = 1.0},
                         {.key = "downward", .name = "Downward", .start = 1.0},
                         {.key = "inGain", .name = "In Gain", .start = 0.5},
                         {.key = "outGain", .name = "Out Gain", .start = 0.5}})
{
}

void OttEffectNode::process(std::span<const MidiEvent> /*events*/, AudioBlock io, const TimeInfo& /*time*/)
{
    const auto db = [this](Setting s) { return (setting(s) - 0.5) * 2.0 * kGainRangeDb; };
    m_ott.setMix(setting(Depth));
    m_ott.setControls(setting(Time), setting(Upward), setting(Downward), db(InGain), db(OutGain));
    auto right = channel(io.right, io.frames).begin();
    for (float& left : channel(io.left, io.frames)) m_ott.tick(left, *right++);
    for (std::size_t b = 0; b < m_bandDb.size(); ++b) m_bandDb.at(b).store(m_ott.bandDb(b), std::memory_order_relaxed);
}

double OttEffectNode::reading(uint32_t id) const
{
    const uint32_t band = id - kReadings;
    if (band >= m_bandDb.size()) return -1.0;
    return std::clamp(0.5 + (m_bandDb.at(band).load(std::memory_order_relaxed) / 48.0), 0.0, 1.0);
}

// ---- Enhance --------------------------------------------------------------

PluginInfo EnhanceEffectNode::info()
{
    return effectInfo(kPluginId, u"Enhance"_s, u"Fx|Mastering"_s);
}

std::shared_ptr<EnhanceEffectNode> EnhanceEffectNode::create(double sampleRate, int maxBlock)
{
    return made<EnhanceEffectNode>(sampleRate, maxBlock);
}

EnhanceEffectNode::EnhanceEffectNode()
    : BuiltinEffectNode({{.key = "amount", .name = "Amount", .start = 0.75},
                         {.key = "mode", .name = "Mode", .start = 0.0,
                          .choices = namesOf(sawstack::Enhance::kModeNames, [](const char* m) { return m; })}})
{
}

void EnhanceEffectNode::process(std::span<const MidiEvent> /*events*/, AudioBlock io, const TimeInfo& /*time*/)
{
    m_enhance.set(setting(Amount), choiceOf(setting(Mode), sawstack::Enhance::kModes));
    auto right = channel(io.right, io.frames).begin();
    for (float& left : channel(io.left, io.frames)) m_enhance.tick(left, *right++);
}

// ---- Sidechain ------------------------------------------------------------

PluginInfo SidechainEffectNode::info()
{
    return effectInfo(kPluginId, u"Sidechain"_s, u"Fx|Dynamics"_s);
}

std::shared_ptr<SidechainEffectNode> SidechainEffectNode::create(double sampleRate, int maxBlock)
{
    return made<SidechainEffectNode>(sampleRate, maxBlock);
}

SidechainEffectNode::SidechainEffectNode()
    : BuiltinEffectNode({{.key = "depth", .name = "Depth", .start = 0.8},
                         {.key = "rate", .name = "Rate", .start = fromChoice(sidechain::kDefaultRate, static_cast<int>(sidechain::kRates.size())),
                          .choices = namesOf(sidechain::kRates, [](const sidechain::Rate& r) { return r.name; })},
                         {.key = "curve", .name = "Curve", .start = 0.0,
                          .choices = namesOf(sidechain::kCurves, [](const sidechain::Curve& c) { return c.name; })},
                         {.key = "slope", .name = "Slope", .start = 0.5},
                         {.key = "trigger", .name = "Trigger", .start = 0.0,
                          .choices = namesOf(sidechain::kTriggers, [](const char* t) { return t; })}})
{
}

void SidechainEffectNode::process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time)
{
    constexpr auto kRates = static_cast<int>(sidechain::kRates.size());
    constexpr auto kCurves = static_cast<int>(sidechain::kCurves.size());
    constexpr auto kTriggers = static_cast<int>(sidechain::kTriggers.size());
    m_sidechain.set(setting(Depth), choiceOf(setting(Curve), kCurves),
                    sidechain::kRates.at(static_cast<std::size_t>(choiceOf(setting(Rate), kRates))).beats, setting(Slope),
                    choiceOf(setting(Trigger), kTriggers) == 1);
    const double step = time.sampleRate > 0.0 ? time.tempo / 60.0 / time.sampleRate : 0.0;
    double ppq = time.ppqPosition;
    auto next = events.begin();
    auto right = channel(io.right, io.frames).begin();
    int i = 0;
    for (float& left : channel(io.left, io.frames)) {
        // A note starting here (following the notes): the curve starts again.
        for (; next != events.end() && next->sampleOffset <= i; ++next) {
            if ((next->status & 0xF0U) == 0x90U && next->data2 > 0 && !next->controlsOnly) m_sidechain.noteAt(ppq);
        }
        const float g = m_sidechain.gain(ppq);
        left *= g;
        *right++ *= g;
        ppq += step;
        ++i;
    }
    m_position.store(static_cast<float>(m_sidechain.position()), std::memory_order_relaxed);
}

double SidechainEffectNode::reading(uint32_t id) const
{
    return id == kReadings ? m_position.load(std::memory_order_relaxed) : -1.0;
}

} // namespace gigchain::engine
