#pragma once

#include "SawStackDsp.h"
#include "SawStackFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// Pads Pro's own building blocks: its two driven filters, the struck
// resonators of its texture layer, the lo-fi stage, a bucket-brigade
// chorus, a phaser and a plate reverb. All written here from published
// methods (named on each); no other product's code. Audio thread: nothing
// allocates, locks or logs (prepare() does).
namespace gigchain::engine::pads {

using sawstack::DelayLine;
using sawstack::OnePole;
using sawstack::Random;
using sawstack::Svf;

inline constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;

// tanh, closely enough for saturation (a Padé form, exactly 1 from 3 up), and cheap.
[[nodiscard]] inline float fastTanh(float x)
{
    const float c = std::clamp(x, -3.0F, 3.0F);
    return c * (27.0F + c * c) / (27.0F + 9.0F * c * c);
}

// Saturation that leaves quiet sounds as they are: `drive` 1 is all but
// clean, more rounds loud peaks off (an analog stage pushed). (Anything that
// bends the wave makes overtones; at the plain rate those above half of it
// fold back as notes that aren't in the chord: the "8-bit" grit. Pads Pro
// runs its voices four times faster and takes the rate back down through
// Decimator4.)
[[nodiscard]] inline float saturate(float x, float drive)
{
    return fastTanh(x * drive) / drive;
}

// A half-band low-pass that halves the rate: a windowed sinc (Kaiser), so
// what is above the new rate's half is taken away before every other
// sample is dropped. Two samples in, one out.
class HalfBand
{
public:
    // `side`: taps each side of the centre (odd); `beta`: the Kaiser window's (about 9 for -90 dB).
    void design(int side, double beta)
    {
        m_side = side;
        m_length = static_cast<std::size_t>(2 * side + 1);
        m_taps.clear();
        const auto bessel = [](double x) {
            double sum = 1.0;
            double term = 1.0;
            for (int k = 1; k < 40; ++k) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        double total = 0.5;
        for (int k = 1; k <= side; k += 2) {
            const double r = static_cast<double>(k) / (side + 1);
            const double window = bessel(beta * std::sqrt(1.0 - r * r)) / bessel(beta);
            const double h = std::sin(std::numbers::pi * k / 2.0) / (std::numbers::pi * k) * window;
            m_taps.push_back(static_cast<float>(h));
            total += 2.0 * h;
        }
        m_centre = static_cast<float>(0.5 / total);
        std::ranges::transform(m_taps, m_taps.begin(), [total](float t) { return static_cast<float>(t / total); });
        m_history.assign(2 * m_length, 0.0F);
        m_pos = 0;
    }
    float tick(float a, float b)
    {
        push(a);
        push(b);
        const float* x = &m_history[m_pos]; // newest first
        const auto centre = static_cast<std::size_t>(m_side);
        float y = m_centre * x[centre];
        for (std::size_t i = 0; i < m_taps.size(); ++i) {
            const std::size_t k = 2 * i + 1;
            y += m_taps[i] * (x[centre - k] + x[centre + k]);
        }
        return y;
    }

private:
    void push(float x)
    {
        m_pos = (m_pos == 0 ? m_length : m_pos) - 1;
        m_history[m_pos] = x;
        m_history[m_pos + m_length] = x; // (twice: the window is always whole)
    }
    int m_side = 1;
    std::size_t m_length = 3, m_pos = 0;
    std::vector<float> m_taps;
    std::vector<float> m_history;
    float m_centre = 0.5F;
};

// Four times the rate back down to the rate: two half-bands, the second
// long enough to keep everything to 20 kHz and take away all from 28 kHz
// (at 48 kHz: about -90 dB).
class Decimator4
{
public:
    void prepare()
    {
        m_first.design(11, 9.0);
        m_second.design(35, 9.0);
    }
    float tick(const float* four)
    {
        const float a = m_first.tick(four[0], four[1]);
        const float b = m_first.tick(four[2], four[3]);
        return m_second.tick(a, b);
    }

private:
    HalfBand m_first, m_second;
};

// A voice's filter settings, worked out a few times a millisecond and shared
// by its two ears.
struct FilterCoeffs
{
    float g = 0.1F;        // the integrators' gain: tan(pi fc / rate)
    float ladderG = 0.09F; // a one-pole stage's: g / (1 + g)
    float k = 1.4142F;     // the 2-pole's damping (1/Q)
    float a1 = 1.0F, a2 = 0.0F, a3 = 0.0F;
    float feedback = 0.0F; // the ladder's resonance

