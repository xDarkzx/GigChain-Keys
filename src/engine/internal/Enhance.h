#pragma once

#include "SawStackFx.h"

#include <algorithm>
#include <array>
#include <cmath>

// Enhance: one knob that makes a sound fuller and more finished, as FL
// Studio's Soundgoodizer does (a maximizer with four hand-made settings, A
// to D, over three bands). Its settings are ours, made from what those four
// are described to do:
//   A: the lows tighter and in mono, the mids a little wider, the highs
//      lifted most; light compression, saturation and limiting.
//   B: mostly saturation: the lows and highs driven hard (warmth and grit),
//      the lows narrower, little compression.
//   C: every band compressed, louder with the mids forward (keys, strings).
//   D: no bands: one compressor into a limiter (glue for any sound).
// The knob blends from the sound as it was to all of it; the result is kept
// at the level the sound came in at (fuller, not just louder). Off, the
// sound passes untouched. Audio thread: nothing allocates, locks or logs.
namespace gigchain::engine::sawstack {

namespace enhance {

inline constexpr std::size_t kBands = 3;

// A compressor's settings; `ratio` 1 is off.
struct CompDesign
{
    float thresholdDb = 0.0F, ratio = 1.0F, attackMs = 5.0F, releaseMs = 100.0F;
};
struct BandDesign
{
    float gainDb = 0.0F;
    float width = 1.0F; // the sides: 0 mono, 1 as they were, more is wider
    float drive = 0.0F; // saturation, 0-1
    CompDesign comp{};
    bool limit = false;
};
struct Design
{
    bool split = true;
    std::array<BandDesign, kBands> bands{};
    CompDesign master{};
    float masterDrive = 0.0F;
};
// A band's peaks held under -9 dBFS (fast, hard).
inline constexpr CompDesign kBandLimit{.thresholdDb = -9.0F, .ratio = 20.0F, .attackMs = 0.5F, .releaseMs = 50.0F};
// The end: nothing past -1 dBFS.
inline constexpr CompDesign kLimit{.thresholdDb = -1.0F, .ratio = 20.0F, .attackMs = 0.3F, .releaseMs = 80.0F};
// The four settings, A to D (lows, mids, highs; then the whole).
inline constexpr std::array<Design, 4> kDesigns{{
    {.split = true,
     .bands = {{{.gainDb = -2.0F, .width = 0.0F, .drive = 0.25F, .comp = {-20.0F, 3.0F, 10.0F, 120.0F}, .limit = true},
                {.gainDb = 1.0F, .width = 1.15F, .drive = 0.1F, .comp = {-22.0F, 2.0F, 5.0F, 100.0F}, .limit = false},
                {.gainDb = 4.0F, .width = 1.0F, .drive = 0.25F, .comp = {-24.0F, 3.0F, 2.0F, 60.0F}, .limit = true}}},
     .master = {-14.0F, 2.0F, 5.0F, 150.0F},
     .masterDrive = 0.15F},
    {.split = true,
     .bands = {{{.gainDb = 0.0F, .width = 0.5F, .drive = 0.7F, .comp = {}, .limit = false},
                {.gainDb = 0.0F, .width = 1.15F, .drive = 0.1F, .comp = {}, .limit = false},
                {.gainDb = 0.0F, .width = 1.0F, .drive = 0.6F, .comp = {}, .limit = false}}},
     .master = {-12.0F, 1.6F, 10.0F, 200.0F},
     .masterDrive = 0.25F},
    {.split = true,
     .bands = {{{.gainDb = 1.0F, .width = 0.3F, .drive = 0.15F, .comp = {-22.0F, 3.0F, 10.0F, 120.0F}, .limit = true},
                {.gainDb = 3.0F, .width = 1.0F, .drive = 0.1F, .comp = {-22.0F, 3.0F, 5.0F, 100.0F}, .limit = false},
                {.gainDb = 2.0F, .width = 1.0F, .drive = 0.15F, .comp = {-24.0F, 3.0F, 2.0F, 60.0F}, .limit = true}}},
     .master = {-14.0F, 2.0F, 5.0F, 150.0F},
     .masterDrive = 0.1F},
    {.split = false, .bands = {}, .master = {-20.0F, 3.0F, 10.0F, 120.0F}, .masterDrive = 0.0F},
}};

} // namespace enhance

class Enhance
{
public:
    static constexpr int kModes = static_cast<int>(enhance::kDesigns.size());
    static constexpr std::array<const char*, kModes> kModeNames{"A", "B", "C", "D"};

    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        for (Channel& c : m_channels) {
            c.lowSplit.set(kLowSplitHz, sampleRate);
            c.highSplit.set(kHighSplitHz, sampleRate);
        }
        m_fade = static_cast<float>(1.0 - std::exp(-1.0 / (0.02 * sampleRate)));     // on and off over ~20 ms
        m_follow = static_cast<float>(1.0 - std::exp(-1.0 / (0.5 * sampleRate)));    // the level, over ~half a second
        setMode(m_mode);
    }
    // `amount` 0-1 (0: off, the sound untouched); `mode` 0-3 (A-D).
    void set(double amount, int mode)
    {
        m_amount = static_cast<float>(std::clamp(amount, 0.0, 1.0));
        if (std::clamp(mode, 0, kModes - 1) != m_mode) setMode(mode);
    }

