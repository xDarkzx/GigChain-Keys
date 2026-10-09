#pragma once

#include "gigchain/engine/IEngine.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace gigchain::test {

// An IEngine that records what the UI asked of it.
class SpyEngine final : public engine::IEngine
{
public:
    int applyCount = 0;
    core::Patch lastPatch;
    std::map<QString, double> volumes;
    std::map<QString, double> pans;
    std::map<QString, bool> mutes;
    std::map<QString, bool> solos;
    double master = 0.0;
    std::vector<std::array<int, 3>> notes;
    std::vector<engine::Notice> pendingNotices;

    using engine::IEngine::applyPatch;
    int preloadCount = 0;
    void preload(const core::Setlist&) override { ++preloadCount; }
    int relinkCount = 0;
    void relinkInstances(const core::Setlist&) override { ++relinkCount; }
    void setProgressHandler(engine::LoadProgress) override {}
    [[nodiscard]] std::size_t loadedPluginCount() const override { return 0; }
    QStringList blocked;
    [[nodiscard]] QStringList blockedPlugins() const override { return blocked; }
    void unblockPlugin(const QString& pluginId) override { blocked.removeAll(pluginId); }
    engine::ControlTriggers triggers{};
    void setControlTriggers(const engine::ControlTriggers& chosen) override { triggers = chosen; }
    std::vector<engine::ControlAction> pendingActions;
    std::vector<engine::ControlAction> takeControlActions() override { return std::exchange(pendingActions, {}); }
    int pendingProgram = -1;
    int takeProgramChange() override { return std::exchange(pendingProgram, -1); }
    engine::MidiTrigger learned;
    engine::MidiTrigger takeLearnedTrigger() override { return std::exchange(learned, {}); }
    uint32_t transportRequests = 0; // what the next poll takes (transport::k*)
    uint32_t takeTransportRequests() override { return std::exchange(transportRequests, 0U); }
    int panics = 0;
    void panic() override { ++panics; }
    int storeCount = 0;
    std::vector<QString> storeProblems;
    std::vector<QString> storePluginStates(core::Setlist& setlist) override
    {
        ++storeCount;
        for (auto& song : setlist.songs) {
            for (auto& patch : song.patches) {
                for (auto& channel : patch.channels) {
                    if (channel.instrument) channel.instrument->state = "spy settings: " + channel.instrument->pluginId.toUtf8();
                }
            }
        }
        return storeProblems;
    }
    bool pluginEdited = false;
    bool takePluginEdits() override { return std::exchange(pluginEdited, false); }
    void applyPatch(const core::SongId&, const core::Patch& patch) override
    {
        ++applyCount;
        calls.push_back(QStringLiteral("patch"));
        lastPatch = patch;
    }
    [[nodiscard]] QString pluginFolder() const override { return {}; } // its plugins are not on disk
    [[nodiscard]] std::vector<engine::PluginInfo> availablePlugins() const override
    {
        return {
            {.id = QStringLiteral("spy/Piano.vst3"), .name = QStringLiteral("Spy Piano"), .vendor = QStringLiteral("Spy"),
             .kind = engine::PluginKind::Instrument, .subCategories = QStringLiteral("Instrument|Piano"),
             .version = QStringLiteral("1.0"), .classId = {}, .website = QStringLiteral("https://spy.example"),
             .email = QStringLiteral("help@spy.example"), .sdkVersion = QStringLiteral("VST 3.8.0")},
            // Its "website" would start a program: the app must never open it.
            {.id = QStringLiteral("spy/Pad.vst3"), .name = QStringLiteral("Spy Pad"), .vendor = QStringLiteral("Spy"),
             .kind = engine::PluginKind::Instrument, .subCategories = QStringLiteral("Instrument|Synth"),
             .version = QStringLiteral("1.0"), .classId = {}, .website = QStringLiteral("file:///C:/Windows/System32/calc.exe"),
             .email = {}, .sdkVersion = {}},
            {.id = QStringLiteral("spy/Reverb.vst3"), .name = QStringLiteral("Spy Reverb"), .vendor = QStringLiteral("Other"),
             .kind = engine::PluginKind::Effect, .subCategories = QStringLiteral("Fx|Reverb"),
             .version = QStringLiteral("1.0"), .classId = {}, .website = {}, .email = {}, .sdkVersion = {}},
        };
    }
    [[nodiscard]] engine::LevelReading channelLevel(const core::ChannelId&) override { return {0.5F, 0.25F}; }
    [[nodiscard]] engine::LevelReading masterLevel() override { return {0.4F, 0.2F}; }
    [[nodiscard]] float cpuLoad() const override { return 0.25F; }
    [[nodiscard]] bool midiActivity() const override { return true; }
    engine::MidiActivity keyboard;
    [[nodiscard]] engine::MidiActivity keyboardActivity() const override { return keyboard; }
    void setChannelVolume(const core::ChannelId& id, double db) override { volumes[id.value()] = db; }
    void setChannelPan(const core::ChannelId& id, double pan) override { pans[id.value()] = pan; }
    void setChannelMute(const core::ChannelId& id, bool mute) override { mutes[id.value()] = mute; }
    void setChannelSolo(const core::ChannelId& id, bool solo) override { solos[id.value()] = solo; }
    void setMasterVolume(double db) override { master = db; }
    [[nodiscard]] double masterVolume() const override { return master; }
    std::vector<core::PluginSlot> masterEffects;
    int masterEffectChanges = 0;
    void setMasterEffects(const std::vector<core::PluginSlot>& effects) override
    {
        ++masterEffectChanges;
        masterEffects = effects;
    }
    std::vector<QString> storeMasterEffectStates(std::vector<core::PluginSlot>& effects) override
    {
        for (auto& slot : effects) slot.state = "spy master settings: " + slot.pluginId.toUtf8();
        return {};
    }
    bool masterEdited = false;
    bool takeMasterEdits() override { return std::exchange(masterEdited, false); }
    std::vector<int> masterEditorRequests;
    core::Result<std::unique_ptr<engine::IPluginEditor>> createMasterEffectEditor(int effect) override
    {
        masterEditorRequests.push_back(effect);
        return core::fail(core::ErrorCode::InvalidData, QStringLiteral("Spy effects have no window"));
    }
    std::vector<core::PluginSlot> auxEffects;
    void setAuxEffects(const std::vector<core::PluginSlot>& effects) override { auxEffects = effects; }
    std::vector<QString> storeAuxEffectStates(std::vector<core::PluginSlot>& effects) override
    {
        for (auto& slot : effects) slot.state = "spy aux settings: " + slot.pluginId.toUtf8();
        return {};
    }
    bool takeAuxEdits() override { return false; }
    core::Result<std::unique_ptr<engine::IPluginEditor>> createAuxEffectEditor(int) override
    {
        return core::fail(core::ErrorCode::InvalidData, QStringLiteral("Spy effects have no window"));
    }
    std::map<QString, double> sends;
    void setChannelSend(const core::ChannelId& id, double sendDb) override { sends[id.value()] = sendDb; }
    bool limiterOn = true;
    double limiterCeiling = -1.0;
    void setOutputLimiter(bool enabled, double ceilingDb) override
    {
        limiterOn = enabled;
        limiterCeiling = ceilingDb;
    }
    bool limiterActivity = false;
    bool takeLimiterActivity() override { return std::exchange(limiterActivity, false); }
    bool muted = false;
    void setMasterMute(bool mute) override { muted = mute; }
    [[nodiscard]] bool masterMuted() const override { return muted; }
    void injectNote(int channel, int note, int velocity) override { notes.push_back({channel, note, velocity}); }
    void injectController(int, int, int) override {}
    std::vector<engine::KeyPress> keyPresses; // what takeKeyPresses() hands over next
    std::vector<engine::KeyPress> takeKeyPresses() override { return std::exchange(keyPresses, {}); }
    engine::AppKnobs appKnobs{};
    engine::AppKnobValues appKnobValues = [] {
        engine::AppKnobValues none{};
        none.fill(-1);
        return none;
    }();
    void setAppKnobs(const engine::AppKnobs& knobs) override { appKnobs = knobs; }
    engine::AppKnobValues takeAppKnobValues() override
    {
        engine::AppKnobValues taken = appKnobValues;
        appKnobValues.fill(-1);
        return taken;
    }
    std::vector<engine::Notice> poll() override
    {
        std::vector<engine::Notice> out;
        out.swap(pendingNotices);
        return out;
    }
    [[nodiscard]] QString statusText() const override { return QStringLiteral("Spy engine"); }
    core::Result<std::unique_ptr<engine::IPluginEditor>> createEditor(const core::ChannelId& id) override
    {
        editorRequests.push_back(id.value());
        return std::unique_ptr<engine::IPluginEditor>();
    }
    std::vector<QString> editorRequests;
    std::vector<std::pair<QString, int>> effectEditorRequests;
    core::Result<std::unique_ptr<engine::IPluginEditor>> createEffectEditor(const core::ChannelId& id, int effect) override
    {
        effectEditorRequests.emplace_back(id.value(), effect);
        return core::fail(core::ErrorCode::InvalidData, QStringLiteral("Spy effects have no window"));
    }
    core::Result<std::unique_ptr<engine::IPluginEditor>> createEditorForPlugin(const QString& pluginId) override
    {
        pluginEditorRequests.push_back(pluginId);
        return std::unique_ptr<engine::IPluginEditor>();
    }
    std::vector<QString> pluginEditorRequests;

