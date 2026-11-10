#include "SoundFontNode.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Checks.h"
#include "gigchain/engine/BuiltinReadings.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <cmath>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

namespace {

constexpr QByteArrayView kStateMagic = "GCF1";
constexpr qsizetype kMaxStateBytes = qsizetype{1024} * 1024;

} // namespace

PluginInfo SoundFontNode::info()
{
    PluginInfo p;
    p.id = QString::fromLatin1(kPluginId);
    p.name = u"SoundFont Player"_s;
    p.vendor = branding::brand();
    p.kind = PluginKind::Instrument;
    p.subCategories = u"Instrument|Sampler"_s;
    p.format = PluginFormat::Builtin;
    return p;
}

std::shared_ptr<SoundFontNode> SoundFontNode::create(double sampleRate, int maxBlock)
{
    auto node = std::make_shared<SoundFontNode>();
    (void)node->prepare(sampleRate, maxBlock); // cannot fail
    return node;
}

core::Result<void> SoundFontNode::prepare(double sampleRate, int /*maxBlock*/)
{
    m_player.prepare(sampleRate);
    return {};
}

void SoundFontNode::process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& /*time*/)
{
    m_player.process(events, io);
}

void SoundFontNode::queueParameter(uint32_t id, double value, int32_t /*sampleOffset*/) noexcept
{
    m_player.setParam(id, value);
}

double SoundFontNode::parameterValue(uint32_t id) const
{
    if (id == readings::kSoundFontFile) {
        switch (m_player.fileState()) {
        case sf2::Player::FileState::None: return 0.0;
        case sf2::Player::FileState::Failed: return 0.25;
        case sf2::Player::FileState::Loading: return 0.5;
        case sf2::Player::FileState::Ready: return 1.0;
        }
        return 0.0;
    }
    if (id == readings::kSoundFontPresets) return m_player.generation();
    return m_player.param(id);
}

bool SoundFontNode::holdsNotes() const noexcept
{
    return m_player.holdsNotes();
}

std::vector<PluginNode::Parameter> SoundFontNode::parameters() const
{
    std::vector<Parameter> list;
    list.reserve(sf2::kParamCount);
    for (uint32_t id = 0; id < static_cast<uint32_t>(sf2::kParamCount); ++id) {
        list.push_back({.id = id, .name = sf2::paramName(id), .choices = sf2::paramChoices(id)});
    }
    // The presets, as the window lists them ("000:000 Grand Piano").
    QStringList presets;
    for (const sf2::PresetInfo& p : m_player.presets()) {
        presets << u"%1:%2 %3"_s.arg(p.bank, 3, 10, u'0').arg(p.program, 3, 10, u'0').arg(p.name);
    }
    list.front().choices = presets;
    return list;
}

std::optional<uint32_t> SoundFontNode::takeTouchedParameter()
{
    GC_ONLY_MAIN_THREAD();
    return std::exchange(m_touched, std::nullopt);
}

core::Result<void> SoundFontNode::setParameter(uint32_t id, double value)
{
    GC_ONLY_MAIN_THREAD();
    if (id >= static_cast<uint32_t>(sf2::kParamCount) || !std::isfinite(value)) {
        return core::fail(core::ErrorCode::InvalidData, u"The SoundFont Player has no parameter %1 (or %2 is not a value)"_s.arg(id).arg(value));
    }
    m_player.setParam(id, value);
    m_edited.store(true, std::memory_order_relaxed);
    m_touched = id;
    return {};
}

core::Result<void> SoundFontNode::setFile(const QString& path)
{
    GC_ONLY_MAIN_THREAD();
    if (!path.isEmpty() && !QFileInfo(path).isFile()) {
        return core::fail(core::ErrorCode::FileNotFound, u"There is no SoundFont at %1"_s.arg(path));
    }
    m_player.setFile(path); // its first preset (the window chooses another)
    m_edited.store(true, std::memory_order_relaxed);
    return {};
}

core::Result<QByteArray> SoundFontNode::saveEncodedState() const
{
    GC_ONLY_MAIN_THREAD();
    QJsonObject values;
    for (uint32_t id = 0; id < static_cast<uint32_t>(sf2::kParamCount); ++id) {
        if (id != sf2::paramId(sf2::Param::Preset)) values.insert(sf2::paramKey(id), m_player.param(id));
    }
    const auto [bank, program] = m_player.chosen();
    const QJsonObject state{{u"version"_s, 1},
                            {u"file"_s, m_player.file()},
                            {u"bank"_s, bank},
                            {u"program"_s, program},
                            {u"params"_s, values}};
    return kStateMagic.toByteArray() + qCompress(QJsonDocument(state).toJson(QJsonDocument::Compact), 9);
}

core::Result<void> SoundFontNode::restoreEncodedState(const QByteArray& bytes)
{
    GC_ONLY_MAIN_THREAD();
    const auto damaged = [](const QString& why) {
        return core::fail(core::ErrorCode::InvalidData, u"The saved SoundFont Player settings are damaged (%1)"_s.arg(why));
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
    const QJsonObject state = doc.object();
    // Every control as saved; one missing (an older version's) at its start.
    const QJsonObject values = state.value(u"params"_s).toObject();
    for (uint32_t id = 0; id < static_cast<uint32_t>(sf2::kParamCount); ++id) {
        if (id == sf2::paramId(sf2::Param::Preset)) continue;
        const QJsonValue v = values.value(sf2::paramKey(id));
        m_player.setParam(id, v.isDouble() ? v.toDouble() : sf2::paramDefault(id));
    }
    // The file: read in the background, its preset chosen once it is in. A
    // file that is not there is said (takeMessages) and the settings kept.
    m_player.setFile(state.value(u"file"_s).toString(), state.value(u"bank"_s).toInt(-1), state.value(u"program"_s).toInt(-1));
    return {};
}

QString SoundFontNode::bundlePath() const
{
    return QString::fromLatin1(kPluginId);
}

QString SoundFontNode::name() const
{
    return u"SoundFont Player"_s;
}

} // namespace gigchain::engine
