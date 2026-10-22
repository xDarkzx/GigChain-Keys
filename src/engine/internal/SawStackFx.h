#pragma once

#include "SawStackDsp.h"
#include "gigchain/engine/SidechainCurves.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

// The effects that make Saw Stack Pro a finished saw stack: drive, EQ,
// chorus, the 3-band "OTT" compressor, width, delay, reverb and the
// sidechain. The compressor, reverb and chorus are ported from Vital
// (Copyright 2013-2019 Matt Tytel, GPLv3, github.com/mtytel/vital), the
// open-source synth whose effects match the commercial ones: the same
// algorithms and default settings, rewritten for one stereo signal. The
// width, EQ, delay and sidechain are ours. Audio thread: nothing allocates,
// locks or logs (prepare() does).
namespace gigchain::engine::sawstack {

// Vital's one-pole low-pass (trapezoidal, its cutoff as Vital computes it).
struct OnePole
{
    float state = 0.0F;
    static float coefficient(double hz, double sampleRate)
    {
        const double phase = hz * std::numbers::pi / sampleRate;
        return static_cast<float>(std::tan(phase / (phase + 1.0)));
    }
    float tick(float in, float coef)
    {
        const float delta = coef * (in - state);
        state += delta;
        const float out = state;
        state += delta;
        return out;
    }
};

[[nodiscard]] inline double midiHz(double note)
{
    return 440.0 * std::exp2((note - 69.0) / 12.0);
}

// A Linkwitz-Riley crossover (two Butterworth sections each way): the low
// and high parts always add back up to the input's level, flat.
class Crossover
{
public:
    void set(double hz, double sampleRate)
    {
        for (Svf& f : m_low) f.set(hz, std::numbers::sqrt2 / 2.0, sampleRate);
        for (Svf& f : m_high) f.set(hz, std::numbers::sqrt2 / 2.0, sampleRate);
    }
    void split(float in, float& low, float& high)
    {
        low = m_low.at(1).lowPass(m_low.at(0).lowPass(in));
        high = m_high.at(1).highPass(m_high.at(0).highPass(in));
    }

private:
    std::array<Svf, 2> m_low{}, m_high{};
};

// Vital's multiband compressor, the "OTT" sound: lows, mids and highs each
// pulled down hard above one level and pulled up hard below another (up to
// x32), then given a lot of gain. `mix` is OTT's Depth (0 leaves it untouched).
class Ott
{
public:
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        for (Channel& c : m_channels) {
            c.lowSplit.set(120.0, sampleRate);
            c.highSplit.set(2500.0, sampleRate);
            c.bands = {};
        }
        // Vital's band timings (attack, release in ms) at its default 0.5.
        constexpr std::array<std::array<double, 2>, kBands> kTimes{{{2.8, 40.0}, {1.4, 28.0}, {0.7, 15.0}}};
        for (std::size_t b = 0; b < kBands; ++b) {
            const double perMs = sampleRate / 1000.0;
            const double attack = std::max(kTimes.at(b).at(0) * perMs, 5.0);
            const double release = std::max(kTimes.at(b).at(1) * perMs, 5.0);
            m_attack.at(b) = {.samples = static_cast<float>(attack), .scale = static_cast<float>(1.0 / (attack + 1.0))};
            m_release.at(b) = {.samples = static_cast<float>(release), .scale = static_cast<float>(1.0 / (release + 1.0))};
        }
    }
    void setMix(double mix) { m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0)); }

    void tick(float& left, float& right)
    {
        if (m_mix <= 0.0F) return;
        left = process(m_channels.at(0), left);
        right = process(m_channels.at(1), right);
    }