    engine::AudioSetup setup{.driver = engine::AudioDriver::System, .device = QStringLiteral("Spy Speakers"),
                             .sampleRate = 48000, .bufferFrames = 256, .inputDevice = {}};
    QStringList midiPresent{QStringLiteral("Spy Keys 0"), QStringLiteral("MIDIIN2 (Spy Keys) 1")};
    engine::MidiSetup midi;
    int midiChanges = 0;
    int setupChanges = 0;
    QString failingDevice; // setAudioSetup fails for this device
    [[nodiscard]] std::vector<engine::AudioOutput> audioOutputs() const override
    {
        return {
            {engine::AudioDriver::System, QStringLiteral("Spy Speakers"), {44100, 48000, 96000}, 48000, true},
            {engine::AudioDriver::System, QStringLiteral("Spy Headphones"), {48000}, 48000, false},
            {engine::AudioDriver::Asio, QStringLiteral("Spy ASIO"), {44100, 48000}, 44100, false},
        };
    }
    [[nodiscard]] engine::AudioSetup audioSetup() const override { return setup; }
    core::Result<void> setAudioSetup(const engine::AudioSetup& wanted) override
    {
        if (!failingDevice.isEmpty() && wanted.device == failingDevice) {
            return core::fail(core::ErrorCode::DeviceUnavailable, failingDevice + QStringLiteral(" cannot open"));
        }
        ++setupChanges;
        setup = wanted;
        if (setup.device.isEmpty()) setup.device = QStringLiteral("Spy Speakers");
        if (setup.sampleRate == 0) setup.sampleRate = 48000;
        return {};
    }
    [[nodiscard]] std::vector<engine::MidiPort> midiInputs() const override
    {
        return engine::resolveMidiInputs(midiPresent, midi);
    }
    [[nodiscard]] engine::MidiSetup midiSetup() const override { return midi; }
    core::Result<void> setMidiSetup(const engine::MidiSetup& chosen) override
    {
        ++midiChanges;
        midi = chosen;
        return {};
    }

