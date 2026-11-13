#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// Pads Pro's spectral body: the PADsynth idea (Nasca Octavian Paul, public
// domain: https://zynaddsubfx.sourceforge.io/doc/PADsynth/PADsynth.htm),
// our own code. A pad's wave is made in the frequency domain: every
// harmonic of the note becomes a soft band (a Gaussian, wider the higher
// the harmonic), each part of it at a random phase; one inverse FFT turns
// that into a long loop that never repeats audibly. What it sounds like:
// an ensemble or a choir of countless voices, sweet and wide, with no
// beating and nothing harsh. The spectra (the "aah" with its formants
// fixed in frequency as a voice's are, the strings, the air, the glass)
// are our own.
//
// Loops are built when the sound is prepared (allocating, the main
// thread, each loop of a set on its own thread), one for every octave,
// and shared by every Pads Pro that plays them; playing only reads them.
namespace gigchain::engine::pads {

enum class Spectrum : uint8_t
{
    Choir,   // a soft "aah": harmonics under a voice's formants (about 800, 1150, 2900 Hz)
    Strings, // a bowed ensemble: a saw's harmonics, gently rolled off, airy
    Air,     // breath: a few low harmonics and wide bands high up
    Glass,   // a glass harmonica: few, pure partials, wide and shimmering
    Reed,    // a reed: the odd harmonics strong, the even ones faint (hollow and woody)
    Hollow,  // a flute or a soft sine floor: odd harmonics falling fast (round, no edge)
    Ooh,     // a soft "ooh": a voice's darker vowel (about 320, 800, 2300 Hz)
    Organ,   // drawbars: the note, its octaves and its fifths, nothing between
    Bell,    // a struck bell's partials held as a pad (not in tune with the note's harmonics: shimmering, metallic)
    Metal,   // bowed metal: partials stretched wider and wider apart (a singing bowl, a bowed cymbal)
    Wind,    // pitched wind: every harmonic, falling softly (with wide bands: a breathy, noisy cloud on the note)
    Piano,   // a struck string: partials stretched sharp as a piano's stiff strings are, the hammer's notch near the 7th
    Principal, // an organ's principal pipe: the note strong, its harmonics falling steadily
};

// One spectrum's loops, one for every octave.
struct SpectralTables
{
    static constexpr int kLength = 1 << 17;    // samples a loop (about 2.7 s at 48 kHz)
    static constexpr int kLowestNote = 24;     // C1
    static constexpr int kStep = 12;           // semitones between loops
    static constexpr int kCount = 8;           // C1 .. C8
    double sampleRate = 48000.0;
    std::array<double, kCount> baseHz{};       // each loop's note
    std::array<std::vector<float>, kCount> loops;

    // The loop to play `noteHz` from (the nearest at or below it).
    [[nodiscard]] int pick(double noteHz) const;
};

// Each spectrum's loops at `sampleRate` (built at the first ask, then shared).
// `bandwidthCents`: how wide each harmonic's band is (a choir 40-60, strings 20-30).
[[nodiscard]] std::shared_ptr<const SpectralTables> spectralTables(Spectrum spectrum, double bandwidthCents, double sampleRate);

} // namespace gigchain::engine::pads