private:
    static constexpr std::size_t kBands = 3;
    struct Timing
    {
        float samples = 1.0F;
        float scale = 0.5F;
    };
    struct Band
    {
        float above = 0.0F; // the mean square the downward part follows
        float below = 0.0F; // the mean square the upward part follows
    };
    struct Channel
    {
        Crossover lowSplit, highSplit;
        std::array<Band, kBands> bands{};
    };
    // Vital's defaults: thresholds (dB), ratios, output gain (dB), per band.
    static constexpr std::array<float, kBands> kUpperDb{-28.0F, -25.0F, -30.0F};
    static constexpr std::array<float, kBands> kLowerDb{-35.0F, -36.0F, -35.0F};
    static constexpr std::array<float, kBands> kUpperRatio{0.9F, 0.857F, 1.0F};
    static constexpr std::array<float, kBands> kLowerRatio{0.8F, 0.8F, 0.8F};
    static constexpr std::array<float, kBands> kGainDb{16.3F, 11.7F, 16.3F};
    static constexpr float kMaxExpand = 32.0F;

    static float power(float db) { return std::pow(10.0F, db / 10.0F); } // dB to mean square

    float process(Channel& c, float in)
    {
        float low = 0.0F;
        float rest = 0.0F;
        c.lowSplit.split(in, low, rest);
        float mid = 0.0F;
        float high = 0.0F;
        c.highSplit.split(rest, mid, high);
        const std::array<float, kBands> parts{low, mid, high};
        float out = 0.0F;
        for (std::size_t b = 0; b < kBands; ++b) {
            const float x = parts.at(b);
            Band& band = c.bands.at(b);
            const float square = x * x;
            const float upper = power(kUpperDb.at(b));
            const float lower = power(kLowerDb.at(b));
            const Timing& above = square > band.above ? m_attack.at(b) : m_release.at(b);
            band.above = std::max((square + band.above * above.samples) * above.scale, upper);
            const Timing& below = square > band.below ? m_attack.at(b) : m_release.at(b);
            band.below = std::min((square + band.below * below.samples) * below.scale, lower);
            const float down = std::pow(upper / band.above, kUpperRatio.at(b) * 0.5F);
            const float up = std::pow(lower / std::max(band.below, 1e-30F), kLowerRatio.at(b) * 0.5F);
            const float gain = std::clamp(down * up, 0.0F, kMaxExpand) * std::pow(10.0F, kGainDb.at(b) / 20.0F);
            out += x * gain;
        }
        return in + (out - in) * m_mix;
    }

    double m_rate = 48000.0;
    std::array<Channel, 2> m_channels{};
    std::array<Timing, kBands> m_attack{}, m_release{};
    float m_mix = 0.0F;
};

// A delay line read between samples (cubic), for the chorus and reverb.
class DelayLine
{
public:
    void allocate(std::size_t size)
    {
        std::size_t n = 4;
        while (n < size + 4) n <<= 1U;
        m_buffer.assign(n, 0.0F);
        m_mask = n - 1;
        m_write = 0;
    }
    void clear() { std::ranges::fill(m_buffer, 0.0F); }
    void push(float x)
    {
        m_buffer.at(m_write) = x;
        m_write = (m_write + 1) & m_mask;
    }
    // `delay` samples ago (at least 1).
    [[nodiscard]] float read(float delay) const
    {
        const float d = std::clamp(delay, 1.0F, static_cast<float>(m_mask - 3));
        const float position = static_cast<float>(m_write) - d;
        const float whole = std::floor(position);
        const float t = position - whole;
        const auto i = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(whole) + static_cast<std::ptrdiff_t>(m_buffer.size()));
        const float y0 = m_buffer.at((i - 1) & m_mask);
        const float y1 = m_buffer.at(i & m_mask);
        const float y2 = m_buffer.at((i + 1) & m_mask);
        const float y3 = m_buffer.at((i + 2) & m_mask);
        // Catmull-Rom.
        return y1 + 0.5F * t * (y2 - y0 + t * (2.0F * y0 - 5.0F * y1 + 4.0F * y2 - y3 + t * (3.0F * (y1 - y2) + y3 - y0)));
    }
    [[nodiscard]] float at(std::size_t delay) const { return m_buffer.at((m_write - delay) & m_mask); }

private:
    std::vector<float> m_buffer;
    std::size_t m_mask = 0;
    std::size_t m_write = 0;
};