    double tempoNow = 120.0;
    std::vector<double> tempoRequests;
    void setTempo(double bpm) override
    {
        tempoRequests.push_back(bpm);
        if (bpm >= 20.0 && bpm <= 400.0) tempoNow = bpm;
    }
    [[nodiscard]] double tempo() const override { return tempoNow; }
    bool click = false;
    double clickVolume = 0.0;
    void setClick(bool on, double volumeDb) override
    {
        click = on;
        clickVolume = volumeDb;
    }
    [[nodiscard]] bool clickOn() const override { return click; }
    int clickPair = 0;
    void setClickOutput(int pair) override { clickPair = pair; }
    QString recordingTo; // empty: not recording
    core::Result<void> startRecording(const QString& path) override
    {
        recordingTo = path;
        return {};
    }
    core::Result<double> stopRecording() override
    {
        recordingTo.clear();
        return 12.5;
    }
    [[nodiscard]] bool recording() const override { return !recordingTo.isEmpty(); }
    int outputs = 8; // an 8-output interface
    [[nodiscard]] int outputChannels() const override { return outputs; }
    engine::BackingTrackState track;
    std::vector<QString> trackRequests;
    void setBackingTrack(const QString& path) override
    {
        trackRequests.push_back(path);
        if (path == track.path) return;
        track = engine::BackingTrackState{.path = path, .loading = false, .loaded = !path.isEmpty(), .playing = false,
                                          .position = 0.0, .length = path.isEmpty() ? 0.0 : 60.0};
    }
    std::vector<engine::BackingStemFile> stems;
    void setBackingStems(const std::vector<engine::BackingStemFile>& files) override { stems = files; }
    void playBackingTrack(bool play) override { track.playing = play && track.loaded; }
    void rewindBackingTrack() override { track.position = 0.0; }
    void seekBackingTrack(double seconds) override
    {
        if (track.loaded) track.position = std::clamp(seconds, 0.0, track.length);
    }
    double trackVolume = 0.0;
    void setBackingTrackVolume(double volumeDb) override { trackVolume = volumeDb; }
    [[nodiscard]] engine::BackingTrackState backingTrack() const override { return track; }
    std::vector<engine::PluginParameter> parameters{{.id = 7, .name = QStringLiteral("Cutoff")},
                                                    {.id = 9, .name = QStringLiteral("Drive")}};
    [[nodiscard]] std::vector<engine::PluginParameter> pluginParameters(const core::ChannelId&, int) const override
    {
        return parameters;
    }
    std::optional<engine::PluginParameter> touched;
    std::optional<engine::PluginParameter> takeTouchedParameter(const core::ChannelId&, int) override
    {
        return std::exchange(touched, std::nullopt);
    }
    std::optional<std::pair<int, int>> movedController;
    std::optional<std::pair<int, int>> takeMovedController() override { return std::exchange(movedController, std::nullopt); }
    [[nodiscard]] std::vector<engine::AudioInputDevice> audioInputDevices() const override
    {
        return {{.driver = engine::AudioDriver::System, .name = QStringLiteral("Spy Mic"), .channels = 2}};
    }
    [[nodiscard]] int audioInputChannels() const override { return setup.inputDevice.isEmpty() ? 0 : 2; }
    [[nodiscard]] QStringList midiOutputs() const override { return {QStringLiteral("Spy Drum Machine")}; }