    void set(double hz, double resonance, double sampleRate)
    {
        const double fc = std::clamp(hz, 20.0, sampleRate * 0.45);
        const double gd = std::tan(std::numbers::pi * fc / sampleRate);
        g = static_cast<float>(gd);
        ladderG = static_cast<float>(gd / (1.0 + gd));
        const double res = std::clamp(resonance, 0.0, 1.0);
        k = static_cast<float>(std::numbers::sqrt2 * (1.0 - 0.85 * res)); // 0: Butterworth, no peak
        a1 = 1.0F / (1.0F + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        feedback = static_cast<float>(3.2 * res);
    }
};

// The soft 2-pole (12 dB/oct), as an OB's or a SEM's: the state-variable
// filter of Zavalishin's "The Art of VA Filter Design" (trapezoidal), the
// signal driven into it and its resonant state softly limited (the drive
// inside the loop: loud chords round off warmly, never harshly).
struct SoftFilter
{
    float ic1 = 0.0F, ic2 = 0.0F;
    float tick(float in, const FilterCoeffs& c, float drive)
    {
        const float x = saturate(in, drive);
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0F * fastTanh(0.5F * (2.0F * v1 - ic1));
        ic2 = 2.0F * v2 - ic2;
        return v2;
    }
};

// The 4-pole ladder (24 dB/oct), as a Moog's or a Prophet's: four one-pole
// stages (trapezoidal), the input driven, the resonance fed back from the
// last stage a sample late (enough at the low resonance pads use).
struct LadderFilter
{
    std::array<float, 4> s{};
    float y = 0.0F;
    float tick(float in, const FilterCoeffs& c, float drive)
    {
        float x = saturate(in - c.feedback * y, drive);
        for (float& state : s) {
            const float v = (x - state) * c.ladderG;
            const float out = v + state;
            state = out + v;
            x = out;
        }
        y = x;
        return y * (1.0F + 0.5F * c.feedback); // (resonance thins the lows: given back)
    }
};

// What floats over a pad: struck bars, forks, glasses, plates or bells, or air.
enum class Texture : uint8_t
{
    Air,       // band-passed breath, high up
    Celesta,   // steel bars over tuned boxes
    Dulcitone, // tuning forks
    Glass,     // a rubbed or struck wine glass
    Metal,     // a struck metal plate: inharmonic, darker
    Bell,      // a church bell's partials
};

inline constexpr int kModes = 6;

// A struck thing's partials: each one's frequency (times the note's), how
// loud it is struck and how long it rings (seconds to -60 dB). Ratio 0: unused.
struct ModeSet
{
    std::array<float, kModes> ratio{}, gain{}, seconds{};
};

[[nodiscard]] inline const ModeSet& modeSet(Texture texture)
{
    // The ratios are the textbook ones (Fletcher & Rossing, "The Physics of
    // Musical Instruments"): a free bar's 1 : 2.756 : 5.404 (the celesta's,
    // with its box's octave), a tuning fork's fundamental and its fast-dying
    // "clang" near 6.3 and 17.6 times it, a wine glass's 1 : 2.3 : 4.2 : 6.6,
    // a free circular plate's inharmonic set, and a church bell's hum, prime,
    // minor third, fifth and nominal. The levels and times are ours, by ear.
    static constexpr ModeSet kNone{};
    static constexpr ModeSet kCelesta{{1.0F, 2.0F, 2.756F, 4.0F, 5.404F, 0.0F},
                                      {1.0F, 0.2F, 0.3F, 0.06F, 0.1F, 0.0F},
                                      {3.0F, 1.6F, 1.2F, 0.6F, 0.5F, 0.0F}};
    static constexpr ModeSet kFork{{1.0F, 6.27F, 17.55F, 0.0F, 0.0F, 0.0F},
                                   {1.0F, 0.3F, 0.08F, 0.0F, 0.0F, 0.0F},
                                   {4.0F, 0.4F, 0.12F, 0.0F, 0.0F, 0.0F}};
    static constexpr ModeSet kGlass{{1.0F, 2.32F, 4.25F, 6.63F, 0.0F, 0.0F},
                                    {1.0F, 0.45F, 0.25F, 0.12F, 0.0F, 0.0F},
                                    {4.0F, 2.6F, 1.6F, 0.9F, 0.0F, 0.0F}};
    static constexpr ModeSet kMetal{{1.0F, 1.73F, 2.33F, 2.92F, 3.91F, 4.79F},
                                    {0.7F, 1.0F, 0.7F, 0.55F, 0.35F, 0.25F},
                                    {2.5F, 2.0F, 1.8F, 1.5F, 1.0F, 0.8F}};
    static constexpr ModeSet kBell{{0.5F, 1.0F, 1.183F, 1.506F, 2.0F, 2.514F},
                                   {0.45F, 1.0F, 0.55F, 0.35F, 0.5F, 0.2F},
                                   {4.5F, 3.2F, 2.6F, 2.0F, 1.6F, 1.0F}};
    switch (texture) {
    case Texture::Air: return kNone;
    case Texture::Celesta: return kCelesta;
    case Texture::Dulcitone: return kFork;
    case Texture::Glass: return kGlass;
    case Texture::Metal: return kMetal;
    case Texture::Bell: return kBell;
    }
    return kNone;
}

// A bank of tuned resonators (each a decaying rotation: a pure partial that
// rings on its own once struck). A strike adds to what is ringing, so
// strikes overlap softly; each partial starts from nothing (no click).
struct Resonators
{
    std::array<float, kModes> re{}, im{}, c{}, s{}, gain{};

