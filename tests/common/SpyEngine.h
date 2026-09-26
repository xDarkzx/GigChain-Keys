#pragma once

#include "gigchain/engine/IEngine.h"

#include <array>
#include <utility>
#include <map>
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
        lastPatch = patch;
    }
    [[nodiscard]] QString pluginFolder() const override { return {}; } // its plugins are not on disk
    [[nodiscard]] std::vector<engine::PluginInfo> availablePlugins() const override
    {
        return {
            {QStringLiteral("spy/Piano.vst3"), QStringLiteral("Spy Piano"), QStringLiteral("Spy"), engine::PluginKind::Instrument,
             QStringLiteral("Instrument|Piano"), QStringLiteral("1.0"), {}, QStringLiteral("https://spy.example"),
             QStringLiteral("help@spy.example"), QStringLiteral("VST 3.8.0")},
            {QStringLiteral("spy/Pad.vst3"), QStringLiteral("Spy Pad"), QStringLiteral("Spy"), engine::PluginKind::Instrument,
             QStringLiteral("Instrument|Synth"), QStringLiteral("1.0")},
            {QStringLiteral("spy/Reverb.vst3"), QStringLiteral("Spy Reverb"), QStringLiteral("Other"), engine::PluginKind::Effect,
             QStringLiteral("Fx|Reverb"), QStringLiteral("1.0")},
        };
    }
    [[nodiscard]] engine::LevelReading channelLevel(const core::ChannelId&) override { return {0.5F, 0.25F}; }
    [[nodiscard]] engine::LevelReading masterLevel() override { return {0.4F, 0.2F}; }
    [[nodiscard]] float cpuLoad() const override { return 0.25F; }
    [[nodiscard]] bool midiActivity() const override { return true; }
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

    engine::AudioSetup setup{engine::AudioDriver::System, QStringLiteral("Spy Speakers"), 48000, 256};
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
};

} // namespace gigchain::test