// Vital's reverb: sixteen delay lines that feed each other through allpass
// diffusers, filtered as they decay, their lengths slowly moving (a chorus
// inside the tail: what makes it lush instead of metallic).
class Reverb
{
public:
    struct Settings
    {
        double decaySeconds = 3.0;
        double size = 0.7;        // 0-1
        double preDelay = 0.02;   // seconds
        double lowCutHz = 150.0;  // what goes in
        double highCutHz = 4700.0;
        double dampHz = 1480.0;   // above it the tail fades faster
        double dampDb = -1.0;
        double chorus = 0.22;     // 0-1
        double chorusHz = 0.25;
    };

    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        m_ratio = sampleRate / 44100.0;
        int scale = 1;
        while (scale < m_ratio) scale *= 2;
        for (std::size_t n = 0; n < kLines; ++n) {
            m_feedback.at(n).allocate(static_cast<std::size_t>(scale) * (1U << 15U));
            m_allpass.at(n).allocate(static_cast<std::size_t>(scale) * 1024U);
            m_allpassDelay.at(n) = kAllpassDelays.at(n) * static_cast<std::size_t>(scale);
        }
        m_preDelay.allocate(static_cast<std::size_t>(sampleRate * 0.3) + 8);
        m_preDelayR.allocate(static_cast<std::size_t>(sampleRate * 0.3) + 8);
        set(m_settings);
    }
    void set(const Settings& s)
    {
        m_settings = s;
        const double sizeMult = std::exp2(std::clamp(s.size, 0.0, 1.0) * 4.0 - 3.0);
        const double decaySamples = std::clamp(s.decaySeconds, 0.1, 100.0) * 44100.0;
        for (std::size_t n = 0; n < kLines; ++n) {
            m_length.at(n) = static_cast<float>(sizeMult * kFeedbackDelays.at(n) * m_ratio);
            m_decay.at(n) = static_cast<float>(std::pow(0.001, kFeedbackDelays.at(n) * sizeMult / decaySamples));
        }
        const float shortest = *std::ranges::min_element(m_length);
        m_chorusDepth = std::min(static_cast<float>(std::clamp(s.chorus, 0.0, 1.0) * 2500.0 * m_ratio * sizeMult), shortest - 32.0F);
        m_chorusStep = s.chorusHz / m_rate;
        m_inLow = OnePole::coefficient(s.lowCutHz, m_rate);
        m_inHigh = OnePole::coefficient(s.highCutHz, m_rate);
        m_damp = OnePole::coefficient(s.dampHz, m_rate);
        m_dampAmount = static_cast<float>(std::pow(10.0, std::min(s.dampDb, 0.0) / 20.0));
        m_preDelaySamples = static_cast<float>(std::clamp(s.preDelay, 0.0, 0.29) * m_rate) + 1.0F;
    }
    void setMix(double mix) { m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0)); }

    void tick(float& left, float& right)
    {
        if (m_mix <= 0.0F) return;
        // The input, band-limited: [L, R, L, R] into the four lanes.
        const float l = m_inHighL.tick(left, m_inHigh) - m_inLowL.tick(left, m_inLow);
        const float r = m_inHighR.tick(right, m_inHigh) - m_inLowR.tick(right, m_inLow);
        const std::array<float, kLanes> lane{l * 0.25F, r * 0.25F, l * 0.25F, r * 0.25F};

        m_chorusPhase += m_chorusStep;
        if (m_chorusPhase >= 1.0) m_chorusPhase -= 1.0;
        std::array<float, kLines> diffused{};
        for (std::size_t n = 0; n < kLines; ++n) {
            const std::size_t group = n / kLanes;
            const std::size_t k = n % kLanes;
            const double phase = 2.0 * std::numbers::pi * (m_chorusPhase + static_cast<double>(k) / kLines);
            const float wave = static_cast<float>(group < 2 ? std::cos(phase) : std::sin(phase));
            const float sign = (group % 2 == 0) ? 1.0F : -1.0F;
            const float fed = m_feedback.at(n).read(m_length.at(n) + sign * wave * m_chorusDepth);
            const float back = m_allpass.at(n).at(m_allpassDelay.at(n));
            const float into = fed - back * kAllpassFeedback;
            m_allpass.at(n).push(lane.at(k) + into);
            diffused.at(n) = back + into * kAllpassFeedback;
        }
        const std::array<float, kLines> mixed = mixMatrix(diffused);
        std::array<float, kLines> stored{};
        for (std::size_t n = 0; n < kLines; ++n) {
            float w = mixed.at(n);
            const float damped = m_dampFilter.at(n).tick(w, m_damp);
            w = damped + m_dampAmount * (w - damped);
            stored.at(n) = w * m_decay.at(n);
            m_feedback.at(n).push(stored.at(n));
        }
        const std::array<float, kLines> forward = mixMatrix(stored);
        std::array<float, kLanes> total{};
        for (std::size_t n = 0; n < kLines; ++n) {
            total.at(n % kLanes) += mixed.at(n) + 0.125F * forward.at(n) * m_decay.at(n);
        }
        m_preDelay.push(total.at(0) + total.at(2));
        m_preDelayR.push(total.at(1) + total.at(3));
        const float wetL = m_preDelay.read(m_preDelaySamples);
        const float wetR = m_preDelayR.read(m_preDelaySamples);
        // Equal power, as Vital mixes it.
        const float wet = std::sin(m_mix * std::numbers::pi_v<float> * 0.5F);
        const float dry = std::cos(m_mix * std::numbers::pi_v<float> * 0.5F);
        left = left * dry + wetL * wet;
        right = right * dry + wetR * wet;
    }

