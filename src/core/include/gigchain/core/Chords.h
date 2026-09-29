#pragma once

#include <QString>

#include <cstdint>
#include <optional>

namespace gigchain::core {

// What a chord name means, for following what is played. Pitch classes
// 0-11 (C = 0).
struct ChordShape
{
    int root = 0;        // pitch class
    uint16_t family = 0; // every note the name gives, root included: bit n = pitch class n
    int bass = -1;       // a written slash bass ("D/E": E); -1 = none
    int third = -1;      // semitones above the root: 3 minor, 4 major; -1 = none (sus, 5, no3)
    int colour = -1;     // without a third, what stands in: 2 or 5 (sus2, sus4), 7 (a 5 chord); else -1

    friend bool operator==(const ChordShape&, const ChordShape&) = default;
};

// The chord a name like "G#m7", "Bbmaj7/D", "Csus", "E5" or "F#m7b5"
// means. A root A-G with # or b, then m/min/-, maj/M/Δ, dim/°, aug/+, ø,
// sus2/sus4/sus, 5, 6, 7, 9, 11, 13, add9/add11, no3, b5 #5 b9 #9 #11 b13
// and /bass, in any order; anything else after a clear root is skipped.
// nullopt when there is no clear root (a typo, "N.C.", a word).
[[nodiscard]] std::optional<ChordShape> parseChordName(const QString& name);

} // namespace gigchain::core
