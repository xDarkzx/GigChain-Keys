#pragma once

#include "gigchain/core/Error.h"

#include <QString>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <numeric>
#include <vector>

namespace gigchain::engine {

// A whole audio file in memory, stereo, at the rate it will play at.
struct AudioClip
{
    QString path;
    double sampleRate = 0.0;
    std::vector<float> left;
    std::vector<float> right;

    [[nodiscard]] int64_t frames() const { return static_cast<int64_t>(left.size()); }
    [[nodiscard]] double seconds() const { return sampleRate > 0.0 ? static_cast<double>(frames()) / sampleRate : 0.0; }
};

// Reads an audio file (WAV, MP3, FLAC, AAC/M4A, OGG: what Qt Multimedia's
// FFmpeg backend reads) into a clip at `sampleRate`, converted to stereo
// floats and resampled as needed. Blocks until done; call it on a worker
// thread (it runs its own event loop). `cancel` set: stops early with an
// error. The file's problems (missing, unreadable, too long) are errors.
core::Result<AudioClip> decodeAudioFile(const QString& path, double sampleRate, const std::atomic<bool>* cancel = nullptr);

// The longest file read: 20 minutes (a stereo float copy of that is ~460 MB at 48 kHz).
inline constexpr double kMaxClipSeconds = 20.0 * 60.0;
// A song's backing track and stems together: at most 45 minutes of audio
// (~1 GB at 48 kHz; 8 stems of a 5-minute song), all of it in memory.
inline constexpr double kMaxTrackSetSeconds = 45.0 * 60.0;

// A song's backing track and its stems, read together: clip 0 is the track
// (empty when there is none), then the stems in order. A file that could
// not be read is an empty clip.
struct TrackSet
{
    std::vector<QString> paths; // what was asked for, in the same order
    std::vector<AudioClip> clips;
    double sampleRate = 0.0;

    // The longest clip's length: the set plays until it ends.
    [[nodiscard]] int64_t frames() const
    {
        return std::accumulate(clips.begin(), clips.end(), int64_t{0},
                               [](int64_t longest, const AudioClip& clip) { return std::max(longest, clip.frames()); });
    }
};

// A finished read: the set, and each file's problem (logged by the reader).
struct TrackRead
{
    TrackSet set;
    std::vector<QString> problems;
    std::vector<QString> failed; // the files that could not be read
};

// Reads `paths` ("" = none) into a set at `sampleRate`, keeping to
// kMaxTrackSetSeconds in all (the files past that are left out, said).
// Worker thread, as decodeAudioFile.
TrackRead readTrackSet(const std::vector<QString>& paths, double sampleRate, const std::atomic<bool>* cancel = nullptr);

} // namespace gigchain::engine
