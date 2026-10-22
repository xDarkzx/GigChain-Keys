#pragma once

#include "PianoPro.h"
#include "PluginNode.h"

#include <atomic>
#include <memory>

namespace gigchain::engine {

// Piano Pro as a graph node: the app's own piano, played, saved and mapped
// to knobs as any plugin is. Its id is kPluginId (no file). Its recorded
// pianos come from PianoLibrary, read in the background the first time a
// type needs them.
class PianoProNode final : public PluginNode
{
public:
    static constexpr auto kPluginId = "builtin:piano-pro";
    [[nodiscard]] static PluginInfo info();

    // Main thread.
    static std::shared_ptr<PianoProNode> create(double sampleRate, int maxBlock);

    PianoProNode() = default;

    core::Result<void> prepare(double sampleRate, int maxBlock) override;                            // main thread
    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override; // audio thread
    void queueParameter(uint32_t id, double value, int32_t sampleOffset) noexcept override;         // audio thread
    // Main thread. Also piano::kRecordingsParameter and the sidechain's position (read only).
    [[nodiscard]] double parameterValue(uint32_t id) const override;
    [[nodiscard]] bool holdsNotes() const noexcept override;                                         // audio thread

    [[nodiscard]] std::vector<Parameter> parameters() const override;
    [[nodiscard]] std::optional<uint32_t> takeTouchedParameter() override;
    Problems takeProblems() override { return {}; }
    // Asks for the piano the type plays now, when it was changed from a
    // keyboard knob (the audio thread cannot) or has not been asked for yet.
    void serviceMainThread() override { askForRecordings(); }
    void releaseAllNotes() override { m_synth.requestReleaseAll(); }
    [[nodiscard]] core::Result<QByteArray> saveEncodedState() const override;
    core::Result<void> restoreEncodedState(const QByteArray& bytes) override;
    bool takeEdited() override { return m_edited.exchange(false, std::memory_order_relaxed); }

    [[nodiscard]] QString bundlePath() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isInstrument() const override { return true; }

    core::Result<void> setParameter(uint32_t id, double value) override;

protected:
    // The app draws its window (as Saw Stack Pro's).
    core::Result<std::unique_ptr<IPluginEditor>> makeEditor(const std::shared_ptr<PluginNode>&) override { return nullptr; }

private:
    // Main thread: the current type's recorded piano, asked for (read once,
    // shared) unless it already was at this rate.
    void askForRecordings();

    piano::Synth m_synth;
    double m_rate = 0.0;
    std::array<bool, piano::kSources> m_asked{}; // main thread: at m_rate
    std::atomic<bool> m_holdsNotes{false};
    std::atomic<bool> m_edited{false};
    std::optional<uint32_t> m_touched; // main thread
};

} // namespace gigchain::engine
