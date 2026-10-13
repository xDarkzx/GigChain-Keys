#pragma once

#include <cmath>
#include <span>

namespace gigchain::engine {

// How fast a gain glides to a new setting (a one-pole's time constant): a
// fader, mute or solo moved while the sound plays never steps it (a click),
// and a mute is silent within about 17 ms (-60 dB).
inline constexpr double kGlideSeconds = 0.0025;

// The share of the way a gain moves to its target per sample at `sampleRate`.
[[nodiscard]] inline float glideStep(double sampleRate) noexcept
{
    return sampleRate > 0.0 ? static_cast<float>(1.0 - std::exp(-1.0 / (kGlideSeconds * sampleRate))) : 1.0F;
}

// One sample of a gain gliding from `now` to `target` (`step`: glideStep).
// It lands on the target once within a hair, or once a step no longer moves
// it (a float's precision near 1: it would stop short of the target for
// good; the last step is far below hearing).
[[nodiscard]] inline float glideTo(float now, float target, float step) noexcept
{
    const float next = now + ((target - now) * step);
    return next == now || std::abs(target - next) < 1.0e-6F ? target : next;
}

// A gain on some sound (the master, a backing track) that glides to where it
// is set instead of jumping. Audio thread only.
class GlidingGain
{
public:
    // Whether it is silent and stays so (nothing to play, nothing gliding).
    [[nodiscard]] bool silent(float target) const noexcept { return target <= 0.0F && m_now <= 0.0F; }

    // `left` and `right` (the same length) times the gain, gliding to `target`.
    void scale(std::span<float> left, std::span<float> right, float target, double sampleRate) noexcept
    {
        float now = start(target);
        const float step = glideStep(sampleRate);
        auto r = right.begin();
        for (float& l : left) {
            now = glideTo(now, target, step);
            l *= now;
            *r++ *= now;
        }
        m_now = now;
    }

    // `from` (left and right, as long as `toLeft` and `toRight`) added to
    // `to` at the gain, gliding to `target`.
    void addInto(std::span<float> toLeft, std::span<float> toRight, std::span<const float> fromLeft, std::span<const float> fromRight,
                 float target, double sampleRate) noexcept
    {
        float now = start(target);
        const float step = glideStep(sampleRate);
        auto tr = toRight.begin();
        auto fl = fromLeft.begin();
        auto fr = fromRight.begin();
        for (float& tl : toLeft) {
            now = glideTo(now, target, step);
            tl += *fl++ * now;
            *tr++ += *fr++ * now;
        }
        m_now = now;
    }

private:
    // Where it is (its first use: where it is set).
    [[nodiscard]] float start(float target) const noexcept { return m_now < 0.0F ? target : m_now; }

    float m_now = -1.0F; // below 0: not used yet
};

} // namespace gigchain::engine
