#pragma once

#include "gigchain/core/Error.h"

#include <QString>

#include <atomic>
#include <cstdint>
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

} // namespace gigchain::engine
