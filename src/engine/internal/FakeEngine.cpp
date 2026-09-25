#include "FakeEngine.h"

#include "gigchain/core/Branding.h"

#include "gigchain/core/Limits.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

double dbToGain(double db)
{
    return db <= core::limits::kMinVolumeDb ? 0.0 : std::pow(10.0, db / 20.0);
}

std::optional<double> sanitizeVolume(double db)
{
    if (!std::isfinite(db)) return std::nullopt;
    return std::clamp(db, core::limits::kMinVolumeDb, core::limits::kMaxVolumeDb);
}

std::vector<PluginInfo> demoPlugins()
{
    const QString vendor = branding::brand() + u" Demo"_s;
    return {
        {u"fake.grand-piano"_s, u"Grand Piano"_s, vendor, PluginKind::Instrument, u"Instrument|Piano"_s, u"1.0"_s},
        {u"fake.electric-piano"_s, u"Electric Piano"_s, vendor, PluginKind::Instrument, u"Instrument|Piano"_s, u"1.0"_s},
        {u"fake.string-ensemble"_s, u"String Ensemble"_s, vendor, PluginKind::Instrument, u"Instrument|Orchestra"_s, u"1.0"_s},
        {u"fake.analog-pad"_s, u"Analog Pad"_s, vendor, PluginKind::Instrument, u"Instrument|Synth"_s, u"1.0"_s},
        {u"fake.tonewheel-organ"_s, u"Tonewheel Organ"_s, vendor, PluginKind::Instrument, u"Instrument|Organ"_s, u"1.0"_s},
        {u"fake.synth-lead"_s, u"Synth Lead"_s, vendor, PluginKind::Instrument, u"Instrument|Synth"_s, u"1.0"_s},
        {u"fake.channel-eq"_s, u"Channel EQ"_s, vendor, PluginKind::Effect, u"Fx|EQ"_s, u"1.0"_s},
        {u"fake.compressor"_s, u"Compressor"_s, vendor, PluginKind::Effect, u"Fx|Dynamics"_s, u"1.0"_s},
        {u"fake.reverb"_s, u"Reverb"_s, vendor, PluginKind::Effect, u"Fx|Reverb"_s, u"1.0"_s},
        {u"fake.delay"_s, u"Delay"_s, vendor, PluginKind::Effect, u"Fx|Delay"_s, u"1.0"_s},
        {u"fake.chorus"_s, u"Chorus"_s, vendor, PluginKind::Effect, u"Fx|Modulation"_s, u"1.0"_s},
        {u"fake.limiter"_s, u"Limiter"_s, vendor, PluginKind::Effect, u"Fx|Dynamics"_s, u"1.0"_s},
    };
}

} // namespace

FakeEngine::FakeEngine(Clock clock) : m_clock(std::move(clock)) {}

void FakeEngine::applyPatch(const core::SongId&, const core::Patch& patch)
{
    m_channels.clear();
    m_channels.reserve(patch.channels.size());
    for (const core::Channel& channel : patch.channels) {
        m_channels.push_back(ChannelState{channel.id, channel.volumeDb, channel.mute, channel.solo});
    }
}

std::vector<PluginInfo> FakeEngine::availablePlugins() const
{
    return demoPlugins();
}

LevelReading FakeEngine::channelLevel(const core::ChannelId& id)
{
    const auto it = std::find_if(m_channels.begin(), m_channels.end(),
                                 [&id](const ChannelState& state) { return state.id == id; });
    if (it == m_channels.end()) return {};
    const bool anySolo = std::any_of(m_channels.begin(), m_channels.end(),
                                     [](const ChannelState& state) { return state.solo; });
    if (it->mute || (anySolo && !it->solo)) return {};

    const auto index = static_cast<double>(std::distance(m_channels.begin(), it));
    const double wave = 0.55 + 0.35 * std::sin(m_clock() * 4.4 + index * 1.3);
    const double peak = std::clamp(wave * dbToGain(it->volumeDb) * dbToGain(m_masterDb), 0.0, 1.0);
    return LevelReading{static_cast<float>(peak), static_cast<float>(peak * 0.7)};
}

float FakeEngine::cpuLoad() const
{
    const double load = 0.12 + 0.04 * std::sin(m_clock() * 0.5) + 0.02 * static_cast<double>(m_channels.size());
    return static_cast<float>(std::clamp(load, 0.0, 1.0));
}

bool FakeEngine::midiActivity() const
{
    return std::fmod(m_clock(), 2.0) < 0.15;
}

void FakeEngine::setChannelVolume(const core::ChannelId& id, double volumeDb)
{
    const auto volume = sanitizeVolume(volumeDb);
    ChannelState* state = find(id);
    if (volume && state != nullptr) state->volumeDb = *volume;
}

void FakeEngine::setChannelPan(const core::ChannelId&, double)
{
    // The fake engine makes no sound; pan has nothing to move.
}

void FakeEngine::setChannelMute(const core::ChannelId& id, bool mute)
{
    if (ChannelState* state = find(id)) state->mute = mute;
}

void FakeEngine::setChannelSolo(const core::ChannelId& id, bool solo)
{
    if (ChannelState* state = find(id)) state->solo = solo;
}

void FakeEngine::setMasterVolume(double volumeDb)
{
    if (const auto volume = sanitizeVolume(volumeDb)) m_masterDb = *volume;
}

double FakeEngine::masterVolume() const
{
    return m_masterDb;
}

void FakeEngine::injectNote(int, int, int)
{
    // The fake engine makes no sound; notes are accepted and dropped by design.
}

std::vector<QString> FakeEngine::poll()
{
    return {};
}

core::Result<std::unique_ptr<IPluginEditor>> FakeEngine::createEditor(const core::ChannelId&)
{
    return std::unique_ptr<IPluginEditor>(); // demo plugins have no editors
}

core::Result<std::unique_ptr<IPluginEditor>> FakeEngine::createEditorForPlugin(const QString&)
{
    return std::unique_ptr<IPluginEditor>(); // demo plugins have no editors
}

std::vector<AudioOutput> FakeEngine::audioOutputs() const
{
    return {AudioOutput{AudioDriver::System, m_setup.device, {44100, 48000}, 48000, true}};
}

core::Result<void> FakeEngine::setAudioSetup(const AudioSetup& setup)
{
    if (!setup.device.isEmpty() && setup.device != m_setup.device) {
        return core::fail(core::ErrorCode::InvalidData, u"No audio output named \"%1\" (demo engine)"_s.arg(setup.device));
    }
    m_setup.sampleRate = setup.sampleRate != 0 ? setup.sampleRate : 48000;
    m_setup.bufferFrames = setup.bufferFrames;
    return {};
}

QString FakeEngine::statusText() const
{
    return u"Demo engine (no audio)"_s;
}

FakeEngine::ChannelState* FakeEngine::find(const core::ChannelId& id)
{
    const auto it = std::find_if(m_channels.begin(), m_channels.end(),
                                 [&id](const ChannelState& state) { return state.id == id; });
    return it == m_channels.end() ? nullptr : &*it;
}

std::unique_ptr<IEngine> createFakeEngine(Clock clock)
{
    return std::make_unique<FakeEngine>(std::move(clock));
}

std::unique_ptr<IEngine> createFakeEngine()
{
    const auto start = std::chrono::steady_clock::now();
    return createFakeEngine([start] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    });
}

} // namespace gigchain::engine