private:
    static constexpr std::size_t kLines = 16;
    static constexpr std::size_t kLanes = 4;
    static constexpr float kAllpassFeedback = 0.6F;
    // Vital's line lengths (samples at 44.1 kHz), four groups of four.
    static constexpr std::array<std::size_t, kLines> kAllpassDelays{1001, 799, 933, 876, 895, 807, 907, 853,
                                                                    957, 1019, 711, 567, 833, 779, 663, 997};
    static constexpr std::array<double, kLines> kFeedbackDelays{6753.2, 9278.4, 7704.5, 11328.5, 9701.12, 5512.5, 8480.45, 5638.65,
                                                                3120.73, 3429.5, 3626.37, 7713.52, 4521.54, 6518.97, 5265.56, 5630.25};

    // Vital's feedback matrix: each line gets a quarter of all, minus half
    // of its lane's and half of its group's.
    static std::array<float, kLines> mixMatrix(const std::array<float, kLines>& in)
    {
        float all = 0.0F;
        std::array<float, kLanes> lanes{};
        std::array<float, kLanes> groups{};
        for (std::size_t n = 0; n < kLines; ++n) {
            all += in.at(n);
            lanes.at(n % kLanes) += in.at(n);
            groups.at(n / kLanes) += in.at(n);
        }
        std::array<float, kLines> out{};
        for (std::size_t n = 0; n < kLines; ++n) {
            out.at(n) = in.at(n) + 0.25F * all - 0.5F * lanes.at(n % kLanes) - 0.5F * groups.at(n / kLanes);
        }
        return out;
    }

    double m_rate = 48000.0;
    double m_ratio = 1.0;
    Settings m_settings;
    std::array<DelayLine, kLines> m_feedback{}, m_allpass{};
    std::array<std::size_t, kLines> m_allpassDelay{};
    std::array<float, kLines> m_length{}, m_decay{};
    std::array<OnePole, kLines> m_dampFilter{};
    OnePole m_inLowL, m_inLowR, m_inHighL, m_inHighR;
    float m_inLow = 0.0F, m_inHigh = 1.0F, m_damp = 1.0F, m_dampAmount = 1.0F;
    float m_chorusDepth = 0.0F;
    double m_chorusPhase = 0.0, m_chorusStep = 0.0;
    DelayLine m_preDelay, m_preDelayR;
    float m_preDelaySamples = 1.0F;
    float m_mix = 0.0F;
};

