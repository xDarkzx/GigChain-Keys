#pragma once

#include "AudioDevice.h"
#include "AudioFile.h"
#include "GraphExchange.h"
#include "Metronome.h"
#include "MidiClockOut.h"
#include "MidiInput.h"
#include "MidiMonitor.h"
#include "MidiQueue.h"
#include "PluginLoadGuard.h"
#include "SafetyLimiter.h"
#include "LoopStation.h"
#include "SongTransport.h"
#include "Vst3Node.h"

#include "gigchain/core/PluginSharing.h"
#include "gigchain/engine/IEngine.h"
#include "gigchain/engine/RealEngineFactory.h"

#include <QMediaDevices>
#include <QThread>

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

class TestRealEngine;

namespace gigchain::engine {

// The engine that makes sound. Main thread: applyPatch builds a RenderGraph
// (plugins cached per channel so returning to a patch is instant) and
// publishes it; mixer calls change atomics on the live graph. Audio thread:
// drain MIDI, render the current graph, measure load.
class RealEngine final : public IEngine
{
    friend class ::TestRealEngine; // unplugs the MIDI clock's output

public:
    static core::Result<std::unique_ptr<RealEngine>> create(const RealEngineOptions& options);
    ~RealEngine() override;

    using IEngine::applyPatch;
    void applyPatch(const core::SongId& song, const core::Patch& patch) override;
    void preload(const core::Setlist& setlist) override;
    void setProgressHandler(LoadProgress handler) override { m_progress = std::move(handler); }
    [[nodiscard]] std::size_t loadedPluginCount() const override { return m_nodes.size(); }
    [[nodiscard]] QStringList blockedPlugins() const override { return m_guard.blocked(); }
    void unblockPlugin(const QString& pluginId) override { m_guard.unblock(pluginId); }
    std::vector<QString> storePluginStates(core::Setlist& setlist) override;
    bool takePluginEdits() override;
    [[nodiscard]] std::vector<PluginInfo> availablePlugins() const override { return m_plugins; }
    [[nodiscard]] QString pluginFolder() const override { return m_pluginFolder; }
    [[nodiscard]] LevelReading channelLevel(const core::ChannelId& id) override;
    [[nodiscard]] LevelReading masterLevel() override;
    [[nodiscard]] float cpuLoad() const override { return m_cpuLoad.load(std::memory_order_relaxed); }
    [[nodiscard]] bool midiActivity() const override { return m_midiSeen.load(std::memory_order_relaxed); }
    [[nodiscard]] MidiActivity keyboardActivity() const override { return m_keyboard.read(); }
    void setChannelVolume(const core::ChannelId& id, double volumeDb) override;
    void setChannelPan(const core::ChannelId& id, double pan) override;
    void setChannelMute(const core::ChannelId& id, bool mute) override;
    void setChannelSolo(const core::ChannelId& id, bool solo) override;
    void setMasterVolume(double volumeDb) override;
    void setMasterMute(bool mute) override;
    void setControlTriggers(const ControlTriggers& triggers) override;
    std::vector<ControlAction> takeControlActions() override;
    int takeProgramChange() override;
    MidiTrigger takeLearnedTrigger() override;
    uint32_t takeTransportRequests() override { return m_midi.takeTransportRequests(); }
    void panic() override;
    void setMasterEffects(const std::vector<core::PluginSlot>& effects) override;
    std::vector<QString> storeMasterEffectStates(std::vector<core::PluginSlot>& effects) override;
    bool takeMasterEdits() override;
    core::Result<std::unique_ptr<IPluginEditor>> createMasterEffectEditor(int effect) override;
    void setOutputLimiter(bool enabled, double ceilingDb) override;
    bool takeLimiterActivity() override { return m_limiter.takeActivity(); }
    [[nodiscard]] bool masterMuted() const override { return m_masterMuted; }
    [[nodiscard]] double masterVolume() const override { return m_masterDb; }
    void injectNote(int midiChannel, int note, int velocity) override;
    void injectController(int midiChannel, int controller, int value) override;
    std::vector<KeyPress> takeKeyPresses() override { return m_midi.takePresses(); }
    std::vector<Notice> poll() override;
    [[nodiscard]] QString statusText() const override;
    [[nodiscard]] std::vector<AudioOutput> audioOutputs() const override;
    [[nodiscard]] AudioSetup audioSetup() const override;
    core::Result<void> setAudioSetup(const AudioSetup& setup) override;
    [[nodiscard]] std::vector<MidiPort> midiInputs() const override;
    [[nodiscard]] MidiSetup midiSetup() const override { return m_midiSetup; }
    core::Result<void> setMidiSetup(const MidiSetup& setup) override;
    core::Result<std::unique_ptr<IPluginEditor>> createEditor(const core::ChannelId& id) override;
    core::Result<std::unique_ptr<IPluginEditor>> createEffectEditor(const core::ChannelId& id, int effect) override;
    core::Result<std::unique_ptr<IPluginEditor>> createEditorForPlugin(const QString& pluginId) override;

