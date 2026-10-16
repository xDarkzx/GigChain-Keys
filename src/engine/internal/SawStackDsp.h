#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>

// The building blocks of Saw Stack Pro, the app's own saw-stack synth: all of
// it written here, from published research and textbook methods (no other
// synth's code). Audio thread: nothing allocates, locks or logs.
namespace gigchain::engine::sawstack {

inline constexpr int kMaxUnison = 16;

// The classic supersaw's detune, as Adam Szabo measured it on the original
// hardware ("How to Emulate the Super Saw", KTH, 2010): the knob (0-1) barely
// spreads the saws over its first half, then fast; 1 at the top.
[[nodiscard]] inline double detuneCurve(double knob)
{
    const double x = std::clamp(knob, 0.0, 1.0);
    static constexpr std::array kCoefficients{
        10028.7312891634, -50818.8652045924, 111363.4808729368, -138150.6761080548,
        106649.6679158292, -53046.9642751875, 17019.9518580080, -3425.0836591318,
        404.2703938388,   -24.1878824391,    0.6717417634,      0.0030115596,
    };
    // Horner's rule, x^11 first.
    const double y = std::accumulate(kCoefficients.begin(), kCoefficients.end(), 0.0, [x](double sum, double c) { return (sum * x) + c; });
    return std::clamp(y, 0.0, 1.0);
}

// The same measurements: the middle saw's level and the side saws' level as
// the mix knob (0-1) moves (the side saws come up as the middle one goes down).
[[nodiscard]] inline double centerGain(double mix) { return -0.55366 * std::clamp(mix, 0.0, 1.0) + 0.99785; }
[[nodiscard]] inline double sideGain(double mix)
{
    const double x = std::clamp(mix, 0.0, 1.0);
    return -0.73764 * x * x + 1.2841 * x + 0.044372;
}

// Where each of the seven measured saws sits at full detune, as a fraction of
// the note's frequency (they are not evenly spaced, which is part of the sound).
inline constexpr std::array<double, 7> kMeasuredOffsets{-0.11002313, -0.06288439, -0.01952356, 0.0,
                                                         0.01991221,  0.06216538,  0.10745242};

// A saw's offset at position `p` (-1 lowest .. 1 highest), between the
// measured ones: any number of saws keeps the measured shape.
[[nodiscard]] inline double offsetAt(double p)
{
    const double at = (std::clamp(p, -1.0, 1.0) + 1.0) * 3.0; // 0..6
    const int below = std::min(static_cast<int>(at), 5);
    const double t = at - below;
    return kMeasuredOffsets[below] + (kMeasuredOffsets[below + 1] - kMeasuredOffsets[below]) * t;
}

// How a layer's saws are spread: each one's frequency offset and its level
// in each ear. The levels are scaled so a layer is as loud with 1 saw as with 16.
struct UnisonSpread
{
    int voices = 1;
    std::array<float, kMaxUnison> offset{};
    std::array<float, kMaxUnison> left{};
    std::array<float, kMaxUnison> right{};

    void set(int count, double detune, double mix, double width)
    {
        voices = std::clamp(count, 1, kMaxUnison);
        const double amount = detuneCurve(detune);
        const double center = centerGain(mix);
        const double side = sideGain(mix);
        std::array<double, kMaxUnison> weight{};
        double power = 0.0;
        for (int i = 0; i < voices; ++i) {
            const double p = voices == 1 ? 0.0 : -1.0 + 2.0 * i / (voices - 1);
            offset[i] = static_cast<float>(offsetAt(p) * amount);
            // The middle saw (two when the count is even) takes the center level.
            const bool middle = voices == 1 || std::abs(p) * (voices - 1) <= 1.0 + 1e-9;
            weight[i] = middle ? center : side;
            power += weight[i] * weight[i];
        }
        const double norm = power > 0.0 ? 1.0 / std::sqrt(power) : 0.0;
        const double spread = std::clamp(width, 0.0, 1.0);
        for (int i = 0; i < voices; ++i) {
            const double p = voices == 1 ? 0.0 : -1.0 + 2.0 * i / (voices - 1);
            // Neighbours go to opposite sides, so high and low saws are in both ears.
            const double pan = spread * std::abs(p) * ((i % 2) != 0 ? 1.0 : -1.0); // -1 left .. 1 right
            const double angle = (pan + 1.0) * std::numbers::pi / 4.0;              // equal power
            left[i] = static_cast<float>(weight[i] * norm * std::cos(angle) * std::numbers::sqrt2);
            right[i] = static_cast<float>(weight[i] * norm * std::sin(angle) * std::numbers::sqrt2);
        }
    }
};

// The correction that takes the harsh steps out of a saw or square (PolyBLEP,
// Välimäki & Huovilainen): `t` is the phase (0-1), `dt` the phase step.
[[nodiscard]] inline float polyBlep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0F;
    }
    if (t > 1.0F - dt) {
        t = (t - 1.0F) / dt;
        return t * t + t + t + 1.0F;
    }
    return 0.0F;
}

