#pragma once

#include "AudioDevice.h"
#include "GraphExchange.h"
#include "MidiInput.h"
#include "MidiQueue.h"
#include "PluginLoadGuard.h"
#include "SafetyLimiter.h"
#include "Vst3Node.h"

#include "gigchain/engine/IEngine.h"
#include "gigchain/engine/RealEngineFactory.h"

#include <array>
#include <chrono>
#include <atomic>
#include <map>
#include <set>
#include <memory>
#include <vector>

namespace gigchain::engine {

// The engine that makes sound. Main thread: applyPatch builds a RenderGraph
// (plugins cached per channel so returning to a patch is instant) and
// publishes it; mixer calls change atomics on the live graph. Audio thread:
// drain MIDI, render the current graph, measure load. See
// docs/superpowers/specs/2026-09-24-real-engine-design.md.
class RealEngine final : public IEngine
{
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
    [[nodiscard]] LevelReading channelLevel(const core::ChannelId& id) override;
    [[nodiscard]] LevelReading masterLevel() override;
    [[nodiscard]] float cpuLoad() const override { return m_cpuLoad.load(std::memory_order_relaxed); }
    [[nodiscard]] bool midiActivity() const override { return m_midiSeen.load(std::memory_order_relaxed); }
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

private:
    RealEngine() = default;
    void render(AudioBlock out) noexcept;
    // The plugin instance for a slot, loaded if needed (logged; a failure is
    // reported to the user). `announce`: show the load in the progress UI.
    std::shared_ptr<Vst3Node> nodeFor(const QString& key, const core::PluginSlot& slot, bool announce);
    // Loads a plugin with its slot's settings (a failure to take them is
    // reported, and it plays at its defaults). nullptr when it cannot load
    // (reported). Loading is not an edit.
    std::shared_ptr<Vst3Node> loadWithSettings(const core::PluginSlot& slot);
    // The instance key of each master effect, by position (empty: switched off).
    [[nodiscard]] std::vector<QString> masterKeys() const;
    // Every plugin slot of a patch with the key of the instance it plays:
    // song + plugin + its position among the patch's uses of that plugin.
    struct PlannedSlot
    {
        QString key;
        const core::PluginSlot* slot = nullptr;
        int channel = 0;
        int effect = -1; // -1 = the channel's instrument
    };
    static std::vector<PlannedSlot> planPatch(const core::SongId& song, const core::Patch& patch);
    core::Result<void> openAudio(const AudioSetup& setup);
    // After the device changed rate or block size: with audio paused, every
    // plugin is re-prepared and the patch rebuilt for the new size.
    void syncPluginsToDevice();
    // Opens the inputs plugged in now as m_midiSetup says; problems returned (each logged).
    std::vector<QString> openMidi();
    // Every couple of seconds: notices a keyboard plugged in or pulled out.
    void watchMidiPorts(std::vector<Notice>& notices);

    AudioDevice m_audio;
    MidiInput m_midi;
    MidiQueue m_injected; // main thread -> audio thread
    GraphExchange m_exchange;
    std::vector<PluginInfo> m_plugins;
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
    LoadProgress m_progress;
    QStringList m_midiPorts; // what was plugged in at the last check
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
    // Audio thread: takes control messages out of `count` events (in place).
    std::size_t takeControlMessages(std::size_t count) noexcept;
};

} // namespace gigchain::engine