    void setTempo(double bpm) override;
    [[nodiscard]] double tempo() const override;
    void setTimeSignature(int numerator, int denominator) override;
    void setSongSections(const SongSections& sections) override;
    void playSong(int fromSection, bool countIn) override;
    void stopSong() override { m_transport.stop(); }
    void jumpToSection(int section) override;
    void queueNextPart() override;
    void queuePart(int part) override;
    void repeatPart() override;
    void toggleHoldPart() override;
    void toggleStopAtEndOfPart() override;
    void cancelQueuedParts() override;
    [[nodiscard]] SongPosition songPosition() const override { return m_transport.position(); }
    void loopCommand(const core::ChannelId& channel, LoopCommand command) override;
    void setLoopSync(bool sync) override { m_loops.setSync(sync); }
    void setLoopBars(int bars) override { m_loops.setTargetLines(bars); }
    void setTempoFromFirstLoop(bool take) override { m_tempoFromLoop = take; }
    void stopAllLoops() override;
    void clearAllLoops() override;
    [[nodiscard]] std::vector<ChannelLoop> loops() const override;
    void setLoopControls(const LoopTriggers& buttons, const SelectorKnob& selector) override;
    std::vector<LoopAction> takeLoopActions() override;
    SelectorMove takeSelectorMove() override;
    std::optional<std::array<int, 3>> takeControllerMove() override;
    void setAppKnobs(const AppKnobs& knobs) override;
    AppKnobValues takeAppKnobValues() override;
    void setClick(bool on, double volumeDb) override;
    [[nodiscard]] bool clickOn() const override { return m_click.isOn(); }
    void setBackingTrack(const QString& path) override;
    void playBackingTrack(bool play) override;
    void rewindBackingTrack() override { m_trackRewind.store(true, std::memory_order_relaxed); }
    void setBackingTrackVolume(double volumeDb) override { m_trackGain.store(dbToGain(volumeDb), std::memory_order_relaxed); }
    [[nodiscard]] BackingTrackState backingTrack() const override;
    [[nodiscard]] std::vector<PluginParameter> pluginParameters(const core::ChannelId& id, int target) const override;
    std::optional<PluginParameter> takeTouchedParameter(const core::ChannelId& id, int target) override;
    std::optional<std::pair<int, int>> takeMovedController() override;
    [[nodiscard]] std::vector<AudioInputDevice> audioInputDevices() const override;
    [[nodiscard]] int audioInputChannels() const override { return m_audio.inputChannels(); }
    [[nodiscard]] QStringList midiOutputs() const override { return MidiClockOut::listPorts(); }

private:
    RealEngine() = default;
    void render(AudioBlock out, const AudioInputs& inputs) noexcept;
    // The plugin a channel's slot plays in the current patch (target -1 =
    // its instrument), or null.
    [[nodiscard]] std::shared_ptr<Vst3Node> currentNode(const core::ChannelId& id, int target) const;
    // Main thread: a finished backing-track read becomes the track; a track
    // read for another sample rate is read again.
    void collectBackingTrack(std::vector<Notice>& notices);
    void startReadingTrack(const QString& path);
    // The plugin instance for a slot, loaded if needed (logged; a failure is
    // reported to the user). `announce`: show the load in the progress UI.
    std::shared_ptr<Vst3Node> nodeFor(const QString& key, const core::PluginSlot& slot, bool announce);
    // Loads a plugin with its slot's settings (a failure to take them is
    // reported, and it plays at its defaults). nullptr when it cannot load
    // (reported). Loading is not an edit.
    std::shared_ptr<Vst3Node> loadWithSettings(const core::PluginSlot& slot);
    // Whether `pluginId` (a .vst3 file) is an installed plugin: a file inside
    // the plugin folder or the app's own (followed through "..", links and
    // junctions), read or not (one switched off after a crash still is).
    // checkInstalled also says why not (logged).
    [[nodiscard]] bool isInstalledPlugin(const QString& pluginId) const;
    [[nodiscard]] core::Result<void> checkInstalled(const QString& pluginId, const QString& name) const;
    // The instance key of each master effect, by position (empty: switched off).
    [[nodiscard]] std::vector<QString> masterKeys() const;
    // Every plugin slot of a patch with the key of the instance it plays:
    // song + plugin + its position among the patch's uses of that plugin.
    // Each playing slot and the instance it plays (shared across songs by
    // its share id: core/PluginSharing.h).
    using PlannedSlot = core::PluginUse;
    static std::vector<PlannedSlot> planPatch(const core::SongId& song, const core::Patch& patch)
    {
        return core::pluginUses(song, patch);
    }
    core::Result<void> openAudio(const AudioSetup& setup);
    // After the device changed rate or block size: with audio paused, every
    // plugin is re-prepared and the patch rebuilt for the new size.
    void syncPluginsToDevice();
    // Opens the inputs plugged in now as m_midiSetup says; problems returned (each logged).
    std::vector<QString> openMidi();
    // Every couple of seconds: notices a keyboard plugged in or pulled out.
    void watchMidiPorts(std::vector<Notice>& notices);
    // Follows m_midiSetup's clock choices: whether the tempo follows an
    // incoming clock, and where the clock is sent. Problems returned (logged).
    std::vector<QString> applyClockSetup();
    // Main thread: the song's sections as a timeline for the audio thread,
    // from m_sections and the time signature.
    void publishTimeline();
    // The song's parts (each a section), in playing order: the flow, else each section once.
    [[nodiscard]] std::vector<int> songParts() const;
    // The part a section is reached at: its next time after the part in force, else its first.
    [[nodiscard]] int partForSection(int section) const;
    // Main thread: which sections each strip of `graph` plays in (all, when
    // the sections were worked out for another patch).
    void applySectionMasks(RenderGraph& graph) const;
    // Main thread: each strip of `graph` records into its channel's loop slot.
    void applyLoopSlots(RenderGraph& graph) const;
    // Main thread, from poll(): layers for loops just closed, room freed
    // for loops cleared, and what the loops have to say.
    void serviceLoops(std::vector<Notice>& notices);
    // The slot a channel's loop is in; -1 when none.
    [[nodiscard]] int loopSlot(const core::ChannelId& channel) const;
    // Frames in a bar at the tempo and time now.
    [[nodiscard]] double barFrames() const;

