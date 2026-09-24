#pragma once

#include "openstage/engine/FakeEngineFactory.h"
#include "openstage/engine/IEngine.h"

#include <vector>

namespace openstage::engine {

class FakeEngine final : public IEngine
{
public:
    explicit FakeEngine(Clock clock);

    void applyPatch(const core::Patch& patch) override;
    [[nodiscard]] std::vector<PluginInfo> availablePlugins() const override;
    [[nodiscard]] LevelReading channelLevel(const core::ChannelId& id) override;
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
    core::Result<std::unique_ptr<IPluginEditor>> createEditor(const core::ChannelId& id) override;

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
};

} // namespace openstage::engine
