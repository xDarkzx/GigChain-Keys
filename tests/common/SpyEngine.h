#pragma once

#include "gigchain/engine/IEngine.h"

#include <array>
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
    std::vector<QString> pendingNotices;

    using engine::IEngine::applyPatch;
    int preloadCount = 0;
    void preload(const core::Setlist&) override { ++preloadCount; }
    void setProgressHandler(engine::LoadProgress) override {}
    [[nodiscard]] std::size_t loadedPluginCount() const override { return 0; }
    void applyPatch(const core::SongId&, const core::Patch& patch) override
    {
        ++applyCount;
        lastPatch = patch;
    }
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
    [[nodiscard]] float cpuLoad() const override { return 0.25F; }
    [[nodiscard]] bool midiActivity() const override { return true; }
    void setChannelVolume(const core::ChannelId& id, double db) override { volumes[id.value()] = db; }
    void setChannelPan(const core::ChannelId& id, double pan) override { pans[id.value()] = pan; }
    void setChannelMute(const core::ChannelId& id, bool mute) override { mutes[id.value()] = mute; }
    void setChannelSolo(const core::ChannelId& id, bool solo) override { solos[id.value()] = solo; }
    void setMasterVolume(double db) override { master = db; }
    [[nodiscard]] double masterVolume() const override { return master; }
    void injectNote(int channel, int note, int velocity) override { notes.push_back({channel, note, velocity}); }
    std::vector<QString> poll() override
    {
        std::vector<QString> out;
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
    int fitRequests = 0;
    core::Result<bool> fitEditorToArea(const core::ChannelId&, QSize, QSize) override
    {
        ++fitRequests;
        return false;
    }
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