    AudioDevice m_audio;
    // Windows' audio devices changing (plugged in or out, a new default):
    // tells m_audio to look again.
    std::unique_ptr<QMediaDevices> m_mediaDevices;
    MidiInput m_midi;
    MidiQueue m_injected; // main thread -> audio thread
    GraphExchange m_exchange;
    std::vector<PluginInfo> m_plugins;
    QString m_pluginFolder;        // where m_plugins were found
    QStringList m_otherPluginFolders; // the system's other standard folders that exist (Linux)
    QString m_bundledPluginFolder; // the app's own plugins (empty: none)
    PluginLoadGuard m_guard; // plugins that crashed the app while loading

    // Main thread: every plugin instance created so far, by channel slot.
    std::map<QString, std::shared_ptr<Vst3Node>> m_nodes;
    // Per instance: the settings it was loaded with or last stored (as the
    // setlist holds them), and whether it was changed since.
    std::map<QString, QByteArray> m_nodeStates;
    std::set<QString> m_editedNodes;
    bool m_unreportedEdit = false;
    // Collects the plugins' edit reports into m_editedNodes.
    void collectEdits();
    std::vector<Notice> m_pendingNotices;
    core::Patch m_patch;         // the sounding patch, rebuilt after a device change
    core::SongId m_song;         // the song it belongs to
    double m_preparedRate = 0.0; // what the plugins are prepared for
    int m_preparedBlock = 0;
    MidiSetup m_midiSetup;
    bool m_midiInputs = true; // false: none opened (RealEngineOptions::midiInputs)
    LoadProgress m_progress;
    QStringList m_midiPorts;   // what was plugged in at the last check
    QStringList m_midiOutputs; // the outputs plugged in at the last check
    std::chrono::steady_clock::time_point m_lastMidiCheck{};
    // Main thread: the instrument each channel of the current patch plays.
    std::map<QString, std::shared_ptr<Vst3Node>> m_currentInstruments;
    // The master bus: its slots, their instances (outside any setlist), and
    // whether one was edited since the last takeMasterEdits().
    std::vector<core::PluginSlot> m_masterSlots;
    std::map<QString, std::shared_ptr<Vst3Node>> m_masterNodes;
    bool m_masterEdited = false;
    SafetyLimiter m_limiter;
    double m_limiterRate = 0.0; // audio thread: the rate the limiter is set for
    // ... and its effects, by position (nullptr: switched off or not loaded).
    std::map<QString, std::vector<std::shared_ptr<Vst3Node>>> m_currentEffects;

