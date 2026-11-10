#pragma once

#include "PluginNode.h"
#include "SoundFont.h"

#include <atomic>
#include <memory>

namespace gigchain::engine {

// The SoundFont Player as a graph node: plays a .sf2 file, with FL Studio's
// SoundFont Player's controls (sf2::Param). Played, saved and mapped to
// knobs as any plugin is; its file is saved with its settings (by path).
class SoundFontNode final : public PluginNode
{
public:
    static constexpr auto kPluginId = "builtin:soundfont";
    [[nodiscard]] static PluginInfo info();

    // Main thread.
    static std::shared_ptr<SoundFontNode> create(double sampleRate, int maxBlock);

    SoundFontNode() = default;

    core::Result<void> prepare(double sampleRate, int maxBlock) override;                            // main thread
    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override; // audio thread
    void queueParameter(uint32_t id, double value, int32_t sampleOffset) noexcept override;         // audio thread
    [[nodiscard]] double parameterValue(uint32_t id) const override;                                 // main thread
    [[nodiscard]] bool holdsNotes() const noexcept override;                                         // audio thread

    [[nodiscard]] std::vector<Parameter> parameters() const override;
    [[nodiscard]] std::optional<uint32_t> takeTouchedParameter() override;
    Problems takeProblems() override { return {}; }
    void serviceMainThread() override { m_player.service(); }
    void releaseAllNotes() override { m_player.requestReleaseAll(); }
    [[nodiscard]] QStringList takeMessages() override { return m_player.takeMessages(); }
    // The file by its path, the preset by bank and program, every control by its name.
    [[nodiscard]] core::Result<QByteArray> saveEncodedState() const override;
    core::Result<void> restoreEncodedState(const QByteArray& bytes) override;
    bool takeEdited() override { return m_edited.exchange(false, std::memory_order_relaxed); }

    core::Result<void> setFile(const QString& path) override;
    [[nodiscard]] QString file() const override { return m_player.file(); }

    [[nodiscard]] QString bundlePath() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isInstrument() const override { return true; }

    // Main thread: as its own window sets it.
    core::Result<void> setParameter(uint32_t id, double value) override;

protected:
    // Its window is the app's own (SoundFontPanel.qml).
    core::Result<std::unique_ptr<IPluginEditor>> makeEditor(const std::shared_ptr<PluginNode>&) override { return nullptr; }

private:
    sf2::Player m_player;
    std::atomic<bool> m_edited{false};
    std::optional<uint32_t> m_touched; // main thread
};

} // namespace gigchain::engine
