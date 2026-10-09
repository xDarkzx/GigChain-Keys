#include "PerformanceRecorder.h"

#include "EngineLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <chrono>
#include <span>
#include <string_view>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

namespace {

constexpr std::size_t kRingSeconds = 10; // what the disk may fall behind by
constexpr int kChannels = 2;
constexpr int kHeaderBytes = 44;

void put32(std::array<char, kHeaderBytes>& header, std::size_t at, uint32_t value)
{
    qToLittleEndian(value, std::span(header).subspan(at, 4).data());
}
void put16(std::array<char, kHeaderBytes>& header, std::size_t at, uint16_t value)
{
    qToLittleEndian(value, std::span(header).subspan(at, 2).data());
}
void putTag(std::array<char, kHeaderBytes>& header, std::size_t at, std::string_view tag)
{
    std::ranges::copy(tag, std::span(header).subspan(at, tag.size()).begin());
}

// A WAV header for 32-bit float stereo at `rate` with `dataBytes` of sound.
std::array<char, kHeaderBytes> waveHeader(uint32_t rate, uint32_t dataBytes)
{
    constexpr uint32_t kFrameBytes = kChannels * sizeof(float);
    std::array<char, kHeaderBytes> h{};
    putTag(h, 0, "RIFF");
    put32(h, 4, 36 + dataBytes);
    putTag(h, 8, "WAVEfmt ");
    put32(h, 16, 16);              // fmt chunk size
    put16(h, 20, 3);               // IEEE float
    put16(h, 22, kChannels);
    put32(h, 24, rate);
    put32(h, 28, rate * kFrameBytes); // bytes per second
    put16(h, 32, kFrameBytes);     // block align
    put16(h, 34, 32);              // bits per sample
    putTag(h, 36, "data");
    put32(h, 40, dataBytes);
    return h;
}

} // namespace

struct PerformanceRecorder::File
{
    QFile file;
    uint64_t dataBytes = 0;
};

PerformanceRecorder::PerformanceRecorder() = default;

PerformanceRecorder::~PerformanceRecorder()
{
    if (m_thread.joinable()) (void)stop(); // a problem is logged by stop
}

core::Result<void> PerformanceRecorder::start(const QString& path, double sampleRate)
{
    if (m_thread.joinable()) return core::fail(core::ErrorCode::InvalidData, u"Already recording to %1"_s.arg(m_path));
    if (sampleRate <= 0.0) return core::fail(core::ErrorCode::DeviceUnavailable, u"Nothing to record: the audio is not running"_s);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        const QString why = u"Could not make the folder for the recording: %1"_s.arg(QFileInfo(path).absolutePath());
        qCWarning(lcEngine).noquote() << why;
        return core::fail(core::ErrorCode::FileWriteFailed, why);
    }
    auto file = std::make_unique<File>();
    file->file.setFileName(path);
    const auto header = waveHeader(static_cast<uint32_t>(sampleRate), 0);
    if (!file->file.open(QIODevice::WriteOnly) || file->file.write(header.data(), header.size()) != kHeaderBytes) {
        const QString why = u"Could not write the recording %1: %2"_s.arg(path, file->file.errorString());
        qCWarning(lcEngine).noquote() << why;
        return core::fail(core::ErrorCode::FileWriteFailed, why);
    }
    m_rate = sampleRate;
    m_capacity = static_cast<std::size_t>(sampleRate) * kRingSeconds;
    m_ring.assign(m_capacity * kChannels, 0.0F);
    m_written.store(0, std::memory_order_relaxed);
    m_read.store(0, std::memory_order_relaxed);
    m_overruns.store(0, std::memory_order_relaxed);
    m_file = std::move(file);
    m_path = path;
    m_error.clear();
    m_stop.store(false, std::memory_order_relaxed);
    m_thread = std::thread([this] { writeUntilStopped(); });
    m_recording.store(true, std::memory_order_release);
    qCInfo(lcEngine).noquote() << "Recording the performance to" << path;
    return {};
}

