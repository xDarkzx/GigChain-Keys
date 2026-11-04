#pragma once

#include "PadsPro.h"
#include "PluginNode.h"

#include <atomic>
#include <memory>

namespace gigchain::engine {

// Pads Pro as a graph node: the app's own pad instrument, played, saved and
// mapped to knobs as any plugin is. Its id is kPluginId (no file).
class PadsProNode final : public PluginNode
{
public:
    static constexpr auto kPluginId = "builtin:pads-pro";
    [[nodiscard]] static PluginInfo info();

    // Main thread.
    static std::shared_ptr<PadsProNode> create(double sampleRate, int maxBlock);

    PadsProNode() = default;

    core::Result<void> prepare(double sampleRate, int maxBlock) override;                            // main thread
    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override; // audio thread
    void queueParameter(uint32_t id, double value, int32_t sampleOffset) noexcept override;         // audio thread
    [[nodiscard]] double parameterValue(uint32_t id) const override;                                 // main thread
    [[nodiscard]] bool holdsNotes() const noexcept override;                                         // audio thread

    [[nodiscard]] std::vector<Parameter> parameters() const override;
    [[nodiscard]] std::optional<uint32_t> takeTouchedParameter() override;
    Problems takeProblems() override { return {}; }
    void releaseAllNotes() override { m_synth.requestReleaseAll(); }
    // Every parameter by its name, the pad by its key (pads may be added or reordered).
    [[nodiscard]] core::Result<QByteArray> saveEncodedState() const override;
    core::Result<void> restoreEncodedState(const QByteArray& bytes) override;
    bool takeEdited() override { return m_edited.exchange(false, std::memory_order_relaxed); }

    [[nodiscard]] QString bundlePath() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isInstrument() const override { return true; }

    // Main thread: as its own window sets it (an edit, and the knob touched for MIDI Learn).
    core::Result<void> setParameter(uint32_t id, double value) override;

protected:
    // Its window is the app's own (PadsProPanel.qml).
    core::Result<std::unique_ptr<IPluginEditor>> makeEditor(const std::shared_ptr<PluginNode>&) override { return nullptr; }

private:
    pads::Synth m_synth;
    std::atomic<bool> m_holdsNotes{false};
    std::atomic<bool> m_edited{false};
    std::optional<uint32_t> m_touched; // main thread
};

} // namespace gigchain::engine
