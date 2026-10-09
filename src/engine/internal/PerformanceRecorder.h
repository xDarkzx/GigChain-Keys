#pragma once

#include "gigchain/core/Error.h"

#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace gigchain::engine {

// Records what the audience hears (the mix, after the safety limiter) to a
// WAV file: the audio thread hands over each block (lock-free, no
// allocation), a thread of its own writes it to the disk. 32-bit float, stereo.
class PerformanceRecorder
{
public:
    PerformanceRecorder();
    ~PerformanceRecorder();
    PerformanceRecorder(const PerformanceRecorder&) = delete;
    PerformanceRecorder& operator=(const PerformanceRecorder&) = delete;
    PerformanceRecorder(PerformanceRecorder&&) = delete;
    PerformanceRecorder& operator=(PerformanceRecorder&&) = delete;

    // Main thread. Starts recording to `path` at `sampleRate`; a file that
    // cannot be written is an error (also logged).
    core::Result<void> start(const QString& path, double sampleRate);
    // Main thread. Stops, writes what is left and finishes the file. Returns
    // its seconds; a problem writing it (a full disk) is an error.
    core::Result<double> stop();
    [[nodiscard]] bool recording() const { return m_recording.load(std::memory_order_acquire); }
    [[nodiscard]] QString filePath() const { return m_path; }
    // Main thread: true once when writing failed (the disk is full) and recording stopped.
    bool takeFailed() { return m_failed.exchange(false, std::memory_order_relaxed); }

    // Audio thread: the block leaving the app. Real-time safe.
    void push(const float* left, const float* right, int frames) noexcept;

private:
    void writeUntilStopped(); // the writer thread
    // Writes the frames waiting (writer thread); false when the disk failed.
    bool drain();

    std::vector<float> m_ring; // interleaved stereo frames
    std::size_t m_capacity = 0; // frames
    std::atomic<std::size_t> m_written{0}; // frames pushed (audio thread)
    std::atomic<std::size_t> m_read{0};    // frames written to the disk (writer thread)
    std::atomic<uint64_t> m_overruns{0};   // frames lost because the disk fell behind
    std::atomic<bool> m_recording{false};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_failed{false};
    std::thread m_thread;
    struct File;
    std::unique_ptr<File> m_file;
    QString m_path;
    QString m_error; // what went wrong writing (writer thread, read after it ends)
    double m_rate = 48000.0;
};

} // namespace gigchain::engine
