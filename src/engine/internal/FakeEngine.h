#pragma once

#include "gigchain/engine/FakeEngineFactory.h"
#include "gigchain/engine/IEngine.h"

#include <optional>
#include <utility>
#include <vector>

namespace gigchain::engine {

// The demo engine, for tests and as a no-sound safety net: see
// FakeEngineFactory.h for why it exists. It plays nothing; the app's sound
// comes from RealEngine. "Fake" here is the testing word for a stand-in.
class FakeEngine final : public IEngine
{
public:
    explicit FakeEngine(Clock clock);

    using IEngine::applyPatch;
    void applyPatch(const core::SongId& song, const core::Patch& patch) override;
    void preload(const core::Setlist&) override {}
    void relinkInstances(const core::Setlist&) override {} // (demo plugins load nothing)
    void setProgressHandler(LoadProgress) override {}
    void setControlTriggers(const ControlTriggers&) override {}
    std::vector<ControlAction> takeControlActions() override { return {}; }
    int takeProgramChange() override { return -1; } // no MIDI input
    MidiTrigger takeLearnedTrigger() override { return {}; }
    uint32_t takeTransportRequests() override { return 0; }
    void panic() override {}
    [[nodiscard]] QStringList blockedPlugins() const override { return {}; }
    void unblockPlugin(const QString&) override {}
    std::vector<QString> storePluginStates(core::Setlist&) override { return {}; } // demo plugins have no settings
    bool takePluginEdits() override { return false; }
    [[nodiscard]] std::size_t loadedPluginCount() const override { return 0; }
    [[nodiscard]] std::vector<PluginInfo> availablePlugins() const override;
    [[nodiscard]] QString pluginFolder() const override { return {}; } // its plugins are not on disk
    [[nodiscard]] LevelReading channelLevel(const core::ChannelId& id) override;
    [[nodiscard]] LevelReading masterLevel() override;
    [[nodiscard]] float cpuLoad() const override;
    [[nodiscard]] bool midiActivity() const override;
    [[nodiscard]] MidiActivity keyboardActivity() const override { return m_keyboard; }
    void setChannelVolume(const core::ChannelId& id, double volumeDb) override;
    void setChannelPan(const core::ChannelId& id, double pan) override;
    void setChannelMute(const core::ChannelId& id, bool mute) override;
    void setChannelSolo(const core::ChannelId& id, bool solo) override;
    void setMasterVolume(double volumeDb) override;
    [[nodiscard]] double masterVolume() const override;
    void setMasterMute(bool mute) override { m_masterMuted = mute; }
    void setMasterEffects(const std::vector<core::PluginSlot>& effects) override { m_masterEffects = effects; }
    std::vector<QString> storeMasterEffectStates(std::vector<core::PluginSlot>&) override { return {}; }
    bool takeMasterEdits() override { return false; }
    core::Result<std::unique_ptr<IPluginEditor>> createMasterEffectEditor(int) override
    {
        return std::unique_ptr<IPluginEditor>(); // demo plugins have no editors
    }
    void setOutputLimiter(bool, double) override {}
    bool takeLimiterActivity() override { return false; }
    [[nodiscard]] bool masterMuted() const override { return m_masterMuted; }
    void injectNote(int midiChannel, int note, int velocity) override;
    // (The demo has no instruments to hear it; a knob learned for the mixer moves it.)
    void injectController(int midiChannel, int controller, int value) override;
    std::vector<KeyPress> takeKeyPresses() override { return std::exchange(m_presses, {}); } // (played on screen)
    std::vector<Notice> poll() override;
    [[nodiscard]] QString statusText() const override;
    [[nodiscard]] std::vector<AudioOutput> audioOutputs() const override;
    [[nodiscard]] AudioSetup audioSetup() const override { return m_setup; }
    core::Result<void> setAudioSetup(const AudioSetup& setup) override;
    [[nodiscard]] std::vector<MidiPort> midiInputs() const override { return {}; }
    [[nodiscard]] MidiSetup midiSetup() const override { return m_midiSetup; }
    core::Result<void> setMidiSetup(const MidiSetup& setup) override
    {
        m_midiSetup = setup;
        return {};
    }
    core::Result<std::unique_ptr<IPluginEditor>> createEditor(const core::ChannelId& id) override;
    core::Result<std::unique_ptr<IPluginEditor>> createEffectEditor(const core::ChannelId&, int) override
    {
        return std::unique_ptr<IPluginEditor>(); // demo plugins have no editors
    }
    core::Result<std::unique_ptr<IPluginEditor>> createEditorForPlugin(const QString& pluginId) override;

