#pragma once

#include "INode.h" // AudioBlock

#include <algorithm>
#include <atomic>
#include <cmath>

namespace gigchain::engine {

// The last thing before the output: nothing leaves louder than the ceiling,
// whatever a patch or plugin does, so the sound desk never gets a surprise.
// Instant attack (the gain drops on the very sample that would pass the
// ceiling, so the ceiling is never crossed), smooth release. Below the
// ceiling the sound passes untouched, sample for sample. A plugin that
// outputs garbage (NaN or infinity) is silenced, not passed on.
//
// Settings are atomics the main thread may change while audio runs;
// process() runs on the audio thread and never allocates, locks or logs.
class SafetyLimiter
{
public:
    // Main thread.
    void setEnabled(bool enabled) { m_enabled.store(enabled, std::memory_order_relaxed); }
    void setCeilingDb(double ceilingDb)
    {
        if (!std::isfinite(ceilingDb)) return;
        const double clamped = std::clamp(ceilingDb, kMinCeilingDb, 0.0);
        m_ceiling.store(static_cast<float>(std::pow(10.0, clamped / 20.0)), std::memory_order_relaxed);
    }
    // True once if the limiter caught a peak (or silenced garbage) since the last call.
    bool takeActivity() { return m_active.exchange(false, std::memory_order_relaxed); }

    // Audio thread.
    void setSampleRate(double sampleRate)
    {
        // Recovers smoothly after the loud part.
        m_release = sampleRate > 0.0 ? 1.0 - std::exp(-1.0 / (kReleaseSeconds * sampleRate)) : 1.0;
    }
    void process(AudioBlock io) noexcept
    {
        const bool enabled = m_enabled.load(std::memory_order_relaxed);
        const double ceiling = m_ceiling.load(std::memory_order_relaxed);
        bool limited = false;
        for (int i = 0; i < io.frames; ++i) {
            float& left = io.left[i];
            float& right = io.right[i];
            if (!std::isfinite(left) || !std::isfinite(right)) {
                left = right = 0.0F;
                limited = true;
                continue;
            }
            if (!enabled) continue;
            const double peak = std::max(std::abs(left), std::abs(right));
            if (peak * m_gain > ceiling) {
                m_gain = ceiling / peak; // instant: this sample lands on the ceiling
                limited = true;          // the LIM light: it caught something
            } else if (m_gain < 1.0) {
                m_gain += (1.0 - m_gain) * m_release;
                if (peak * m_gain > ceiling) {
                    m_gain = ceiling / peak;
                    limited = true;
                }
                if (m_gain > 0.9999) m_gain = 1.0; // within 0.001 dB: untouched again
            }
            if (m_gain < 1.0) { // catching, or recovering after it
                left = static_cast<float>(left * m_gain);
                right = static_cast<float>(right * m_gain);
            }
        }
        if (limited) m_active.store(true, std::memory_order_relaxed);
    }

    static constexpr double kMinCeilingDb = -24.0;
    static constexpr double kDefaultCeilingDb = -1.0;
    static constexpr double kReleaseSeconds = 0.1; // time constant: back to full within a second

private:
    std::atomic<bool> m_enabled{true};
    std::atomic<float> m_ceiling{0.891251F}; // -1 dB
    std::atomic<bool> m_active{false};
    // Audio thread only. Double: near full level each release step is
    // smaller than a float can add, and the gain would stall below 1.
    double m_gain = 1.0;
    double m_release = 1.0;
};

} // namespace gigchain::engine
