#pragma once

#include "gigchain/core/Error.h"

#include <QString>

#include <atomic>
#include <cstdint>
#include <vector>

// Piano Pro's recorded pianos (VCSL, CC0): an SFZ map and its recordings,
// read into memory at the playing rate, 16-bit, each trimmed to a length
// that suits its register and faded out there.
namespace gigchain::engine {

// One recording and where it plays.
struct SampleZone
{
    int loKey = 0, hiKey = 127, keyCenter = 60;
    int loVel = 0, hiVel = 127;
    double tuneCents = 0.0;
    float gain = 1.0F; // the map's volume and global volume
    double attack = 0.0, decay = 0.0, sustain = 1.0, release = 0.001; // seconds; sustain 0-1
    double rtDecayDb = 0.0; // release recordings: dB quieter per second the key was held
    int seqLength = 1, seqPosition = 1;
    std::vector<int16_t> frames; // stereo, left then right, from where the map says it starts

    [[nodiscard]] int64_t length() const { return static_cast<int64_t>(frames.size() / 2); }
};

struct SampleSet
{
    QString name; // the map's file name without .sfz
    double sampleRate = 0.0;
    float velocityTrack = 1.0F; // 0-1: how much how hard a key is played changes the level
    std::vector<SampleZone> attack;  // played when a key goes down
    std::vector<SampleZone> release; // played when it is let go

    // The recording for `note` played `velocity` hard (1-127), the release one
    // when `release`; `roundRobin` counts the times the note was played (for
    // maps that alternate recordings). nullptr: nothing there.
    [[nodiscard]] const SampleZone* find(int note, int velocity, bool release, int roundRobin) const;
    // Memory the recordings take.
    [[nodiscard]] std::size_t bytes() const;
};

struct PianoLoadOptions
{
    int maxLayers = 3;            // loudness layers kept per note (spread over those recorded)
    double longestSeconds = 8.0;  // the lowest key's length (low strings ring longest)
    double shortestSeconds = 3.0; // the highest key's
    double fadeSeconds = 0.5;     // the fade at the end of each trimmed recording
};

// Reads the map at `sfzPath` and its recordings at `sampleRate`. Worker
// thread (it decodes). Any problem (the map, a recording missing or
// unreadable): an error naming the file. `cancel` set: stops with an error.
[[nodiscard]] core::Result<SampleSet> loadSampleSet(const QString& sfzPath, double sampleRate, const PianoLoadOptions& options = {},
                                                    const std::atomic<bool>* cancel = nullptr);

// Where the bundled pianos are: $GIGCHAIN_PIANO_SAMPLES when set, else
// samples/piano next to the app.
[[nodiscard]] QString pianoSampleFolder();

} // namespace gigchain::engine
