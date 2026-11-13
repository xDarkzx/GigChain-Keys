#pragma once

#include "Enhance.h"
#include "PluginNode.h"
#include "SawStackFx.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace gigchain::engine {

// The app's own effects (Squash, Enhance, Sidechain): loaded into any channel's
// or bus's effects as any plugin is, saved with it and mapped to knobs. Their
// windows are the app's (BuiltinFxWindow.qml). What they share is here: their
// settings (0-1 each, by name in the saved state), set from the window on the
// main thread or by a mapped knob on the audio thread; each block reads them.
class BuiltinEffectNode : public PluginNode
{
public:
    // One setting: its key (saved), its name (shown), where it starts, and a
    // stepped one's settings by name.
    struct Spec
    {
        const char* key = "";
        const char* name = "";
        double start = 0.0;
        std::vector<const char*> choices{};
    };
    static constexpr std::size_t kMaxSettings = 8;

    core::Result<void> prepare(double sampleRate, int maxBlock) override;
    void queueParameter(uint32_t id, double value, int32_t sampleOffset) noexcept override;
    [[nodiscard]] double parameterValue(uint32_t id) const override;

    [[nodiscard]] std::vector<Parameter> parameters() const override;
    core::Result<void> setParameter(uint32_t id, double value) override;
    [[nodiscard]] std::optional<uint32_t> takeTouchedParameter() override;
    Problems takeProblems() override { return {}; }
    void releaseAllNotes() override {}
    [[nodiscard]] core::Result<QByteArray> saveEncodedState() const override;
    core::Result<void> restoreEncodedState(const QByteArray& bytes) override;
    bool takeEdited() override { return m_edited.exchange(false, std::memory_order_relaxed); }
    [[nodiscard]] bool isInstrument() const override { return false; }

protected:
    explicit BuiltinEffectNode(std::vector<Spec> specs);
    // The effect's DSP at this rate (main thread, before process()).
    virtual void prepareEffect(double sampleRate) = 0;
    // What the window reads besides the settings (meters, a playhead), by id
    // (ids from kReadings up); below 0 when there is no such reading.
    [[nodiscard]] virtual double reading(uint32_t /*id*/) const { return -1.0; }
    [[nodiscard]] double setting(std::size_t index) const { return m_values.at(index).load(std::memory_order_relaxed); }
    core::Result<std::unique_ptr<IPluginEditor>> makeEditor(const std::shared_ptr<PluginNode>&) override { return nullptr; }

public:
    // Read-only values start here (never settings, never saved).
    static constexpr uint32_t kReadings = 0x5C00;

private:
    std::vector<Spec> m_specs;
    std::array<std::atomic<double>, kMaxSettings> m_values{};
    std::atomic<bool> m_edited{false};
    std::optional<uint32_t> m_touched; // main thread
};

// Squash: the three-band squash (Vital's multiband compressor at its default
// settings): quiet parts pulled up, loud parts pushed down, each band apart.
// Shown as "Squash" (its id, "builtin:ott", is only what songs save).
class OttEffectNode final : public BuiltinEffectNode
{
public:
    static constexpr auto kPluginId = "builtin:ott";
    enum Setting : uint32_t { Depth, Time, Upward, Downward, InGain, OutGain };
    // The gains' range either side of 0 dB (0.5 is 0 dB).
    static constexpr double kGainRangeDb = 24.0;
    [[nodiscard]] static PluginInfo info();
    static std::shared_ptr<OttEffectNode> create(double sampleRate, int maxBlock);
    OttEffectNode();

    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override;
    [[nodiscard]] QString bundlePath() const override { return QString::fromLatin1(kPluginId); }
    [[nodiscard]] QString name() const override { return QStringLiteral("Squash"); }

protected:
    void prepareEffect(double sampleRate) override { m_ott.prepare(sampleRate); }
    // kReadings + 0..2: the lows', mids' and highs' work in dB (0.5 + dB/48: 0.5 is none).
    [[nodiscard]] double reading(uint32_t id) const override;

private:
    sawstack::Ott m_ott;
    std::array<std::atomic<float>, 3> m_bandDb{};
};

// Enhance: one knob that makes a sound fuller and more finished, four
// settings A-D (sawstack::Enhance).
class EnhanceEffectNode final : public BuiltinEffectNode
{
public:
    static constexpr auto kPluginId = "builtin:enhance";
    enum Setting : uint32_t { Amount, Mode };
    [[nodiscard]] static PluginInfo info();
    static std::shared_ptr<EnhanceEffectNode> create(double sampleRate, int maxBlock);
    EnhanceEffectNode();

    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override;
    [[nodiscard]] QString bundlePath() const override { return QString::fromLatin1(kPluginId); }
    [[nodiscard]] QString name() const override { return QStringLiteral("Enhance"); }

protected:
    void prepareEffect(double sampleRate) override { m_enhance.prepare(sampleRate); }

private:
    sawstack::Enhance m_enhance;
};

// Sidechain: the sound ducked in time with the song (Kickstart's way): a
// curve every beat, or every note played.
class SidechainEffectNode final : public BuiltinEffectNode
{
public:
    static constexpr auto kPluginId = "builtin:sidechain";
    enum Setting : uint32_t { Depth, Rate, Curve, Slope, Trigger };
    [[nodiscard]] static PluginInfo info();
    static std::shared_ptr<SidechainEffectNode> create(double sampleRate, int maxBlock);
    SidechainEffectNode();

    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override;
    [[nodiscard]] bool wantsNotes() const noexcept override { return true; }
    [[nodiscard]] QString bundlePath() const override { return QString::fromLatin1(kPluginId); }
    [[nodiscard]] QString name() const override { return QStringLiteral("Sidechain"); }

protected:
    void prepareEffect(double /*sampleRate*/) override {}
    // kReadings: where it is in its cycle (0-1), for the window's playhead.
    [[nodiscard]] double reading(uint32_t id) const override;

private:
    sawstack::Sidechain m_sidechain;
    std::atomic<float> m_position{0.0F};
};

} // namespace gigchain::engine