enum class Wave : uint8_t
{
    Saw,
    Square,
    Sine,
};

[[nodiscard]] inline float oscillator(Wave wave, float phase, float dt)
{
    switch (wave) {
    case Wave::Saw: return 2.0F * phase - 1.0F - polyBlep(phase, dt);
    case Wave::Square: {
        float shifted = phase + 0.5F;
        if (shifted >= 1.0F) shifted -= 1.0F;
        return (phase < 0.5F ? 1.0F : -1.0F) + polyBlep(phase, dt) - polyBlep(shifted, dt);
    }
    case Wave::Sine: return std::sin(2.0F * std::numbers::pi_v<float> * phase);
    }
    return 0.0F;
}

// A small fast random generator (xorshift32): noise and the saws' starting points.
struct Random
{
    uint32_t state = 0x9E3779B9U;
    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float unit() { return static_cast<float>(next() >> 8) * (1.0F / 16777216.0F); } // 0..1
    float bipolar() { return unit() * 2.0F - 1.0F; }                                 // -1..1
};

// The state-variable filter of Zavalishin's "The Art of VA Filter Design"
// (trapezoidal): stays stable while its cutoff moves.
struct Svf
{
    float g = 0.0F, k = 1.4142F, a1 = 1.0F, a2 = 0.0F, a3 = 0.0F;
    float ic1 = 0.0F, ic2 = 0.0F;

    void set(double cutoffHz, double q, double sampleRate)
    {
        const double fc = std::clamp(cutoffHz, 10.0, sampleRate * 0.49);
        g = static_cast<float>(std::tan(std::numbers::pi * fc / sampleRate));
        k = static_cast<float>(1.0 / std::max(q, 0.1));
        a1 = 1.0F / (1.0F + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0F; }
    // One sample in; the low-pass and high-pass outputs.
    void tick(float in, float& low, float& high)
    {
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0F * v1 - ic1;
        ic2 = 2.0F * v2 - ic2;
        low = v2;
        high = in - k * v1 - v2;
    }
    float lowPass(float in)
    {
        float low = 0.0F, high = 0.0F;
        tick(in, low, high);
        return low;
    }
    float highPass(float in)
    {
        float low = 0.0F, high = 0.0F;
        tick(in, low, high);
        return high;
    }
};

// Attack (straight up), then decay and release (curved, as ears hear them).
struct Envelope
{
    enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Stage::Idle;
    float level = 0.0F;
    float attackStep = 1.0F, decayCoef = 0.0F, releaseCoef = 0.0F, sustain = 1.0F;

    static float coefFor(double seconds, double sampleRate)
    {
        // Reaches 1/1000 (-60 dB) of the way left in `seconds`.
        return seconds <= 0.0 ? 1.0F : static_cast<float>(1.0 - std::exp(std::log(0.001) / (seconds * sampleRate)));
    }
    void set(double attack, double decay, double sustainLevel, double release, double sampleRate)
    {
        attackStep = attack <= 0.0 ? 1.0F : static_cast<float>(1.0 / (attack * sampleRate));
        decayCoef = coefFor(decay, sampleRate);
        releaseCoef = coefFor(release, sampleRate);
        sustain = static_cast<float>(std::clamp(sustainLevel, 0.0, 1.0));
    }
    // From wherever it is now: a retrigger never jumps (no click).
    void gateOn() { stage = Stage::Attack; }
    void gateOff()
    {
        if (stage != Stage::Idle) stage = Stage::Release;
    }
    void fastRelease(double sampleRate)
    {
        releaseCoef = coefFor(0.004, sampleRate);
        gateOff();
    }
    [[nodiscard]] bool active() const { return stage != Stage::Idle; }
    float tick()
    {
        switch (stage) {
        case Stage::Idle: return 0.0F;
        case Stage::Attack:
            level += attackStep;
            if (level >= 1.0F) {
                level = 1.0F;
                stage = Stage::Decay;
            }
            break;
        case Stage::Decay:
            level += (sustain - level) * decayCoef;
            if (std::abs(level - sustain) < 1e-4F) {
                level = sustain;
                stage = Stage::Sustain;
            }
            break;
        case Stage::Sustain: level = sustain; break;
        case Stage::Release:
            level -= level * releaseCoef;
            if (level < 1e-4F) { // -80 dB: silent
                level = 0.0F;
                stage = Stage::Idle;
            }
            break;
        }
        return level;
    }
};

} // namespace gigchain::engine::sawstack
