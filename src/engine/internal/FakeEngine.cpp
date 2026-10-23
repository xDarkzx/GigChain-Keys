// The demo engine: a silent stand-in for RealEngine, used by the tests and
// when no audio device can be opened. See FakeEngineFactory.h for why.
#include "FakeEngine.h"

#include "EngineLog.h"
#include "PianoProNode.h"
#include "SawStackNode.h"

#include "gigchain/core/Branding.h"

#include "gigchain/core/Limits.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
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
    auto demo = [&vendor](const QString& id, const QString& name, PluginKind kind, const QString& subCategories) {
        return PluginInfo{.id = id,
                          .name = name,
                          .vendor = vendor,
                          .kind = kind,
                          .subCategories = subCategories,
                          .version = u"1.0"_s,
                          .classId = {},
                          .website = {},
                          .email = {},
                          .sdkVersion = {}};
    };
    return {
        demo(u"fake.grand-piano"_s, u"Grand Piano"_s, PluginKind::Instrument, u"Instrument|Piano"_s),
        demo(u"fake.electric-piano"_s, u"Electric Piano"_s, PluginKind::Instrument, u"Instrument|Piano"_s),
        demo(u"fake.string-ensemble"_s, u"String Ensemble"_s, PluginKind::Instrument, u"Instrument|Orchestra"_s),
        demo(u"fake.analog-pad"_s, u"Analog Pad"_s, PluginKind::Instrument, u"Instrument|Synth"_s),
        demo(u"fake.tonewheel-organ"_s, u"Tonewheel Organ"_s, PluginKind::Instrument, u"Instrument|Organ"_s),
        demo(u"fake.synth-lead"_s, u"Synth Lead"_s, PluginKind::Instrument, u"Instrument|Synth"_s),
        demo(u"fake.channel-eq"_s, u"Channel EQ"_s, PluginKind::Effect, u"Fx|EQ"_s),
        demo(u"fake.compressor"_s, u"Compressor"_s, PluginKind::Effect, u"Fx|Dynamics"_s),
        demo(u"fake.reverb"_s, u"Reverb"_s, PluginKind::Effect, u"Fx|Reverb"_s),
        demo(u"fake.delay"_s, u"Delay"_s, PluginKind::Effect, u"Fx|Delay"_s),
        demo(u"fake.chorus"_s, u"Chorus"_s, PluginKind::Effect, u"Fx|Modulation"_s),
        demo(u"fake.limiter"_s, u"Limiter"_s, PluginKind::Effect, u"Fx|Dynamics"_s),
    };
}

} // namespace

FakeEngine::FakeEngine(Clock clock) : m_clock(std::move(clock)) {}

void FakeEngine::applyPatch(const core::SongId&, const core::Patch& patch)
{
    m_channels.clear();
    m_channels.reserve(patch.channels.size());
    std::ranges::transform(patch.channels, std::back_inserter(m_channels), [](const core::Channel& channel) {
        return ChannelState{.id = channel.id,
                            .volumeDb = channel.volumeDb,
                            .mute = channel.mute,
                            .solo = channel.solo,
                            .builtin = channel.instrument && PluginNode::isBuiltin(channel.instrument->pluginId)
                                           ? channel.instrument->pluginId
                                           : QString()};
    });
    // Each of the app's own instruments: one per channel, kept while the
    // channel is (its settings with it), unprepared: nothing loaded or played.
    for (const ChannelState& c : m_channels) {
        if (c.builtin.isEmpty() || m_builtins.contains(c.id.value())) continue;
        if (c.builtin == QLatin1StringView(SawStackNode::kPluginId)) m_builtins[c.id.value()] = std::make_shared<SawStackNode>();
        if (c.builtin == QLatin1StringView(PianoProNode::kPluginId)) m_builtins[c.id.value()] = std::make_shared<PianoProNode>();
    }
}

PluginNode* FakeEngine::builtinOf(const core::ChannelId& id, int target) const
{
    if (target >= 0) return nullptr;
    const auto found = m_builtins.find(id.value());
    return found != m_builtins.end() ? found->second.get() : nullptr;
}

std::vector<PluginParameter> FakeEngine::pluginParameters(const core::ChannelId& id, int target) const
{
    if (const PluginNode* node = builtinOf(id, target)) {
        std::vector<PluginParameter> list;
        std::ranges::transform(node->parameters(), std::back_inserter(list), [](const PluginNode::Parameter& p) {
            return PluginParameter{.id = p.id, .name = p.name, .choices = p.choices};
        });
        return list;
    }
    return {{.id = 1, .name = u"Cutoff"_s, .choices = {}}, {.id = 2, .name = u"Resonance"_s, .choices = {}}};
}

double FakeEngine::pluginParameterValue(const core::ChannelId& id, int target, uint32_t parameter) const
{
    if (const auto set = m_parameterValues.find(parameter); set != m_parameterValues.end()) return set->second;
    if (const PluginNode* node = builtinOf(id, target)) return node->parameterValue(parameter);
    return 0.5;
}

