#include "PadsProSpectral.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <mutex>
#include <numbers>
#include <numeric>
#include <random>
#include <thread>
#include <tuple>
#include <utility>

namespace gigchain::engine::pads {

namespace {

using Complex = std::complex<double>;

// An in-place inverse FFT (radix 2; `data.size()` a power of two).
void inverseFft(std::vector<Complex>& data)
{
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1U;
        for (; (j & bit) != 0; bit >>= 1U) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data.at(i), data.at(j));
    }
    for (std::size_t len = 2; len <= n; len <<= 1U) {
        const double angle = 2.0 * std::numbers::pi / static_cast<double>(len); // (+: the inverse)
        const Complex step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            Complex w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const Complex u = data.at(i + k);
                const Complex v = data.at(i + k + len / 2) * w;
                data.at(i + k) = u + v;
                data.at(i + k + len / 2) = u - v;
                w *= step;
            }
        }
    }
}

double bump(double hz, double centre, double width)
{
    const double x = (hz - centre) / width;
    return std::exp(-x * x);
}

constexpr double kPiano = std::numbers::pi;

// A struck bell's partials against its note (hum, note, minor third, fifth,
// octave and up: the classic bell's), and how loud each is held.
constexpr std::array<double, 12> kBellRatio{0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215};
constexpr std::array<double, 12> kBellLevel{0.45, 1.0, 0.55, 0.4, 0.5, 0.3, 0.22, 0.18, 0.13, 0.09, 0.06, 0.04};

// Where partial `n` sits, against the note (the harmonics: n; a bell's and
// bowed metal's: their own); 0 when the spectrum has no partial `n`.
double ratio(Spectrum spectrum, int n)
{
    switch (spectrum) {
    case Spectrum::Bell: return std::cmp_less_equal(n, kBellRatio.size()) ? kBellRatio.at(static_cast<std::size_t>(n - 1)) : 0.0;
    case Spectrum::Metal: return std::pow(n, 1.12);
    case Spectrum::Piano: return n * std::sqrt(1.0 + 0.0004 * n * n); // (Fletcher: the stiff string's inharmonicity)
    default: return n;
    }
}

// How loud harmonic `n` of `f0` is, in each spectrum (our own shapes).
double amplitude(Spectrum spectrum, int n, double f0)
{
    const double hz = ratio(spectrum, n) * f0;
    switch (spectrum) {
    case Spectrum::Reed:
        return (n % 2 == 1 ? 1.0 : 0.12) / n / (1.0 + std::pow(hz / 4000.0, 2.0));
    case Spectrum::Hollow:
        return n % 2 == 1 ? 1.0 / (static_cast<double>(n) * n) : 0.0;
    case Spectrum::Ooh: {
        const double voice = 1.0 / std::pow(n, 0.9);
        const double formants = 0.08 + bump(hz, 320.0, 90.0) + 0.45 * bump(hz, 800.0, 140.0) + 0.12 * bump(hz, 2300.0, 250.0);
        return voice * formants * (hz > 6000.0 ? std::exp(-(hz - 6000.0) / 1500.0) : 1.0);
    }
    case Spectrum::Organ:
        switch (n) {
        case 1: return 1.0;
        case 2: return 0.7;
        case 3: return 0.45;
        case 4: return 0.5;
        case 6: return 0.3;
        case 8: return 0.25;
        default: return 0.0;
        }
    case Spectrum::Bell:
        return std::cmp_less_equal(n, kBellLevel.size()) ? kBellLevel.at(static_cast<std::size_t>(n - 1)) : 0.0;
    case Spectrum::Metal:
        return n <= 40 ? (1.0 / std::pow(n, 0.8)) / (1.0 + std::pow(hz / 6000.0, 2.0)) : 0.0;
    case Spectrum::Wind:
        return (1.0 / std::sqrt(n)) / (1.0 + std::pow(hz / 5000.0, 2.0));
    case Spectrum::Piano:
        // Struck an eighth of the way along: the partials near the 8th and
        // 16th missing; falling as a felt hammer's strike does.
        return (0.08 + std::abs(std::sin(kPiano * n / 8.0))) / std::pow(n, 1.1) / (1.0 + std::pow(hz / 5000.0, 2.0));
    case Spectrum::Principal:
        return (n <= 16 ? 1.0 / std::pow(n, 1.3) : 0.0) * (n == 2 ? 1.3 : 1.0);
    default: break;
    }
    switch (spectrum) {
    case Spectrum::Choir: {
        // A soft voice: falling harmonics, lifted where an "aah" resonates.
        const double voice = 1.0 / std::pow(n, 0.8);
        const double formants = 0.12 + bump(hz, 800.0, 120.0) + 0.6 * bump(hz, 1150.0, 140.0) + 0.3 * bump(hz, 2900.0, 260.0)
                                + 0.12 * bump(hz, 3500.0, 300.0);
        return voice * formants * (hz > 9000.0 ? std::exp(-(hz - 9000.0) / 2000.0) : 1.0);
    }
    case Spectrum::Strings:
        // A bow on many strings: a saw's harmonics, rolled off gently above a few kHz.
        return (1.0 / n) / (1.0 + std::pow(hz / 6500.0, 2.0));
    case Spectrum::Air:
        // A little body, then breath spread wide up high.
        return n <= 4 ? 0.4 / n : (hz > 1500.0 ? 0.06 * std::exp(-hz / 12000.0) : 0.02);
    case Spectrum::Glass:
        return n == 1 ? 1.0 : (n == 2 ? 0.35 : (n == 3 ? 0.12 : (n == 4 ? 0.06 : 0.0)));
    default: break;
    }
    return 0.0;
}