    // Song sections and the song's transport.
    std::vector<QString> calls; // "patch", "sections", in the order asked
    engine::SongSections sections;
    int sectionsCount = 0;
    std::pair<int, int> timeSignature{4, 4};
    void setTimeSignature(int numerator, int denominator) override { timeSignature = {numerator, denominator}; }
    void setSongSections(const engine::SongSections& chosen) override
    {
        ++sectionsCount;
        calls.push_back(QStringLiteral("sections"));
        sections = chosen;
    }
    std::optional<std::pair<int, bool>> played; // {from section, count-in}
    void playSong(int fromSection, bool countIn) override
    {
        played = {fromSection, countIn};
        const int section = std::max(fromSection, 0); // (-1: the top)
        position = {.playing = true, .countingIn = countIn, .section = section, .bar = countIn ? 0 : 1,
                    .bars = sections.sections.at(static_cast<std::size_t>(section)).bars, .part = section};
    }
    int stops = 0;
    void stopSong() override
    {
        ++stops;
        position.playing = false;
    }
    std::vector<int> jumps;
    void jumpToSection(int section) override
    {
        jumps.push_back(section);
        position.section = section;
    }
    std::vector<QString> liveControls; // "next", "part 2", "repeat", "hold", "stop", "cancel", in order
    void queueNextPart() override { liveControls.push_back(QStringLiteral("next")); }
    void queuePart(int part) override { liveControls.push_back(QStringLiteral("part %1").arg(part)); }
    void repeatPart() override { liveControls.push_back(QStringLiteral("repeat")); }
    void toggleHoldPart() override
    {
        liveControls.push_back(QStringLiteral("hold"));
        position.hold = !position.hold;
    }
    void toggleStopAtEndOfPart() override { liveControls.push_back(QStringLiteral("stop")); }
    void cancelQueuedParts() override { liveControls.push_back(QStringLiteral("cancel")); }
    engine::SongPosition position;
    [[nodiscard]] engine::SongPosition songPosition() const override { return position; }

    // Loops: what was asked, and what the test says there is.
    std::vector<std::pair<core::ChannelId, engine::LoopCommand>> loopCommands;
    void loopCommand(const core::ChannelId& channel, engine::LoopCommand command) override
    {
        loopCommands.emplace_back(channel, command);
    }
    std::optional<bool> loopSync;
    void setLoopSync(bool sync) override { loopSync = sync; }
    int loopBars = -1;
    void setLoopBars(int bars) override { loopBars = bars; }
    std::optional<bool> tempoFromLoop;
    void setTempoFromFirstLoop(bool take) override { tempoFromLoop = take; }
    int loopStops = 0;
    void stopAllLoops() override { ++loopStops; }
    int loopClears = 0;
    void clearAllLoops() override { ++loopClears; }
    std::vector<engine::ChannelLoop> channelLoops;
    [[nodiscard]] std::vector<engine::ChannelLoop> loops() const override { return channelLoops; }
    engine::LoopTriggers loopButtons{};
    engine::SelectorKnob selector;
    int loopControlsSet = 0;
    void setLoopControls(const engine::LoopTriggers& buttons, const engine::SelectorKnob& knob) override
    {
        ++loopControlsSet;
        loopButtons = buttons;
        selector = knob;
    }
    std::vector<engine::LoopAction> pendingLoopActions;
    std::vector<engine::LoopAction> takeLoopActions() override { return std::exchange(pendingLoopActions, {}); }
    engine::SelectorMove selectorMove;
    engine::SelectorMove takeSelectorMove() override { return std::exchange(selectorMove, engine::SelectorMove{}); }
    std::vector<std::array<int, 3>> controllerMoves; // taken one per call, first first
    std::optional<std::array<int, 3>> takeControllerMove() override
    {
        if (controllerMoves.empty()) return std::nullopt;
        const auto move = controllerMoves.front();
        controllerMoves.erase(controllerMoves.begin());
        return move;
    }
};

} // namespace gigchain::test
