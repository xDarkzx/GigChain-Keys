#pragma once

#include "gigchain/core/Error.h"

#include <QString>

#include <cstdint>
#include <vector>

// Reading an SFZ map (sfzformat.com): which recording plays for which keys
// and how hard, and how. Only what Piano Pro's pianos use is read; anything
// else is refused with its line, so no setting is ever silently ignored.
namespace gigchain::engine::sfz {

// One <region>, with its <global> and <group> settings inherited.
struct Region
{
    QString sample; // relative to the map's folder, '/' between folders
    int loKey = 0, hiKey = 127, keyCenter = 60;
    int loVel = 0, hiVel = 127;
    double tuneCents = 0.0;
    double volumeDb = 0.0;
    double globalVolumeDb = 0.0;
    double ampVeltrack = 100.0; // percent: how much how hard a key is played changes the level
    int64_t offset = 0;         // where the recording starts (its own frames)
    bool release = false;       // trigger=release: played when the key is let go
    double attack = 0.0, decay = 0.0, sustainPercent = 100.0, releaseSeconds = 0.001;
    double rtDecayDb = 0.0; // a release recording is this many dB quieter per second the key was held
    int seqLength = 1, seqPosition = 1; // round robin
};

// The regions of `text`, in order. A setting not known here, a value that
// is not a number, or a region without a sample: an error naming the line.
[[nodiscard]] core::Result<std::vector<Region>> parse(const QString& text);

} // namespace gigchain::engine::sfz