// How the bands widen with the harmonic (PADsynth's "scale").
double widening(Spectrum spectrum)
{
    switch (spectrum) {
    case Spectrum::Choir:
    case Spectrum::Strings: return 1.0;
    case Spectrum::Air: return 1.2;
    case Spectrum::Glass: return 0.6;
    case Spectrum::Bell: return 0.3;
    case Spectrum::Metal: return 0.5;
    case Spectrum::Organ: return 0.0;
    case Spectrum::Wind: return 0.8;
    default: return 1.0;
    }
}

std::vector<float> buildLoop(Spectrum spectrum, double bandwidthCents, double f0, double rate, uint32_t seed)
{
    const int length = SpectralTables::kLength;
    std::vector<double> amp(static_cast<std::size_t>(length / 2), 0.0);
    // Up to where the loop, played up to an octave higher, stays under half the rate.
    const double top = 0.45 * rate / std::exp2(12.5 / 12.0);
    const double binHz = rate / length;
    for (int n = 1; n < 4000; ++n) {
        const double r = ratio(spectrum, n);
        if (r <= 0.0 || r * f0 >= top) break;
        const double a = amplitude(spectrum, n, f0);
        if (a <= 0.0) continue;
        const double centre = r * f0;
        const double bandHz = (std::exp2(bandwidthCents / 1200.0) - 1.0) * f0 * std::pow(n, widening(spectrum));
        const double width = std::max(bandHz, binHz); // (a Gaussian of this width, at least a bin)
        const int from = std::max(1, static_cast<int>((centre - 4.0 * width) / binHz));
        const int to = std::min(length / 2 - 1, static_cast<int>((centre + 4.0 * width) / binHz) + 1);
        for (int i = from; i <= to; ++i) {
            const double x = (i * binHz - centre) / width;
            amp.at(static_cast<std::size_t>(i)) += a * std::exp(-x * x) / std::sqrt(width);
        }
    }
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> phase(0.0, 2.0 * std::numbers::pi);
    std::vector<Complex> spectrumBins(static_cast<std::size_t>(length), Complex(0.0, 0.0));
    for (int i = 1; i < length / 2; ++i) {
        const Complex value = std::polar(amp.at(static_cast<std::size_t>(i)), phase(random));
        spectrumBins.at(static_cast<std::size_t>(i)) = value;
        spectrumBins.at(static_cast<std::size_t>(length - i)) = std::conj(value); // (a real wave)
    }
    inverseFft(spectrumBins);
    // As loud as each other, whatever the note: the same RMS.
    const double sum = std::accumulate(spectrumBins.begin(), spectrumBins.end(), 0.0,
                                       [](double total, const Complex& c) { return total + (c.real() * c.real()); });
    const double scale = sum > 0.0 ? 0.25 / std::sqrt(sum / length) : 0.0;
    std::vector<float> loop(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i) loop.at(static_cast<std::size_t>(i)) = static_cast<float>(spectrumBins.at(static_cast<std::size_t>(i)).real() * scale);
    return loop;
}

} // namespace

int SpectralTables::pick(double noteHz) const
{
    int best = 0;
    for (int i = 0; i < kCount; ++i) {
        if (baseHz.at(static_cast<std::size_t>(i)) <= noteHz * 1.0001) best = i;
    }
    return best;
}

std::shared_ptr<const SpectralTables> spectralTables(Spectrum spectrum, double bandwidthCents, double sampleRate)
{
    using Key = std::tuple<int, int, int>;
    static std::mutex lock;
    static std::map<Key, std::shared_ptr<const SpectralTables>> built;
    const Key key{static_cast<int>(spectrum), static_cast<int>(std::lround(bandwidthCents * 10.0)), static_cast<int>(std::lround(sampleRate))};
    const std::scoped_lock guard(lock);
    if (const auto found = built.find(key); found != built.end()) return found->second;
    auto tables = std::make_shared<SpectralTables>();
    tables->sampleRate = sampleRate;
    // Each loop on its own thread (each writes only its own slot).
    std::vector<std::thread> builders;
    for (int i = 0; i < SpectralTables::kCount; ++i) {
        const int note = SpectralTables::kLowestNote + i * SpectralTables::kStep;
        const double hz = 440.0 * std::exp2((note - 69) / 12.0);
        tables->baseHz.at(static_cast<std::size_t>(i)) = hz;
        builders.emplace_back([&tables, i, hz, spectrum, bandwidthCents, sampleRate] {
            tables->loops.at(static_cast<std::size_t>(i)) =
                buildLoop(spectrum, bandwidthCents, hz, sampleRate, 0x5EED0000U + static_cast<uint32_t>(spectrum) * 64U + static_cast<uint32_t>(i));
        });
    }
    for (std::thread& b : builders) b.join();
    built.emplace(key, tables);
    return tables;
}

} // namespace gigchain::engine::pads