    void tick(float& left, float& right)
    {
        const float target = m_amount > 0.0F ? 1.0F : 0.0F;
        if (target == 0.0F && m_presence <= 1e-4F) {
            m_presence = 0.0F;
            return;
        }
        m_presence += (target - m_presence) * m_fade;
        if (m_amount > 0.0F) m_mix = m_amount; // (fading out: as it was)
        const float inL = left;
        const float inR = right;

        float dryL = inL;
        float dryR = inR;
        float wetL = inL;
        float wetR = inR;
        if (m_design->split) {
            std::array<float, kBands> bandL{};
            std::array<float, kBands> bandR{};
            split(m_channels.at(0), inL, bandL);
            split(m_channels.at(1), inR, bandR);
            // The bands added back up untouched: the dry the knob blends from
            // (the same phase as the processed bands, so the blend never thins).
            dryL = bandL.at(0) + bandL.at(1) + bandL.at(2);
            dryR = bandR.at(0) + bandR.at(1) + bandR.at(2);
            wetL = 0.0F;
            wetR = 0.0F;
            for (std::size_t b = 0; b < kBands; ++b) {
                const BandDesign& d = m_design->bands.at(b);
                float l = bandL.at(b) * m_bandGain.at(b);
                float r = bandR.at(b) * m_bandGain.at(b);
                // Width: the sides of this band, from none (mono) to wider.
                const float mid = 0.5F * (l + r);
                const float side = 0.5F * (l - r) * d.width;
                l = mid + side;
                r = mid - side;
                saturate(l, r, d.drive);
                m_bandComp.at(b).tick(l, r);
                if (d.limit) m_bandLimit.at(b).tick(l, r);
                wetL += l;
                wetR += r;
            }
        }
        m_masterComp.tick(wetL, wetR);
        saturate(wetL, wetR, m_design->masterDrive);
        m_limiter.tick(wetL, wetR);

        float l = dryL + ((wetL - dryL) * m_mix);
        float r = dryR + ((wetR - dryR) * m_mix);
        // Kept at the level it came in at (followed slowly; silence holds it).
        m_inPower += ((inL * inL) + (inR * inR) - m_inPower) * m_follow;
        m_outPower += ((l * l) + (r * r) - m_outPower) * m_follow;
        if (m_inPower > kQuiet && m_outPower > kQuiet) {
            m_match = std::clamp(std::sqrt(m_inPower / m_outPower), kLeastMatch, kMostMatch);
        }
        l *= m_match;
        r *= m_match;
        left = inL + ((l - inL) * m_presence);
        right = inR + ((r - inR) * m_presence);
    }

private:
    using CompDesign = enhance::CompDesign;
    using BandDesign = enhance::BandDesign;
    using Design = enhance::Design;
    static constexpr std::size_t kBands = enhance::kBands;
    static constexpr double kLowSplitHz = 150.0;
    static constexpr double kHighSplitHz = 2500.0;
    static constexpr float kQuiet = 1e-9F;      // -90 dB: too quiet to match the level by
    static constexpr float kLeastMatch = 0.25F; // -12 dB
    static constexpr float kMostMatch = 2.0F;   // +6 dB