    void tune(const ModeSet& set, double hz, double decayScale, double sampleRate)
    {
        for (std::size_t i = 0; i < kModes; ++i) {
            const double f = hz * set.ratio.at(i);
            if (set.ratio.at(i) <= 0.0F || f >= sampleRate * 0.45) {
                c.at(i) = s.at(i) = gain.at(i) = 0.0F;
                continue;
            }
            const double seconds = std::max(static_cast<double>(set.seconds.at(i)) * decayScale, 0.05);
            const double r = std::pow(0.001, 1.0 / (seconds * sampleRate));
            const double w = 2.0 * std::numbers::pi * f / sampleRate;
            c.at(i) = static_cast<float>(r * std::cos(w));
            s.at(i) = static_cast<float>(r * std::sin(w));
            gain.at(i) = set.gain.at(i);
        }
    }
    void strike(float amount)
    {
        for (std::size_t i = 0; i < kModes; ++i) re.at(i) += amount * gain.at(i);
    }
    float tick()
    {
        float out = 0.0F;
        for (std::size_t i = 0; i < kModes; ++i) {
            const float r = c.at(i) * re.at(i) - s.at(i) * im.at(i);
            const float m = s.at(i) * re.at(i) + c.at(i) * im.at(i);
            re.at(i) = r;
            im.at(i) = m;
            out += m;
        }
        return out;
    }
    // Rung out: exactly silent (what is left is far below hearing).
    void settle()
    {
        for (std::size_t i = 0; i < kModes; ++i) {
            if (std::abs(re.at(i)) + std::abs(im.at(i)) < 1.0e-9F) re.at(i) = im.at(i) = 0.0F;
        }
    }
};

// Lo-fi, as tape and old samplers colour a sound: saturation and a rolled-off
// top (tape, but no wow: the pitch stays put), then fewer samples and bits
// (a crusher). Each 0-1; 0 leaves the sound untouched.
class LoFi
{
public:
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        set(m_tape, m_crush);
    }
    void set(double tape, double crush)
    {
        m_tape = static_cast<float>(std::clamp(tape, 0.0, 1.0));
        m_crush = static_cast<float>(std::clamp(crush, 0.0, 1.0));
        m_roll = OnePole::coefficient(16000.0 * std::exp2(-2.7 * m_tape), m_rate); // 16 kHz .. 2.5 kHz (twice: 12 dB/oct)
        m_drive = 1.0F + 3.0F * m_tape;
        m_hold = 1.0F + 7.0F * m_crush * m_crush;                                  // samples kept (48 kHz .. 6 kHz)
        m_levels = static_cast<float>(std::exp2(15.0 - 9.0 * m_crush));          // 16 bits .. 7
    }
    void tick(float& left, float& right, Random& random)
    {
        if (m_tape > 0.0F) {
            const float hiss = 0.0015F * m_tape;
            left = m_rollL2.tick(m_rollL.tick(saturate(left, m_drive), m_roll), m_roll) + random.bipolar() * hiss;
            right = m_rollR2.tick(m_rollR.tick(saturate(right, m_drive), m_roll), m_roll) + random.bipolar() * hiss;
        }
        if (m_crush > 0.0F) {
            m_phase += 1.0F;
            if (m_phase >= m_hold) {
                m_phase -= m_hold;
                m_heldL = std::round(left * m_levels) / m_levels;
                m_heldR = std::round(right * m_levels) / m_levels;
            }
            left = m_heldL;
            right = m_heldR;
        }
    }

private:
    double m_rate = 48000.0;
    float m_tape = 0.0F, m_crush = 0.0F, m_roll = 1.0F, m_drive = 1.0F, m_hold = 1.0F, m_levels = 32768.0F;
    OnePole m_rollL, m_rollR, m_rollL2, m_rollR2;
    float m_phase = 0.0F, m_heldL = 0.0F, m_heldR = 0.0F;
};

// The Juno-60's chorus, to the numbers measured on the hardware
// (pendragon-andyh/Juno60's analysis and the service notes; the mixing
// resistors via jpcima's rc-effect-playground): one triangle LFO (a 49.3 %
// duty) sweeping two bucket-brigade delays between 1.66 and 5.35 ms, the
// right one's sweep upside down; mode I at 0.513 Hz ("mild"), II at
// 0.863 Hz ("deeper, richer"); a 12 dB low-pass before the chips and after
// them, the chips rounding the wave a little; at full, the dry 1.62 dB under
// the delayed. Mono in (as the Juno's), stereo out.
class BbdChorus
{
public:
    enum class Mode : uint8_t { One, Two };
    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        m_left.allocate(static_cast<std::size_t>(0.008 * sampleRate) + 8);
        m_right.allocate(static_cast<std::size_t>(0.008 * sampleRate) + 8);
        for (Svf& f : m_filters) f.set(kFilterHz, 0.7071, sampleRate);
    }
    void setMix(double mix) { m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0)); }
    void setMode(Mode mode) { m_hz = mode == Mode::One ? 0.513 : 0.863; }
    void tick(float& left, float& right, Random& random)
    {
        if (m_mix <= 0.0F) return;
        m_phase += m_hz / m_rate;
        if (m_phase >= 1.0) m_phase -= 1.0;
        // The triangle, rising for 49.3 % of its turn: 0 .. 1.
        const auto up = static_cast<float>(m_phase < kDuty ? m_phase / kDuty : (1.0 - m_phase) / (1.0 - kDuty));
        const auto ms = static_cast<float>(m_rate / 1000.0);
        const float in = m_filters[0].lowPass(m_filters[1].lowPass(0.5F * (left + right))) + random.bipolar() * 2.0e-5F;
        m_left.push(in);
        m_right.push(in);
        const float wetL = m_filters[2].lowPass(m_filters[3].lowPass(saturate(m_left.read(ms * (kMinMs + kSpanMs * up)), 1.3F)));
        const float wetR = m_filters[4].lowPass(m_filters[5].lowPass(saturate(m_right.read(ms * (kMinMs + kSpanMs * (1.0F - up))), 1.3F)));
        // At full: the delayed at 0.6, the dry 1.62 dB under it.
        const float wet = 0.6F * m_mix;
        const float dry = 1.0F - m_mix * (1.0F - 0.6F * kDryUnderWet);
        left = left * dry + wetL * wet;
        right = right * dry + wetR * wet;
    }

