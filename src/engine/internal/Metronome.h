#pragma once

#include "INode.h"

#include <atomic>
#include <cmath>
#include <numbers>
#include <span>

namespace gigchain::engine {

// The click: a short tone on every beat, higher on the first beat of each
// bar, following the engine's clock (TimeInfo). Added to the output.
// setOn/setVolumeDb from the main thread; process() on the audio thread
// (real-time safe).
class Metronome
{
public:
    void setOn(bool on) { m_on.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool isOn() const { return m_on.load(std::memory_order_relaxed); }
    void setVolumeDb(double volumeDb)
    {
        m_gain.store(std::isfinite(volumeDb) ? static_cast<float>(std::pow(10.0, std::min(volumeDb, 0.0) / 20.0)) : 0.0F,
                     std::memory_order_relaxed);
    }

    // Audio thread: adds the clicks falling in this block. `time` is where
    // the block starts.
    void process(const AudioBlock& out, const TimeInfo& time) noexcept
    {
        if (out.frames <= 0 || time.sampleRate <= 0.0 || time.tempo <= 0.0) return;
        const bool on = m_on.load(std::memory_order_relaxed);
        const float gain = m_gain.load(std::memory_order_relaxed);
        const double quartersPerSample = time.tempo / 60.0 / time.sampleRate;
        const double beatLength = 4.0 / time.timeSigDenominator; // in quarter notes
        const auto frames = static_cast<std::size_t>(out.frames);
        const std::span<float> left(out.left, frames);
        const std::span<float> right(out.right, frames);
        auto r = right.begin();
        std::size_t i = 0;
        for (float& l : left) {
            const double ppq = time.ppqPosition + (static_cast<double>(i) * quartersPerSample);
            const double beat = std::floor((ppq + 1e-9) / beatLength);
            if (beat != m_lastBeat) { // a beat starts at this sample
                m_lastBeat = beat;
                if (on) {
                    const double inBar = std::fmod(ppq - time.barStartPpq + 1e-9, time.quartersPerBar());
                    m_accent = inBar < beatLength * 0.5; // the bar's first beat
                    m_phase = 0.0;
                    m_remaining = static_cast<int>(kLengthSeconds * time.sampleRate);
                    m_length = m_remaining;
                }
            }
            if (m_remaining > 0) {
                const double frequency = m_accent ? kAccentHz : kBeatHz;
                const double envelope = static_cast<double>(m_remaining) / m_length; // linear fade out
                const auto sample = static_cast<float>(std::sin(m_phase) * envelope * envelope) * gain * kLevel;
                m_phase += 2.0 * std::numbers::pi * frequency / time.sampleRate;
                --m_remaining;
                l += sample;
                *r += sample;
            }
            ++r;
            ++i;
        }
    }

private:
    static constexpr double kLengthSeconds = 0.03;
    static constexpr double kAccentHz = 1500.0;
    static constexpr double kBeatHz = 1000.0;
    static constexpr float kLevel = 0.5F;

    std::atomic<bool> m_on{false};
    std::atomic<float> m_gain{1.0F};
    // Audio thread only.
    double m_lastBeat = -1.0;
    double m_phase = 0.0;
    int m_remaining = 0;
    int m_length = 1;
    bool m_accent = false;
};

} // namespace gigchain::engine