    // A compressor on both sides at once (linked, so the image never moves),
    // following the peaks in dB, with a soft knee.
    class Compressor
    {
    public:
        void set(const CompDesign& d, double rate)
        {
            m_d = d;
            m_attack = static_cast<float>(std::exp(-1.0 / (std::max(d.attackMs, 0.05F) * 0.001 * rate)));
            m_release = static_cast<float>(std::exp(-1.0 / (std::max(d.releaseMs, 1.0F) * 0.001 * rate)));
        }
        void tick(float& left, float& right)
        {
            if (m_d.ratio <= 1.0F) return;
            const float peak = std::max(std::abs(left), std::abs(right));
            const float db = 20.0F * std::log10(std::max(peak, 1e-6F));
            const float coef = db > m_env ? m_attack : m_release;
            m_env = (coef * m_env) + ((1.0F - coef) * db);
            const float over = m_env - m_d.thresholdDb;
            constexpr float kKnee = 6.0F;
            float reduce = 0.0F;
            const float slope = 1.0F - (1.0F / m_d.ratio);
            if (over >= kKnee / 2.0F) {
                reduce = over * slope;
            } else if (over > -kKnee / 2.0F) {
                const float into = over + (kKnee / 2.0F);
                reduce = slope * into * into / (2.0F * kKnee);
            }
            if (reduce <= 0.0F) return;
            const float gain = std::pow(10.0F, -reduce / 20.0F);
            left *= gain;
            right *= gain;
        }

    private:
        CompDesign m_d{};
        float m_attack = 0.0F, m_release = 0.0F;
        float m_env = -120.0F;
    };

    struct Channel
    {
        Crossover lowSplit, highSplit;
    };

    static void split(Channel& c, float in, std::array<float, kBands>& bands)
    {
        float rest = 0.0F;
        c.lowSplit.split(in, bands.at(0), rest);
        c.highSplit.split(rest, bands.at(1), bands.at(2));
    }
    // Soft saturation that leaves quiet sound as it is and rounds the loud
    // (more drive: rounded from lower down, more harmonics).
    static void saturate(float& left, float& right, float drive)
    {
        if (drive <= 0.0F) return;
        const float d = 1.0F + (9.0F * drive);
        left = std::tanh(left * d) / d;
        right = std::tanh(right * d) / d;
    }
    void setMode(int mode)
    {
        m_mode = std::clamp(mode, 0, kModes - 1);
        m_design = &enhance::kDesigns.at(static_cast<std::size_t>(m_mode));
        for (std::size_t b = 0; b < kBands; ++b) {
            const BandDesign& d = m_design->bands.at(b);
            m_bandGain.at(b) = std::pow(10.0F, d.gainDb / 20.0F);
            m_bandComp.at(b).set(d.comp, m_rate);
            m_bandLimit.at(b).set(enhance::kBandLimit, m_rate);
        }
        m_masterComp.set(m_design->master, m_rate);
        m_limiter.set(enhance::kLimit, m_rate);
    }

    double m_rate = 48000.0;
    int m_mode = 0;
    const Design* m_design = enhance::kDesigns.data();
    std::array<Channel, 2> m_channels{};
    std::array<float, kBands> m_bandGain{1.0F, 1.0F, 1.0F};
    std::array<Compressor, kBands> m_bandComp{}, m_bandLimit{};
    Compressor m_masterComp, m_limiter;
    float m_amount = 0.0F, m_mix = 0.0F, m_presence = 0.0F;
    float m_fade = 1.0F, m_follow = 1.0F;
    float m_inPower = 0.0F, m_outPower = 0.0F, m_match = 1.0F;
};

} // namespace gigchain::engine::sawstack
