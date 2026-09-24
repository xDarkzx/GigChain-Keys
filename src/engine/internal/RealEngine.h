#pragma once

#include "AudioDevice.h"
#include "GraphExchange.h"
#include "MidiInput.h"
#include "MidiQueue.h"
#include "Vst3Node.h"

#include "openstage/engine/IEngine.h"
#include "openstage/engine/RealEngineFactory.h"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace openstage::engine {

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

    void applyPatch(const core::Patch& patch) override;
    [[nodiscard]] std::vector<PluginInfo> availablePlugins() const override { return m_plugins; }
    [[nodiscard]] LevelReading channelLevel(const core::ChannelId& id) override;
    [[nodiscard]] float cpuLoad() const override { return m_cpuLoad.load(std::memory_order_relaxed); }
    [[nodiscard]] bool midiActivity() const override { return m_midiSeen.load(std::memory_order_relaxed); }
    void setChannelVolume(const core::ChannelId& id, double volumeDb) override;
    void setChannelMute(const core::ChannelId& id, bool mute) override;
    void setChannelSolo(const core::ChannelId& id, bool solo) override;
    void setMasterVolume(double volumeDb) override;
    [[nodiscard]] double masterVolume() const override { return m_masterDb; }
    void injectNote(int midiChannel, int note, int velocity) override;
    std::vector<QString> poll() override;
    [[nodiscard]] QString statusText() const override;
    core::Result<std::unique_ptr<IPluginEditor>> createEditor(const core::ChannelId& id) override;

private:
    RealEngine() = default;
    void render(AudioBlock out) noexcept;
    std::shared_ptr<Vst3Node> nodeFor(const QString& cacheKey, const core::PluginSlot& slot);

    AudioDevice m_audio;
    MidiInput m_midi;
    MidiQueue m_injected; // main thread -> audio thread
    GraphExchange m_exchange;
    std::vector<PluginInfo> m_plugins;

    // Main thread: every plugin instance created so far, by channel slot.
    std::map<QString, std::shared_ptr<Vst3Node>> m_nodes;
    std::vector<QString> m_pendingNotices;
    // Main thread: the instrument each channel of the current patch plays.
    std::map<QString, std::shared_ptr<Vst3Node>> m_currentInstruments;

    // Audio thread only.
    std::array<MidiEvent, kMaxEventsPerBlock> m_events{};

    std::atomic<float> m_masterGain{1.0F};
    double m_masterDb = 0.0;
    std::atomic<float> m_cpuLoad{0.0F};
    std::atomic<bool> m_midiSeen{false};
    std::atomic<uint64_t> m_droppedInjected{0};
};

} // namespace openstage::engine