    // Audio thread only.
    std::array<MidiEvent, kMaxEventsPerBlock> m_events{};

    std::atomic<float> m_masterGain{1.0F};
    // What left the app (after the master fader): peak since masterLevel()
    // last asked, and the last block's RMS.
    std::atomic<float> m_masterPeak{0.0F};
    std::atomic<float> m_masterRms{0.0F};
    double m_masterDb = 0.0;
    bool m_masterMuted = false;
    std::atomic<float> m_cpuLoad{0.0F};
    std::atomic<bool> m_midiSeen{false};
    std::atomic<uint64_t> m_droppedInjected{0};
    // Control triggers (packed MidiTrigger per ControlAction), actions pressed
    // (one bit each) and the last learnable press. Audio thread reads/sets,
    // main thread writes/takes.
    std::array<std::atomic<uint32_t>, kControlActionCount> m_triggers{};
    std::atomic<uint32_t> m_pressedActions{0};
    std::atomic<uint32_t> m_learned{0};
    std::atomic<int> m_program{-1}; // the last Program Change, -1 when none since taken
    // The last CC moved: (channel (0-15) * 128 + number) * 128 + value; -1 = none since taken.
    std::atomic<int> m_movedController{-1};
    // The looper's buttons (packed triggers), their presses (one bit per
    // LoopAction), which are held (audio thread), and the instrument knob.
    std::array<std::atomic<uint32_t>, kLoopButtonCount> m_loopTriggers{};
    std::atomic<uint32_t> m_loopPressed{0};
    uint32_t m_loopHeld = 0;
    std::atomic<uint32_t> m_selectorKnob{0};
    std::atomic<int> m_selectorMode{0};
    std::atomic<int> m_selectorValue{-1};
    std::atomic<int> m_selectorSteps{0};
    // The app's knobs (packed triggers) and each one's last value + 1 (0: not moved since taken).
    std::array<std::atomic<uint32_t>, kAppKnobCount> m_appKnobs{};
    std::array<std::atomic<int>, kAppKnobCount> m_appKnobValues{};
    // Audio thread: takes control messages out of `count` events (in place).
    std::size_t takeControlMessages(std::size_t count) noexcept;

    // The clock plugins and the click follow. Tempo set by the main thread;
    // position kept by the audio thread.
    std::atomic<double> m_tempo{120.0};
    std::atomic<bool> m_followClock{false};
    int64_t m_samplePosition = 0; // audio thread
    double m_ppq = 0.0;           // audio thread
    std::atomic<int> m_timeNumerator{4};
    std::atomic<int> m_timeDenominator{4};
    Metronome m_click;
    // The song's sections: as set (main thread), as a timeline (to the audio
    // thread), and the play/stop/jump counting through them.
    SongSections m_sections;
    HazardExchange<SongTimeline> m_timeline;
    SongTransport m_transport;
    // The loop station: which channel owns each slot (main thread), and the
    // first free loop setting the tempo.
    LoopStation m_loops;
    std::array<core::ChannelId, LoopStation::kSlots> m_loopOwners{};
    std::array<uint32_t, LoopStation::kSlots> m_loopFreshSent{}; // undone layers given fresh buffers
    bool m_tempoFromLoop = false;
    bool m_freeTempoTaken = false;
    // Audio thread: bar 1 is moved to this sample (-1: nothing to do).
    std::atomic<int64_t> m_barOriginAt{-1};
    MidiClockOut m_clockOut;
    MidiMonitor m_keyboard; // what is being played, for the on-screen keyboard

    // The backing track: read on a worker thread, handed to the audio thread
    // through the exchange. Play state and position are atomics.
    HazardExchange<AudioClip> m_track;
    QString m_trackPath;          // main thread: the file asked for
    std::atomic<bool> m_trackPlaying{false};
    std::atomic<bool> m_trackRewind{false};
    std::atomic<int64_t> m_trackPosition{0}; // frames; written by the audio thread
    std::atomic<float> m_trackGain{1.0F};
    // The reading in progress, and its outcome (guarded by m_trackMutex).
    std::unique_ptr<QThread> m_trackReader;
    std::atomic<bool> m_cancelTrackRead{false};
    std::mutex m_trackMutex;
    std::optional<core::Result<AudioClip>> m_trackRead;
    QString m_trackReadPath; // the path m_trackRead is for
    QString m_trackFailed;   // main thread: a path that could not be read (not tried again until asked again)
};

} // namespace gigchain::engine