private:
    static constexpr double kDuty = 0.493;
    static constexpr float kMinMs = 1.66F;
    static constexpr float kSpanMs = 5.35F - 1.66F;
    static constexpr float kDryUnderWet = 0.8297F; // -1.62 dB
    static constexpr double kFilterHz = 9000.0;    // (the chips' anti-alias filters)
    double m_rate = 48000.0, m_phase = 0.0, m_hz = 0.513;
    DelayLine m_left, m_right;
    std::array<Svf, 6> m_filters{};
    float m_mix = 0.0F;
};

// A slow phaser: four allpass stages swept together (250 Hz to 2.3 kHz,
// the ears a quarter turn apart), a little fed back, mixed with the dry.
class Phaser
{
public:
    void prepare(double sampleRate) { m_rate = sampleRate; }
    void setMix(double mix) { m_mix = static_cast<float>(std::clamp(mix, 0.0, 1.0)); }
    void tick(float& left, float& right)
    {
        if (m_mix <= 0.0F) return;
        if (--m_countdown <= 0) {
            m_countdown = kUpdate;
            m_phase += kUpdate * 0.11 / m_rate;
            if (m_phase >= 1.0) m_phase -= 1.0;
            for (std::size_t ch = 0; ch < 2; ++ch) {
                const double sweep = 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * (m_phase + 0.25 * static_cast<double>(ch)));
                const double t = std::tan(std::numbers::pi * 250.0 * std::exp2(3.2 * sweep) / m_rate);
                m_ears.at(ch).a = static_cast<float>((t - 1.0) / (t + 1.0));
            }
        }
        left = m_ears.at(0).tick(left, m_mix);
        right = m_ears.at(1).tick(right, m_mix);
    }

