#pragma once

#include "PluginNode.h"
#include "SawStack.h"

#include <atomic>
#include <memory>

namespace gigchain::engine {

// Saw Stack Pro as a graph node: the app's own instrument, played, saved and
// mapped to knobs as any plugin is. Its id is kPluginId (no file).
class SawStackNode final : public PluginNode
{
public:
    static constexpr auto kPluginId = "builtin:saw-stack-pro";
    // What the instrument list shows of it.
    [[nodiscard]] static PluginInfo info();

    // Main thread.
    static std::shared_ptr<SawStackNode> create(double sampleRate, int maxBlock);

    SawStackNode() = default;

    core::Result<void> prepare(double sampleRate, int maxBlock) override;                            // main thread
    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override; // audio thread
    void queueParameter(uint32_t id, double value, int32_t sampleOffset) noexcept override;         // audio thread
    [[nodiscard]] double parameterValue(uint32_t id) const override;                                 // main thread
    [[nodiscard]] bool holdsNotes() const noexcept override;                                         // audio thread

    [[nodiscard]] std::vector<Parameter> parameters() const override;
    // The knob last turned in its window (the app's), for MIDI Learn.
    [[nodiscard]] std::optional<uint32_t> takeTouchedParameter() override;
    Problems takeProblems() override { return {}; }
    void releaseAllNotes() override { m_synth.requestReleaseAll(); }
    // Every parameter by its name (a later version adding parameters still
    // reads it: new ones keep their defaults), tagged as the built-in's.
    [[nodiscard]] core::Result<QByteArray> saveEncodedState() const override;
    core::Result<void> restoreEncodedState(const QByteArray& bytes) override;
    bool takeEdited() override { return m_edited.exchange(false, std::memory_order_relaxed); }

    [[nodiscard]] QString bundlePath() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isInstrument() const override { return true; }

    // Main thread: sets a parameter as its own window does (counts as an
    // edit, and as the knob touched for MIDI Learn).
    core::Result<void> setParameter(uint32_t id, double value) override;

protected:
    // (Its own window comes with the next step; until then it has none.)
    core::Result<std::unique_ptr<IPluginEditor>> makeEditor(const std::shared_ptr<PluginNode>&) override { return nullptr; }

private:
    sawstack::Synth m_synth;
    std::atomic<bool> m_holdsNotes{false};
    std::atomic<bool> m_edited{false};
    std::optional<uint32_t> m_touched; // main thread
};

} // namespace gigchain::engine