core::Result<void> FakeEngine::setPluginParameter(const core::ChannelId& id, int target, uint32_t parameter, double value)
{
    // A built-in's setting goes to it (a type brings its own settings); what
    // it refuses is a reading (the sidechain's place, the piano's state): kept as set.
    if (PluginNode* node = builtinOf(id, target); node != nullptr && node->setParameter(parameter, value)) return {};
    m_parameterValues[parameter] = std::clamp(value, 0.0, 1.0);
    return {};
}

LevelReading FakeEngine::masterLevel()
{
    // The loudest simulated channel stands in for the mix.
    LevelReading loudest;
    for (const ChannelState& state : m_channels) {
        const LevelReading level = channelLevel(state.id);
        if (level.peak > loudest.peak) loudest = level;
    }
    return loudest;
}

std::vector<PluginInfo> FakeEngine::availablePlugins() const
{
    // The demo's made-up plugins, and the app's own instruments (as the real engine lists them).
    std::vector<PluginInfo> plugins = demoPlugins();
    std::ranges::move(PluginNode::builtins(), std::back_inserter(plugins));
    return plugins;
}

LevelReading FakeEngine::channelLevel(const core::ChannelId& id)
{
    const auto it = std::ranges::find_if(m_channels, [&id](const ChannelState& state) { return state.id == id; });
    if (it == m_channels.end()) return {};
    const bool anySolo = std::ranges::any_of(m_channels, [](const ChannelState& state) { return state.solo; });
    if (it->mute || (anySolo && !it->solo)) return {};

    const auto index = static_cast<double>(std::distance(m_channels.begin(), it));
    const double wave = 0.55 + 0.35 * std::sin(m_clock() * 4.4 + index * 1.3);
    const double master = m_masterMuted ? 0.0 : dbToGain(m_masterDb);
    const double peak = std::clamp(wave * dbToGain(it->volumeDb) * master, 0.0, 1.0);
    return LevelReading{.peak = static_cast<float>(peak), .rms = static_cast<float>(peak * 0.7)};
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

void FakeEngine::injectNote(int midiChannel, int note, int velocity)
{
    // The fake engine makes no sound; the note only shows on the keyboard.
    if (note < 0 || note > 127 || velocity < 0 || velocity > 127) return;
    const MidiTrigger pressed{.kind = MidiTrigger::Note,
                              .channel = static_cast<uint8_t>(std::clamp(midiChannel, 1, 16) - 1),
                              .number = static_cast<uint8_t>(note)};
    if (velocity > 0) m_learned = pressed; // what Learn (Pads…, Set by playing) takes, as the real engine
    // A pad assigned to the panel presses its button (never a note).
    const auto isPad = [&pressed](const MidiTrigger& t) { return t == pressed; };
    if (const auto sound = std::ranges::find_if(m_panel.sounds, isPad); sound != m_panel.sounds.end()) {
        if (velocity > 0) m_panelPresses.push_back({.kind = PanelPress::Sound, .index = static_cast<int>(sound - m_panel.sounds.begin())});
        return;
    }
    if (const auto part = std::ranges::find_if(m_panel.parts, isPad); part != m_panel.parts.end()) {
        if (velocity > 0) m_panelPresses.push_back({.kind = PanelPress::Part, .index = static_cast<int>(part - m_panel.parts.begin())});
        return;
    }
    m_keyboard.velocity.at(static_cast<std::size_t>(note)) = static_cast<uint8_t>(velocity);
    if (velocity > 0) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        m_presses.push_back(KeyPress{.note = note, .velocity = velocity,
                                     .timeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()});
    }
}

void FakeEngine::injectController(int midiChannel, int controller, int value)
{
    if (midiChannel < 1 || midiChannel > 16 || controller < 0 || controller > 127 || value < 0 || value > 127) {
        qCWarning(lcEngine) << "Ignored a controller out of range: channel" << midiChannel << "controller" << controller << "value" << value;
        return;
    }
    m_moved = std::pair{midiChannel, controller};
    const MidiTrigger knob{.kind = MidiTrigger::ControlChange, .channel = static_cast<uint8_t>(midiChannel - 1),
                           .number = static_cast<uint8_t>(controller)};
    if (value >= 64) m_learned = knob; // a button pressed (as the real engine's learnable)
    for (std::size_t i = 0; i < m_appKnobs.size(); ++i) {
        if (m_appKnobs.at(i) == knob) m_appKnobValues.at(i) = value;
    }
}

std::vector<Notice> FakeEngine::poll()
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
    return {AudioOutput{.driver = AudioDriver::System,
                        .name = m_setup.device,
                        .sampleRates = {44100, 48000},
                        .preferredSampleRate = 48000,
                        .isDefault = true}};
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
    const auto it = std::ranges::find_if(m_channels, [&id](const ChannelState& state) { return state.id == id; });
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
