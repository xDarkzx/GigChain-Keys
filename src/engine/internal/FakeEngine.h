#pragma once

#include "gigchain/engine/FakeEngineFactory.h"
#include "gigchain/engine/IEngine.h"

#include <vector>

namespace gigchain::engine {

class FakeEngine final : public IEngine
{
public:
    explicit FakeEngine(Clock clock);

    using IEngine::applyPatch;
    void applyPatch(const core::SongId& song, const core::Patch& patch) override;
    void preload(const core::Setlist&) override {}
    void setProgressHandler(LoadProgress) override {}
    std::vector<QString> storePluginStates(core::Setlist&) override { return {}; } // demo plugins have no settings
    bool takePluginEdits() override { return false; }
    [[nodiscard]] std::size_t loadedPluginCount() const override { return 0; }
    [[nodiscard]] std::vector<PluginInfo> availablePlugins() const override;
    [[nodiscard]] LevelReading channelLevel(const core::ChannelId& id) override;
    [[nodiscard]] LevelReading masterLevel() override;
    [[nodiscard]] float cpuLoad() const override;
    [[nodiscard]] bool midiActivity() const override;
    void setChannelVolume(const core::ChannelId& id, double volumeDb) override;
    void setChannelPan(const core::ChannelId& id, double pan) override;
    void setChannelMute(const core::ChannelId& id, bool mute) override;
    void setChannelSolo(const core::ChannelId& id, bool solo) override;
    void setMasterVolume(double volumeDb) override;
    [[nodiscard]] double masterVolume() const override;
    void injectNote(int midiChannel, int note, int velocity) override;
    std::vector<QString> poll() override;
    [[nodiscard]] QString statusText() const override;
    [[nodiscard]] std::vector<AudioOutput> audioOutputs() const override;
    [[nodiscard]] AudioSetup audioSetup() const override { return m_setup; }
    core::Result<bool> fitEditorToArea(const core::ChannelId&, QSize, QSize) override { return false; }
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

private:
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
    AudioSetup m_setup{AudioDriver::System, QStringLiteral("Demo output"), 48000, 256};
    MidiSetup m_midiSetup;
};

} // namespace gigchain::engine
