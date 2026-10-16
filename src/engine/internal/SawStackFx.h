#pragma once

#include "SawStackDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// The effects behind Saw Stack Pro's sound, which each stack type sets and
// players never have to: drive, glue and chorus. Our own code, from textbook
// methods. Audio thread: nothing allocates, locks or logs (prepare() does).
namespace gigchain::engine::sawstack {

// Soft saturation: warmth at low drive, edge at high. 0 dB is off.
struct Drive
{
    float k = 0.0F;
    void set(double driveDb) { k = driveDb <= 0.0 ? 0.0F : static_cast<float>(0.5 * std::pow(10.0, driveDb / 20.0)); }
    [[nodiscard]] float tick(float x) const { return k > 0.0F ? std::tanh(k * x) / k : x; }
};

// The squash that makes a stack sound finished: the sound split in three
// (lows, mids, highs), each band's loud parts pulled down and its quiet
// detail pulled up, then put back together. `depth` 0 leaves it untouched.
class Glue
{
public:
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        for (auto& f : m_split) f.set(kLowSplitHz, 0.5, sampleRate);
        for (auto& f : m_split2) f.set(kHighSplitHz, 0.5, sampleRate);
        const auto coef = [&](double seconds) { return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate))); };
        m_attack = coef(0.003);
        m_release = coef(0.08);
        m_smooth = coef(0.005);
    }
    void setDepth(double depth) { m_depth = static_cast<float>(std::clamp(depth, 0.0, 1.0)); }

    void tick(float& left, float& right)
    {
        if (m_depth <= 0.0F) return;
        // The bands always add back up to the input exactly.
        std::array<float, 2> in{left, right};
        std::array<std::array<float, 2>, kBands> band{};
        for (int c = 0; c < 2; ++c) {
            band[0][c] = m_split[c].lowPass(in[c]);
            const float rest = in[c] - band[0][c];
            band[1][c] = m_split2[c].lowPass(rest);
            band[2][c] = rest - band[1][c];
        }
        left = right = 0.0F;
        for (int b = 0; b < kBands; ++b) {
            const float level = std::max(std::abs(band[b][0]), std::abs(band[b][1]));
            float& env = m_env[b];
            env += (level - env) * (level > env ? m_attack : m_release);
            if (--m_countdown[b] <= 0) { // the gain is worked out every few samples
                m_countdown[b] = 16;
                m_target[b] = gainFor(env);
            }
            m_gain[b] += (m_target[b] - m_gain[b]) * m_smooth;
            left += band[b][0] * m_gain[b];
            right += band[b][1] * m_gain[b];
        }
    }

    static constexpr double kLowSplitHz = 120.0;
    static constexpr double kHighSplitHz = 2500.0;

private:
    static constexpr int kBands = 3;

    [[nodiscard]] float gainFor(float env) const
    {
        const double db = 20.0 * std::log10(std::max(double(env), 1e-9));
        double change = 0.0;
        if (db > kDownDb) change = (kDownDb - db) * (1.0 - 1.0 / kDownRatio); // loud: down
        else if (db < kUpDb && db > kGateDb) change = std::min((kUpDb - db) * (1.0 - 1.0 / kUpRatio), kMaxUpDb); // quiet: up
        return static_cast<float>(std::pow(10.0, m_depth * change / 20.0));
    }

    static constexpr double kDownDb = -18.0, kDownRatio = 3.0;
    static constexpr double kUpDb = -36.0, kUpRatio = 2.5, kMaxUpDb = 12.0;
    static constexpr double kGateDb = -66.0; // silence is left silent (no hiss pulled up)

    double m_rate = 48000.0;
    std::array<Svf, 2> m_split{}, m_split2{};
    std::array<float, kBands> m_env{}, m_target{1.0F, 1.0F, 1.0F}, m_gain{1.0F, 1.0F, 1.0F};
    std::array<int, kBands> m_countdown{};
    float m_attack = 1.0F, m_release = 1.0F, m_smooth = 1.0F, m_depth = 0.0F;
};

// A wide doubling: two slowly moving short delays, one per side.
class Chorus
{
public:
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        m_size = 1;
        while (m_size < static_cast<int>(sampleRate * 0.04)) m_size <<= 1;
        m_left.assign(static_cast<std::size_t>(m_size), 0.0F);
        m_right.assign(static_cast<std::size_t>(m_size), 0.0F);
        m_write = 0;
    }
    void setMix(double mix) { m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0)); }

    void tick(float& left, float& right)
    {
        if (m_mix <= 0.0F || m_size <= 1) return;
        const int mask = m_size - 1;
        m_left[static_cast<std::size_t>(m_write)] = left;
        m_right[static_cast<std::size_t>(m_write)] = right;
        m_phase += 0.45 / m_rate;
        if (m_phase >= 1.0) m_phase -= 1.0;
        const double twoPi = 2.0 * std::numbers::pi;
        // Each side moves on its own (a quarter turn apart): width without a wobble in the middle.
        const double delayL = (0.012 + 0.003 * std::sin(twoPi * m_phase)) * m_rate;
        const double delayR = (0.014 + 0.003 * std::cos(twoPi * m_phase)) * m_rate;
        const auto read = [&](const std::vector<float>& line, double delay) {
            const double at = m_write - delay;
            const double floor = std::floor(at);
            const float t = static_cast<float>(at - floor);
            const int i = static_cast<int>(floor) & mask;
            const float a = line[static_cast<std::size_t>(i)];
            const float b = line[static_cast<std::size_t>((i + 1) & mask)];
            return a + (b - a) * t;
        };
        // Each ear gets the other side's doubling: the classic wide stereo trick.
        const float wetL = read(m_right, delayL);
        const float wetR = read(m_left, delayR);
        m_write = (m_write + 1) & mask;
        const float dry = 1.0F - 0.3F * m_mix;
        left = left * dry + wetL * 0.6F * m_mix;
        right = right * dry + wetR * 0.6F * m_mix;
    }

private:
    double m_rate = 48000.0;
    std::vector<float> m_left, m_right;
    int m_size = 1;
    int m_write = 0;
    double m_phase = 0.0;
    float m_mix = 0.0F;
};

} // namespace gigchain::engine::sawstack