private:
    static constexpr int kUpdate = 16;
    struct Ear
    {
        float a = 0.0F, last = 0.0F;
        std::array<float, 4> z{};
        float tick(float in, float mix)
        {
            float x = in + 0.3F * last;
            for (float& state : z) {
                const float y = a * x + state;
                state = x - a * y;
                x = y;
            }
            last = x;
            return in * (1.0F - 0.5F * mix) + x * 0.5F * mix;
        }
    };
    double m_rate = 48000.0, m_phase = 0.0;
    int m_countdown = 0;
    std::array<Ear, 2> m_ears{};
    float m_mix = 0.0F;
};

// A plate reverb: Jon Dattorro's ("Effect Design, Part 1: Reverberator and
// Other Filters", J. Audio Eng. Soc. 45(9), 1997). The input band-limited
// and smeared by four allpasses, then a figure-of-eight tank of two halves
// (a slowly moving allpass, a delay, damping, an allpass, a delay), each
// feeding the other; each ear reads four of the paper's taps from one half.
// The paper's lengths (at 29,761 Hz) are scaled to the rate and the plate's
// size. Wet only.
class Plate
{
public:
    struct Settings
    {
        double decaySeconds = 6.0; // to -60 dB
        double size = 0.85;        // 0.4-1 (1: the paper's plate)
        double dampHz = 7000.0;    // above it the tail fades faster
        double preDelay = 0.012;   // seconds
        double inputHz = 10000.0;  // what goes in, at most
        double lowCutHz = 250.0;   // and at least: held low notes would ring the tank's modes (+12 dB, measured), muddying the pad
    };

    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        const double scale = sampleRate / kPaperRate;
        const auto room = [scale](double samples) { return static_cast<std::size_t>(samples * scale) + 48; };
        for (std::size_t i = 0; i < m_diffuse.size(); ++i) m_diffuse.at(i).allocate(room(kDiffuse.at(i)));
        m_apA.allocate(room(672.0 + 32.0));
        m_delayA1.allocate(room(4453.0));
        m_apA2.allocate(room(1800.0));
        m_delayA2.allocate(room(3720.0));
        m_apB.allocate(room(908.0 + 32.0));
        m_delayB1.allocate(room(4217.0));
        m_apB2.allocate(room(2656.0));
        m_delayB2.allocate(room(3163.0));
        m_pre.allocate(static_cast<std::size_t>(0.3 * sampleRate) + 8);
        set(m_settings);
    }

    void set(const Settings& s)
    {
        m_settings = s;
        const double scale = m_rate / kPaperRate * std::clamp(s.size, 0.4, 1.0);
        const auto len = [scale](double samples) { return std::max<std::size_t>(static_cast<std::size_t>(samples * scale), 1); };
        for (std::size_t i = 0; i < kDiffuse.size(); ++i) m_diffuseLen.at(i) = len(kDiffuse.at(i));
        m_lenApA = static_cast<float>(672.0 * scale);
        m_lenA1 = len(4453.0);
        m_lenApA2 = len(1800.0);
        m_lenA2 = len(3720.0);
        m_lenApB = static_cast<float>(908.0 * scale);
        m_lenB1 = len(4217.0);
        m_lenApB2 = len(2656.0);
        m_lenB2 = len(3163.0);
        for (std::size_t i = 0; i < kTaps.size(); ++i) m_taps.at(i) = len(kTaps.at(i));
        m_excursion = static_cast<float>(4.0 * scale); // (a quarter of the paper's: held chords would swell and fade as it moves)
        m_lowCut.set(s.lowCutHz, 0.7071, m_rate);
        // Once round a half of the tank (where the decay is applied twice, as
        // the paper draws it): its share of -60 dB over the decay.
        const double half = (672.0 + 4453.0 + 1800.0 + 3720.0) * scale / m_rate;
        m_decay = static_cast<float>(std::clamp(std::pow(0.001, half / (2.0 * std::max(s.decaySeconds, 0.2))), 0.0, 0.97));
        m_decayDiffusion2 = std::clamp(m_decay + 0.15F, 0.25F, 0.5F);
        m_damp = OnePole::coefficient(s.dampHz, m_rate);
        m_band = OnePole::coefficient(s.inputHz, m_rate);
        m_preSamples = static_cast<float>(std::clamp(s.preDelay, 0.0, 0.29) * m_rate) + 1.0F;
    }

    void tick(float& left, float& right)
    {
        m_pre.push(m_lowCut.highPass(0.5F * (left + right)) + kAntiDenormal);
        float x = m_bandFilter.tick(m_pre.read(m_preSamples), m_band);
        for (std::size_t i = 0; i < m_diffuse.size(); ++i) x = allpass(m_diffuse.at(i), m_diffuseLen.at(i), kDiffusion.at(i), x);

        m_mod += 0.6 / m_rate;
        if (m_mod >= 1.0) m_mod -= 1.0;
        const auto swing = static_cast<float>(std::sin(2.0 * std::numbers::pi * m_mod));
        const auto swingB = static_cast<float>(std::cos(2.0 * std::numbers::pi * m_mod));

        // The first half, fed by the end of the second.
        float a = movingAllpass(m_apA, m_lenApA + m_excursion * swing, -0.7F, x + m_decay * m_endB);
        float t = m_delayA1.at(m_lenA1);
        m_delayA1.push(a);
        t = m_dampA.tick(t, m_damp) * m_decay;
        t = allpass(m_apA2, m_lenApA2, m_decayDiffusion2, t);
        const float endA = m_delayA2.at(m_lenA2);
        m_delayA2.push(t);
        // The second half, fed by the end of the first.
        a = movingAllpass(m_apB, m_lenApB + m_excursion * swingB, -0.7F, x + m_decay * m_endA);
        t = m_delayB1.at(m_lenB1);
        m_delayB1.push(a);
        t = m_dampB.tick(t, m_damp) * m_decay;
        t = allpass(m_apB2, m_lenApB2, m_decayDiffusion2, t);
        m_endB = m_delayB2.at(m_lenB2);
        m_delayB2.push(t);
        m_endA = endA;

        // The paper's taps, each ear from its own half only. (The paper also
        // takes the other half away from each ear: on held chords, where
        // both halves ring alike, that set the ears against each other, and
        // the pad went hollow in mono. Measured: correlation 0.64 to 0.07.)
        const auto& k = m_taps;
        left = 0.8F * (m_delayB1.at(k[0]) + m_delayB1.at(k[1]) - m_apB2.at(k[2]) + m_delayB2.at(k[3]));
        right = 0.8F * (m_delayA1.at(k[4]) + m_delayA1.at(k[5]) - m_apA2.at(k[6]) + m_delayA2.at(k[7]));
    }