// Vital's chorus: four pairs of short moving delays (left and right sweeping
// apart, a slower set behind them) fed back a little, for thickness.
class Chorus
{
public:
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        for (DelayLine& d : m_lines) d.allocate(static_cast<std::size_t>(0.08 * sampleRate) + 8);
        for (Svf& f : m_lowCut) f.set(150.0, 0.7071, sampleRate);
        for (Svf& f : m_highCut) f.set(8000.0, 0.7071, sampleRate);
    }
    void setMix(double mix) { m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0)); }

    void tick(float& left, float& right)
    {
        if (m_mix <= 0.0F) return;
        m_phase += kRateHz / m_rate;
        if (m_phase >= 1.0) m_phase -= 1.0;
        const std::array<float, kTaps> input{left, right, left, right};
        constexpr std::array<double, kTaps> kBase{0.00195, 0.00195, 0.0078, 0.0078}; // Vital's 2^-9 and 2^-7 s
        const double average = (kBase.at(0) + kBase.at(2)) / 2.0;
        float wetL = 0.0F;
        float wetR = 0.0F;
        for (std::size_t pair = 0; pair < kPairs; ++pair) {
            const double t = static_cast<double>(pair) / (kPairs - 1);
            for (std::size_t tap = 0; tap < kTaps; ++tap) {
                const std::size_t n = pair * kTaps + tap;
                const double offset = (tap % 2 == 1 ? 0.25 : 0.0) + (tap >= 2 ? 0.5 : 0.0) + 0.25 * static_cast<double>(pair) / kPairs;
                const double mod = std::sin((m_phase + offset) * 2.0 * std::numbers::pi) * 0.5 + 1.0;
                const double seconds = mod * kDepth + kBase.at(tap) + (average - kBase.at(tap)) * t;
                DelayLine& line = m_lines.at(n);
                const float out = line.read(static_cast<float>(seconds * m_rate));
                const float back = m_highCut.at(n).lowPass(m_lowCut.at(n).highPass(out)) * kFeedback;
                line.push(input.at(tap) + back);
                (tap % 2 == 0 ? wetL : wetR) += out * 0.5F;
            }
        }
        const float wet = std::sin(m_mix * std::numbers::pi_v<float> * 0.5F);
        const float dry = std::cos(m_mix * std::numbers::pi_v<float> * 0.5F);
        left = left * dry + wetL * wet * 0.5F;
        right = right * dry + wetR * wet * 0.5F;
    }

private:
    static constexpr std::size_t kPairs = 4;
    static constexpr std::size_t kTaps = 4;
    static constexpr double kRateHz = 0.25;
    static constexpr double kDepth = 0.5 * 0.03; // Vital's default depth of its 30 ms
    static constexpr float kFeedback = 0.4F;
    double m_rate = 48000.0;
    double m_phase = 0.0;
    std::array<DelayLine, kPairs * kTaps> m_lines{};
    std::array<Svf, kPairs * kTaps> m_lowCut{}, m_highCut{};
    float m_mix = 0.0F;
};

// Width (as Serum's Dimension and the Roland Dimension D): the middle left
// as it is, the sides fed from two short delayed copies in opposite
// polarity, so it opens out in stereo and cancels cleanly in mono.
class Dimension
{
public:
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        m_line.allocate(static_cast<std::size_t>(0.04 * sampleRate) + 8);
        m_lowCut.set(250.0, 0.7071, sampleRate); // the lows stay in the middle
    }
    void setAmount(double amount) { m_amount = static_cast<float>(std::clamp(amount, 0.0, 1.0)); }

    void tick(float& left, float& right)
    {
        const float mid = 0.5F * (left + right);
        m_line.push(m_lowCut.highPass(mid));
        if (m_amount <= 0.0F) return;
        const float side = m_line.read(static_cast<float>(0.011 * m_rate)) - m_line.read(static_cast<float>(0.017 * m_rate));
        left += side * m_amount * 0.7F;
        right -= side * m_amount * 0.7F;
    }

private:
    double m_rate = 48000.0;
    DelayLine m_line;
    Svf m_lowCut;
    float m_amount = 0.0F;
};