void PerformanceRecorder::push(const float* left, const float* right, int frames) noexcept
{
    if (!m_recording.load(std::memory_order_acquire) || frames <= 0) return;
    const std::size_t written = m_written.load(std::memory_order_relaxed);
    const std::size_t read = m_read.load(std::memory_order_acquire);
    const auto count = static_cast<std::size_t>(frames);
    if (written - read + count > m_capacity) { // the disk fell behind: these frames are lost (counted, said)
        m_overruns.fetch_add(count, std::memory_order_relaxed);
        return;
    }
    const std::span<float> ring(m_ring);
    const std::span<const float> l(left, count);
    const std::span<const float> r(right, count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = ((written + i) % m_capacity) * kChannels;
        ring.subspan(at, 2).front() = l.subspan(i, 1).front();
        ring.subspan(at + 1, 1).front() = r.subspan(i, 1).front();
    }
    m_written.store(written + count, std::memory_order_release);
}

bool PerformanceRecorder::drain()
{
    const std::size_t written = m_written.load(std::memory_order_acquire);
    std::size_t read = m_read.load(std::memory_order_relaxed);
    while (read < written) {
        const std::size_t at = read % m_capacity;
        const std::size_t frames = std::min(written - read, m_capacity - at); // up to the ring's end
        const auto bytes = static_cast<qint64>(frames * kChannels * sizeof(float));
        const char* from = reinterpret_cast<const char*>(std::span<const float>(m_ring).subspan(at * kChannels).data()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): raw bytes to a file
        if (m_file->file.write(from, bytes) != bytes) {
            m_error = u"Could not write the recording %1: %2"_s.arg(m_path, m_file->file.errorString());
            return false;
        }
        m_file->dataBytes += static_cast<uint64_t>(bytes);
        read += frames;
        m_read.store(read, std::memory_order_release);
    }
    return true;
}

void PerformanceRecorder::writeUntilStopped()
{
    while (!m_stop.load(std::memory_order_acquire)) {
        if (!drain()) {
            m_recording.store(false, std::memory_order_release);
            m_failed.store(true, std::memory_order_relaxed);
            qCWarning(lcEngine).noquote() << m_error;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!drain()) qCWarning(lcEngine).noquote() << m_error;
}

core::Result<double> PerformanceRecorder::stop()
{
    if (!m_thread.joinable()) return core::fail(core::ErrorCode::InvalidData, u"Not recording"_s);
    m_recording.store(false, std::memory_order_release);
    m_stop.store(true, std::memory_order_release);
    m_thread.join();
    // The header's sizes, now known (a WAV holds up to 4 GB: about 3 hours here).
    const auto dataBytes = static_cast<uint32_t>(std::min<uint64_t>(m_file->dataBytes, 0xFFFFFFFFULL - 36));
    const auto header = waveHeader(static_cast<uint32_t>(m_rate), dataBytes);
    const bool finished = m_file->file.seek(0) && m_file->file.write(header.data(), header.size()) == kHeaderBytes;
    m_file->file.close();
    const double seconds = static_cast<double>(m_file->dataBytes) / (kChannels * sizeof(float)) / m_rate;
    if (const uint64_t lost = m_overruns.exchange(0, std::memory_order_relaxed); lost > 0) {
        qCWarning(lcEngine) << "The recording lost" << lost << "frames: the disk could not keep up";
    }
    if (!m_error.isEmpty()) return core::fail(core::ErrorCode::FileWriteFailed, m_error);
    if (!finished) {
        const QString why = u"Could not finish the recording %1: %2"_s.arg(m_path, m_file->file.errorString());
        qCWarning(lcEngine).noquote() << why;
        return core::fail(core::ErrorCode::FileWriteFailed, why);
    }
    qCInfo(lcEngine).noquote() << "Recorded" << seconds << "seconds to" << m_path;
    return seconds;
}

} // namespace gigchain::engine