private:
    static constexpr double kPaperRate = 29761.0;
    static constexpr float kAntiDenormal = 1.0e-20F;
    static constexpr std::array<double, 4> kDiffuse{142.0, 107.0, 379.0, 277.0};
    static constexpr std::array<float, 4> kDiffusion{0.75F, 0.75F, 0.625F, 0.625F};
    static constexpr std::array<double, 8> kTaps{266.0, 2974.0, 1913.0, 1996.0, 353.0, 3627.0, 1228.0, 2673.0};

    // An allpass `length` samples long (the Schroeder form, as the paper draws it).
    static float allpass(DelayLine& line, std::size_t length, float g, float in)
    {
        const float delayed = line.at(length);
        const float w = in - g * delayed;
        line.push(w);
        return delayed + g * w;
    }
    static float movingAllpass(DelayLine& line, float length, float g, float in)
    {
        const float delayed = line.read(length);
        const float w = in - g * delayed;
        line.push(w);
        return delayed + g * w;
    }

    double m_rate = 48000.0, m_mod = 0.0;
    Settings m_settings;
    std::array<DelayLine, 4> m_diffuse{};
    std::array<std::size_t, 4> m_diffuseLen{};
    DelayLine m_apA, m_delayA1, m_apA2, m_delayA2, m_apB, m_delayB1, m_apB2, m_delayB2, m_pre;
    float m_lenApA = 672.0F, m_lenApB = 908.0F;
    std::size_t m_lenA1 = 1, m_lenApA2 = 1, m_lenA2 = 1, m_lenB1 = 1, m_lenApB2 = 1, m_lenB2 = 1;
    std::array<std::size_t, 8> m_taps{};
    float m_excursion = 16.0F, m_decay = 0.5F, m_decayDiffusion2 = 0.5F, m_damp = 1.0F, m_band = 1.0F, m_preSamples = 1.0F;
    OnePole m_bandFilter, m_dampA, m_dampB;
    Svf m_lowCut;
    float m_endA = 0.0F, m_endB = 0.0F;
};