// A stereo ping-pong echo locked to the tempo, darker each time round.
class PingPong
{
public:
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        m_left.allocate(static_cast<std::size_t>(2.0 * sampleRate) + 8);
        m_right.allocate(static_cast<std::size_t>(2.0 * sampleRate) + 8);
        for (Svf& f : m_lowCut) f.set(300.0, 0.7071, sampleRate);
        for (Svf& f : m_highCut) f.set(6000.0, 0.7071, sampleRate);
    }
    // `beats`: the echo's length in quarter notes (0.75: a dotted eighth).
    void set(double mix, double feedback, double beats)
    {
        m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0));
        m_feedbackAmount = static_cast<float>(std::clamp(feedback, 0.0, 0.9));
        m_beats = beats;
    }
    void tick(float& left, float& right, double tempo)
    {
        if (m_mix <= 0.0F) return;
        const auto target = static_cast<float>(std::clamp(60.0 / std::max(tempo, 20.0) * m_beats, 0.01, 1.9) * m_rate);
        m_delay += (target - m_delay) * 0.0005F; // a tempo change glides, never clicks
        const float fromL = m_left.read(m_delay);
        const float fromR = m_right.read(m_delay);
        // Into the left only; each echo crosses to the other side.
        m_left.push(0.5F * (left + right) + m_highCut.at(1).lowPass(m_lowCut.at(1).highPass(fromR)) * m_feedbackAmount);
        m_right.push(m_highCut.at(0).lowPass(m_lowCut.at(0).highPass(fromL)) * m_feedbackAmount);
        left += fromL * m_mix;
        right += fromR * m_mix;
    }

private:
    double m_rate = 48000.0;
    DelayLine m_left, m_right;
    std::array<Svf, 2> m_lowCut{}, m_highCut{};
    float m_mix = 0.0F, m_feedbackAmount = 0.3F;
    double m_beats = 0.75;
    float m_delay = 12000.0F;
};

// A peaking or shelving EQ band (the RBJ cookbook biquads).
class EqBand
{
public:
    enum class Kind : uint8_t { Peak, HighShelf };
    void set(Kind kind, double hz, double gainDb, double q, double sampleRate)
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * std::numbers::pi * std::min(hz, sampleRate * 0.45) / sampleRate;
        const double cosw = std::cos(w);
        const double alpha = std::sin(w) / (2.0 * q);
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
        if (kind == Kind::Peak) {
            b0 = 1.0 + alpha * a;
            b1 = -2.0 * cosw;
            b2 = 1.0 - alpha * a;
            a0 = 1.0 + alpha / a;
            a1 = -2.0 * cosw;
            a2 = 1.0 - alpha / a;
        } else {
            const double root = 2.0 * std::sqrt(a) * alpha;
            b0 = a * ((a + 1.0) + (a - 1.0) * cosw + root);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw);
            b2 = a * ((a + 1.0) + (a - 1.0) * cosw - root);
            a0 = (a + 1.0) - (a - 1.0) * cosw + root;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw);
            a2 = (a + 1.0) - (a - 1.0) * cosw - root;
        }
        m_b = {static_cast<float>(b0 / a0), static_cast<float>(b1 / a0), static_cast<float>(b2 / a0)};
        m_a = {static_cast<float>(a1 / a0), static_cast<float>(a2 / a0)};
    }
    float tick(float x, std::size_t channel)
    {
        auto& [z1, z2] = m_state.at(channel);
        const float y = m_b.at(0) * x + z1;
        z1 = m_b.at(1) * x - m_a.at(0) * y + z2;
        z2 = m_b.at(2) * x - m_a.at(1) * y;
        return y;
    }

private:
    std::array<float, 3> m_b{1.0F, 0.0F, 0.0F};
    std::array<float, 2> m_a{};
    std::array<std::array<float, 2>, 2> m_state{};
};