    void setTempo(double bpm) override
    {
        if (bpm >= 20.0 && bpm <= 400.0) m_tempo = bpm;
    }
    [[nodiscard]] double tempo() const override { return m_tempo; }
    void setClick(bool on, double) override { m_click = on; }
    void setClickOutput(int) override {}
    // (The demo makes no sound: nothing to record.)
    core::Result<void> startRecording(const QString&) override
    {
        return core::fail(core::ErrorCode::DeviceUnavailable, QStringLiteral("Nothing to record: running without sound"));
    }
    core::Result<double> stopRecording() override { return core::fail(core::ErrorCode::InvalidData, QStringLiteral("Not recording")); }
    [[nodiscard]] bool recording() const override { return false; }
    // (As an 8-output interface, so the output choices can be seen and tested.)
    [[nodiscard]] int outputChannels() const override { return 8; }
    [[nodiscard]] bool clickOn() const override { return m_click; }
    // The demo "reads" any track at once, as three minutes long.
    void setBackingTrack(const QString& path) override
    {
        if (path == m_track.path) return;
        m_track = BackingTrackState{.path = path, .loading = false, .loaded = !path.isEmpty(), .playing = false,
                                    .position = 0.0, .length = path.isEmpty() ? 0.0 : 180.0};
    }
    void playBackingTrack(bool play) override { m_track.playing = play && m_track.loaded; }
    void rewindBackingTrack() override { m_track.position = 0.0; }
    void setBackingTrackVolume(double) override {}
    [[nodiscard]] BackingTrackState backingTrack() const override { return m_track; }
    [[nodiscard]] std::vector<PluginParameter> pluginParameters(const core::ChannelId&, int) const override
    {
        return {{.id = 1, .name = QStringLiteral("Cutoff")}, {.id = 2, .name = QStringLiteral("Resonance")}};
    }
    std::optional<PluginParameter> takeTouchedParameter(const core::ChannelId&, int) override { return std::nullopt; }
    std::optional<std::pair<int, int>> takeMovedController() override { return std::exchange(m_moved, std::nullopt); }
    [[nodiscard]] std::vector<AudioInputDevice> audioInputDevices() const override
    {
        return {{.driver = AudioDriver::System, .name = QStringLiteral("Demo input"), .channels = 2}};
    }
    [[nodiscard]] int audioInputChannels() const override { return m_setup.inputDevice.isEmpty() ? 0 : 2; }
    [[nodiscard]] QStringList midiOutputs() const override { return {}; }

    // The demo does not count bars: Play shows the first bar of the section.
    void setTimeSignature(int, int) override {}
    void setSongSections(const SongSections& sections) override
    {
        m_sections = sections;
        m_position = SongPosition{.section = sections.sections.empty() ? -1 : 0};
    }
    void playSong(int fromSection, bool) override
    {
        fromSection = std::max(fromSection, 0); // (-1: the top)
        if (std::cmp_greater_equal(fromSection, m_sections.sections.size())) return;
        m_position = SongPosition{.playing = true, .countingIn = false, .section = fromSection, .bar = 1,
                                  .bars = m_sections.sections.at(static_cast<std::size_t>(fromSection)).bars,
                                  .part = fromSection};
    }
    void stopSong() override
    {
        m_position.playing = false;
        m_position.bar = 0;
    }
    void jumpToSection(int section) override
    {
        if (section < 0 || std::cmp_greater_equal(section, m_sections.sections.size())) return;
        m_position.section = section;
        m_position.bars = m_sections.sections.at(static_cast<std::size_t>(section)).bars;
        m_position.bar = m_position.playing ? 1 : 0;
        // The part: the section's first in the flow (or, without one, the section itself).
        const auto first = std::ranges::find(m_sections.parts, section);
        m_position.part = m_sections.parts.empty() ? section
                          : first != m_sections.parts.end() ? static_cast<int>(first - m_sections.parts.begin()) : -1;
    }
    // (No count runs here: what is queued is only shown.)
    void queueNextPart() override { m_position.queuedPart = m_position.queuedPart >= 0 ? -1 : m_position.part + 1; }
    void queuePart(int part) override
    {
        // Stopped: that part is where the song is (its section, the parts' order or the sections').
        if (!m_position.playing) {
            const bool listed = part >= 0 && std::cmp_less(part, m_sections.parts.size());
            jumpToSection(listed ? m_sections.parts.at(static_cast<std::size_t>(part)) : part);
            m_position.part = part;
            return;
        }
        m_position.queuedPart = m_position.queuedPart == part ? -1 : part;
    }
    void repeatPart() override { ++m_position.repeats; }
    void toggleHoldPart() override { m_position.hold = !m_position.hold; }
    void toggleStopAtEndOfPart() override { m_position.stopAtEnd = !m_position.stopAtEnd; }
    void cancelQueuedParts() override
    {
        m_position.queuedPart = -1;
        m_position.repeats = 0;
        m_position.hold = false;
        m_position.stopAtEnd = false;
    }
    [[nodiscard]] SongPosition songPosition() const override { return m_position; }