// A hall: our own, from Jot's feedback delay network (Jot & Chaigne,
// "Digital delay networks for designing artificial reverberators", AES
// 1991). Eight delay lines of unrelated lengths, each fed back into all
// through a Hadamard matrix (every line into every other, losing nothing:
// dense and colourless); each line's own loss set so the tail falls 60 dB
// in the decay time (Jot's rule, gain 10^(-3 L / (rate T60))), a one-pole
// in each loop letting the highs die sooner; the lines' lengths slowly
// moving (each its own rate) so it never rings metallic. What goes in is
// cleared of its lows and smeared by three allpasses. Wet only.
class Hall
{
public:
    struct Settings
    {
        double decaySeconds = 6.0; // to -60 dB
        double size = 0.85;        // 0.4-1: a room to a cathedral
        double dampHz = 7000.0;    // above it the tail fades faster
        double preDelay = 0.02;    // seconds
        double lowCutHz = 200.0;   // what goes in, at least (held low notes would boom in it)
    };

    void prepare(double sampleRate)
    {
        m_rate = sampleRate;
        const auto room = [sampleRate](double ms) { return static_cast<std::size_t>(ms * 0.001 * sampleRate) + 16; };
        for (std::size_t i = 0; i < kLines; ++i) m_lines.at(i).allocate(room(kLineMs.at(i) * kMaxScale + kSwingMs * 2.0));
        for (std::size_t i = 0; i < kDiffusers; ++i) {
            m_diffuseL.at(i).allocate(room(kDiffuseMs.at(i)));
            m_diffuseR.at(i).allocate(room(kDiffuseMs.at(i)));
        }
        m_preL.allocate(room(300.0));
        m_preR.allocate(room(300.0));
        for (std::size_t i = 0; i < kLines; ++i) m_swing.at(i) = 0.17 * static_cast<double>(i);
        set(m_settings);
    }

    void set(const Settings& s)
    {
        m_settings = s;
        const double scale = (0.35 + 0.65 * std::clamp(s.size, 0.4, 1.0)) * kMaxScale;
        const double seconds = std::max(s.decaySeconds, 0.2);
        for (std::size_t i = 0; i < kLines; ++i) {
            const double samples = kLineMs.at(i) * scale * 0.001 * m_rate;
            m_length.at(i) = static_cast<float>(samples);
            m_gain.at(i) = static_cast<float>(std::pow(10.0, -3.0 * samples / (m_rate * seconds)));
        }
        m_damp = OnePole::coefficient(s.dampHz, m_rate);
        m_preSamples = static_cast<float>(std::clamp(s.preDelay, 0.0, 0.29) * m_rate) + 1.0F;
        m_lowCutL.set(s.lowCutHz, 0.7071, m_rate);
        m_lowCutR.set(s.lowCutHz, 0.7071, m_rate);
        for (std::size_t i = 0; i < kDiffusers; ++i) m_diffuseLen.at(i) = static_cast<std::size_t>(kDiffuseMs.at(i) * 0.001 * m_rate);
    }