// Tape, as felt and lo-fi pianos are finished: the pitch wavers slowly
// (wow) and quickly (flutter), it warms and rounds off (saturation, the top
// rolled off), and a little hiss sits under it. One `amount` 0-1 for all of it.
class Tape
{
public:
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        m_left.allocate(static_cast<std::size_t>(0.02 * sampleRate));
        m_right.allocate(static_cast<std::size_t>(0.02 * sampleRate));
        m_wowStep = 0.55 / sampleRate;
        m_flutterStep = 6.5 / sampleRate;
        m_roll = OnePole::coefficient(7000.0, sampleRate);
        m_hissColour = OnePole::coefficient(5000.0, sampleRate);
    }
    void setAmount(double amount) { m_amount = static_cast<float>(std::clamp(amount, 0.0, 1.0)); }
    void tick(float& left, float& right, Random& random)
    {
        m_left.push(left);
        m_right.push(right);
        if (m_amount <= 0.0F) return;
        m_wow += m_wowStep;
        m_wow -= std::floor(m_wow);
        m_flutter += m_flutterStep;
        m_flutter -= std::floor(m_flutter);
        constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
        const auto ms = static_cast<float>(m_rate / 1000.0);
        // 5 ms in, wavering by up to 2 ms (wow) and 0.2 ms (flutter).
        const float delay = ms * (5.0F + m_amount * (2.0F * std::sin(kTwoPi * static_cast<float>(m_wow))
                                                     + 0.2F * std::sin(kTwoPi * static_cast<float>(m_flutter))));
        const float drive = 1.0F + 2.5F * m_amount;
        const auto shape = [drive](float x) { return std::tanh(x * drive) / drive; };
        float l = shape(m_left.read(delay));
        float r = shape(m_right.read(delay));
        l = m_rollL.tick(l, m_roll);
        r = m_rollR.tick(r, m_roll);
        const float hiss = 0.006F * m_amount;
        l += m_hissL.tick(random.bipolar(), m_hissColour) * hiss;
        r += m_hissR.tick(random.bipolar(), m_hissColour) * hiss;
        left = left + (l - left) * m_amount;
        right = right + (r - right) * m_amount;
    }

private:
    double m_rate = 48000.0, m_wow = 0.0, m_flutter = 0.0, m_wowStep = 0.0, m_flutterStep = 0.0;
    DelayLine m_left, m_right;
    OnePole m_rollL, m_rollR, m_hissL, m_hissR;
    float m_roll = 1.0F, m_hissColour = 1.0F, m_amount = 0.0F;
};

// Sidechain ducking as Kickstart does it: one of its curves
// (sidechain::kCurves) every `beats` quarter notes, from the song's beat or
// from the last note played.
class Sidechain
{
public:
    // `mix` 0-1: how far it ducks; `slope` 0-1 stretches the dip (0.5: as the curve is).
    void set(double mix, int curve, double beats, double slope, bool fromNotes)
    {
        m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0));
        m_curve = curve;
        m_beats = std::max(beats, 0.125);
        m_slope = slope;
        m_fromNotes = fromNotes;
    }
    // A note was played at `ppq`: following the notes, the curve starts again there.
    void noteAt(double ppq)
    {
        if (m_fromNotes) m_start = ppq;
    }
    // The gain for this sample at `ppq` (quarter notes since the clock started).
    [[nodiscard]] float gain(double ppq)
    {
        // The song's clock gone back past the last note (a jump, a clock
        // Start): the curve goes on from where it was, never stuck down.
        if (m_fromNotes && ppq < m_start) m_start = ppq - (m_position * m_beats);
        const double from = m_fromNotes ? ppq - m_start : ppq;
        m_position = std::fmod(std::max(from, 0.0), m_beats) / m_beats;
        float g = 1.0F;
        if (m_mix > 0.0F) g = 1.0F - (m_mix * (1.0F - static_cast<float>(sidechain::curveGain(m_curve, m_position, m_slope))));
        // Never a step: smoothed over a millisecond.
        m_smoothed += (g - m_smoothed) * 0.05F;
        return m_smoothed;
    }
    // Where it is in its cycle (0-1), at the last gain().
    [[nodiscard]] double position() const { return m_position; }

private:
    float m_mix = 0.0F, m_smoothed = 1.0F;
    int m_curve = 0;
    double m_beats = 1.0, m_slope = 0.5, m_start = 0.0, m_position = 0.0;
    bool m_fromNotes = false;
};

} // namespace gigchain::engine::sawstack