    // The demo's loops change state at once (no sound, no bars): a 4-bar loop.
    void loopCommand(const core::ChannelId& channel, LoopCommand command) override
    {
        auto it = std::ranges::find_if(m_loops, [&channel](const ChannelLoop& l) { return l.channel == channel; });
        if (it == m_loops.end()) {
            if (command != LoopCommand::Record) return;
            m_loops.push_back(ChannelLoop{.channel = channel});
            it = std::prev(m_loops.end());
        }
        ChannelLoop& loop = *it;
        using S = LoopState;
        switch (command) {
        case LoopCommand::Record:
            if (loop.state == S::Empty) loop.state = S::Recording;
            else if (loop.state == S::Recording) loop = ChannelLoop{.channel = channel, .state = S::Playing, .progress = 0.0, .bar = 1, .bars = 4, .layers = 0};
            else if (loop.state == S::Playing) loop.state = S::Overdubbing;
            else if (loop.state == S::Overdubbing) {
                loop.state = S::Playing;
                ++loop.layers;
            }
            break;
        case LoopCommand::PlayStop:
            if (loop.state == S::Playing || loop.state == S::Overdubbing) loop.state = S::Stopped;
            else if (loop.state == S::Stopped) loop.state = S::Playing;
            else if (loop.state == S::Recording) loop = ChannelLoop{.channel = channel, .state = S::Playing, .progress = 0.0, .bar = 1, .bars = 4, .layers = 0};
            break;
        case LoopCommand::Undo:
            if (loop.state == S::Overdubbing) loop.state = S::Playing;
            else if (loop.layers > 0) --loop.layers;
            break;
        case LoopCommand::Stop:
            if (loop.state == S::Recording) m_loops.erase(it);
            else loop.state = S::Stopped;
            break;
        case LoopCommand::Clear: m_loops.erase(it); break;
        }
    }
    void setLoopSync(bool) override {}
    void setLoopBars(int) override {}
    void setTempoFromFirstLoop(bool) override {}
    void stopAllLoops() override
    {
        std::erase_if(m_loops, [](const ChannelLoop& l) { return l.state == LoopState::Recording; });
        for (ChannelLoop& loop : m_loops) loop.state = LoopState::Stopped;
    }
    void clearAllLoops() override { m_loops.clear(); }
    [[nodiscard]] std::vector<ChannelLoop> loops() const override { return m_loops; }
    // The demo has no MIDI input: nothing is ever pressed or turned.
    void setLoopControls(const LoopTriggers&, const SelectorKnob&) override {}
    std::vector<LoopAction> takeLoopActions() override { return {}; }
    SelectorMove takeSelectorMove() override { return {}; }
    std::optional<std::array<int, 3>> takeControllerMove() override { return std::nullopt; }
    void setAppKnobs(const AppKnobs& knobs) override { m_appKnobs = knobs; }
    AppKnobValues takeAppKnobValues() override
    {
        AppKnobValues none{};
        none.fill(-1);
        return std::exchange(m_appKnobValues, none);
    }

private:
    std::optional<std::pair<int, int>> m_moved; // the last controller injected (channel 1-16, number), not yet taken
    AppKnobs m_appKnobs{};
    AppKnobValues m_appKnobValues = [] {
        AppKnobValues none{};
        none.fill(-1);
        return none;
    }();
    std::vector<ChannelLoop> m_loops;
    SongSections m_sections;
    SongPosition m_position;
    std::vector<KeyPress> m_presses; // keys played on screen, not yet taken
    double m_tempo = 120.0;
    bool m_click = false;
    MidiActivity m_keyboard; // notes played on screen (the demo has no MIDI input)
    BackingTrackState m_track;
    struct ChannelState
    {
        core::ChannelId id;
        double volumeDb = 0.0;
        bool mute = false;
        bool solo = false;
    };

    ChannelState* find(const core::ChannelId& id);

    Clock m_clock;
    std::vector<ChannelState> m_channels;
    double m_masterDb = 0.0;
    bool m_masterMuted = false;
    std::vector<core::PluginSlot> m_masterEffects;
    AudioSetup m_setup{.driver = AudioDriver::System, .device = QStringLiteral("Demo output"), .sampleRate = 48000,
                       .bufferFrames = 256, .inputDevice = {}};
    MidiSetup m_midiSetup;
};

} // namespace gigchain::engine