    void tick(float& left, float& right)
    {
        m_preL.push(m_lowCutL.highPass(left) + kAntiDenormal);
        m_preR.push(m_lowCutR.highPass(right) - kAntiDenormal);
        float inL = m_preL.read(m_preSamples);
        float inR = m_preR.read(m_preSamples);
        for (std::size_t i = 0; i < kDiffusers; ++i) {
            inL = allpass(m_diffuseL.at(i), m_diffuseLen.at(i), kDiffusion, inL);
            inR = allpass(m_diffuseR.at(i), m_diffuseLen.at(i), kDiffusion, inR);
        }
        // Each line out (its length moving a little), through its loss.
        const double swingStep = 1.0 / m_rate;
        std::array<float, kLines> out{};
        for (std::size_t i = 0; i < kLines; ++i) {
            m_swing.at(i) += swingStep * kSwingHz.at(i);
            if (m_swing.at(i) >= 1.0) m_swing.at(i) -= 1.0;
            const auto swing = static_cast<float>(std::sin(2.0 * std::numbers::pi * m_swing.at(i)) * kSwingMs * 0.001 * m_rate);
            const float delayed = m_lines.at(i).read(m_length.at(i) + swing);
            out.at(i) = m_dampers.at(i).tick(delayed, m_damp) * m_gain.at(i);
        }
        // Every line into every other: the Hadamard matrix (fast, scaled to lose nothing).
        std::array<float, kLines> mixed = out;
        for (std::size_t half = 1; half < kLines; half <<= 1U) {
            for (std::size_t i = 0; i < kLines; i += half * 2) {
                for (std::size_t j = i; j < i + half; ++j) {
                    const float a = mixed.at(j);
                    const float b = mixed.at(j + half);
                    mixed.at(j) = a + b;
                    mixed.at(j + half) = a - b;
                }
            }
        }
        for (std::size_t i = 0; i < kLines; ++i) {
            const float in = (i % 2 == 0 ? inL : inR) * kInput;
            m_lines.at(i).push(mixed.at(i) * kNorm + in);
        }
        // Each ear its own mix of the lines (orthogonal: wide, and solid in mono).
        float l = 0.0F;
        float r = 0.0F;
        for (std::size_t i = 0; i < kLines; ++i) {
            l += out.at(i) * kLeftSigns.at(i);
            r += out.at(i) * kRightSigns.at(i);
        }
        left = l * kOutput;
        right = r * kOutput;
    }

private:
    static constexpr std::size_t kLines = 8;
    static constexpr std::size_t kDiffusers = 3;
    static constexpr double kMaxScale = 1.5;
    static constexpr double kSwingMs = 0.35;
    static constexpr float kDiffusion = 0.6F;
    static constexpr float kNorm = 0.35355339F; // 1 / sqrt(8)
    static constexpr float kInput = 0.5F;
    static constexpr float kOutput = 0.5F;
    static constexpr float kAntiDenormal = 1.0e-20F;
    static constexpr std::array<double, kLines> kLineMs{37.3, 43.7, 51.1, 59.9, 69.7, 80.9, 93.1, 107.3};
    static constexpr std::array<double, kLines> kSwingHz{0.11, 0.13, 0.17, 0.19, 0.23, 0.29, 0.31, 0.37};
    static constexpr std::array<double, kDiffusers> kDiffuseMs{3.1, 7.7, 11.3};
    static constexpr std::array<float, kLines> kLeftSigns{1, 1, -1, -1, 1, 1, -1, -1};
    static constexpr std::array<float, kLines> kRightSigns{1, -1, 1, -1, -1, 1, -1, 1};

    static float allpass(DelayLine& line, std::size_t length, float g, float in)
    {
        const float delayed = line.at(std::max<std::size_t>(length, 1));
        const float w = in - g * delayed;
        line.push(w);
        return delayed + g * w;
    }

    double m_rate = 48000.0;
    Settings m_settings;
    std::array<DelayLine, kLines> m_lines{};
    std::array<float, kLines> m_length{}, m_gain{};
    std::array<OnePole, kLines> m_dampers{};
    std::array<double, kLines> m_swing{};
    std::array<DelayLine, kDiffusers> m_diffuseL{}, m_diffuseR{};
    std::array<std::size_t, kDiffusers> m_diffuseLen{};
    DelayLine m_preL, m_preR;
    float m_preSamples = 1.0F, m_damp = 1.0F;
    Svf m_lowCutL, m_lowCutR;
};

} // namespace gigchain::engine::pads
