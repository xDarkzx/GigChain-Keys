#include "AudioFile.h"

#include "EngineLog.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

// How long a decoder may say nothing (no audio, no end, no error) before it
// is given up on. Qt's FFmpeg decoder can fail to set up its resampler and
// then go silent for good; waiting on it held a piano's read, and quitting,
// forever.
constexpr qint64 kSilentLimitMs = 10'000;

struct DecodeEnd
{
    QString problem; // what went wrong, or empty
    bool stalled = false; // the decoder went silent (worth one more try)
};

// Runs `decoder` on `path` until it finishes, fails, or `onBuffer` says to
// stop (returns false). Never waits forever: `cancel` stops it within a tenth
// of a second, and a decoder silent for kSilentLimitMs is stopped as stalled.
DecodeEnd runDecoder(QAudioDecoder& decoder, const QString& path, const std::atomic<bool>* cancel,
                     const std::function<bool(const QAudioBuffer&)>& onBuffer)
{
    DecodeEnd end;
    QString& problem = end.problem;
    decoder.setSource(QUrl::fromLocalFile(path));
    QEventLoop loop;
    QElapsedTimer quiet;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        quiet.restart();
        while (decoder.bufferAvailable()) {
            const QAudioBuffer buffer = decoder.read();
            if (buffer.isValid() && !onBuffer(buffer)) {
                decoder.stop();
                loop.quit();
                return;
            }
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, [&](QAudioDecoder::Error) {
        problem = decoder.errorString().isEmpty() ? u"it could not be decoded"_s : decoder.errorString();
        loop.quit();
    });
    QTimer watch;
    watch.setInterval(100);
    QObject::connect(&watch, &QTimer::timeout, &loop, [&] {
        const bool cancelled = cancel != nullptr && cancel->load(std::memory_order_relaxed);
        if (!cancelled && !quiet.hasExpired(kSilentLimitMs)) return;
        decoder.stop();
        // (after stop(), which may say an error of its own: this is the cause)
        problem = cancelled ? u"reading it was cancelled"_s : u"the decoder stopped answering"_s;
        end.stalled = !cancelled;
        loop.quit();
    });
    decoder.start();
    if (decoder.error() != QAudioDecoder::NoError) {
        problem = decoder.errorString();
        return end;
    }
    quiet.start();
    watch.start();
    loop.exec();
    return end;
}

} // namespace

namespace {

// One try at decodeAudioFile(); `stalled` set when the decoder went silent.
core::Result<AudioClip> decodeAudioFileOnce(const QString& path, double sampleRate, const std::atomic<bool>* cancel, bool& stalled)
{
    if (!QFileInfo::exists(path)) return core::fail(core::ErrorCode::FileNotFound, u"Backing track not found: %1"_s.arg(path));
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
        return core::fail(core::ErrorCode::InvalidData, u"Cannot read %1 without an audio output running"_s.arg(path));
    }
    const QString name = QFileInfo(path).fileName();

    // The file's own channel count first: asked for stereo, FFmpeg turns a
    // mono file 3 dB down on each side; a mono track should play on both
    // sides at its own level.
    int sourceChannels = 0;
    {
        QAudioDecoder probe;
        const DecodeEnd probed = runDecoder(probe, path, cancel, [&sourceChannels](const QAudioBuffer& buffer) {
            sourceChannels = buffer.format().channelCount();
            return false; // one buffer tells
        });
        stalled = probed.stalled;
        const QString& problem = probed.problem;
        if (sourceChannels <= 0) {
            return core::fail(core::ErrorCode::InvalidData,
                              u"Cannot play %1: %2"_s.arg(name, problem.isEmpty() ? u"it holds no audio"_s : problem));
        }
    }
    const int channels = std::min(sourceChannels, 2); // more than two are mixed down to stereo

    QAudioFormat format;
    format.setSampleRate(static_cast<int>(std::lround(sampleRate)));
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Float);

    AudioClip clip;
    clip.path = path;
    clip.sampleRate = sampleRate;
    const auto maxFrames = static_cast<std::size_t>(kMaxClipSeconds * sampleRate);
    QString stopped;

    QAudioDecoder decoder;
    decoder.setAudioFormat(format);
    const DecodeEnd decoded = runDecoder(decoder, path, cancel, [&](const QAudioBuffer& buffer) {
        if (buffer.format().sampleFormat() != QAudioFormat::Float || buffer.format().channelCount() != channels) {
            stopped = u"it came out in an unexpected sample format"_s;
            return false;
        }
        const std::span<const float> samples(buffer.constData<float>(), static_cast<std::size_t>(buffer.sampleCount()));
        const auto step = static_cast<std::size_t>(channels);
        for (std::size_t i = 0; i + step - 1 < samples.size(); i += step) {
            const float left = samples.subspan(i).front();
            clip.left.push_back(left);
            clip.right.push_back(channels == 2 ? samples.subspan(i + 1).front() : left); // mono: both sides
        }
        if (clip.left.size() > maxFrames) {
            stopped = u"it is longer than %1 minutes"_s.arg(kMaxClipSeconds / 60.0);
            return false;
        }
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            stopped = u"reading it was cancelled"_s;
            return false;
        }
        return true;
    });
    stalled = decoded.stalled;
    QString problem = decoded.problem;
    if (!stopped.isEmpty()) problem = stopped;

    if (!problem.isEmpty()) return core::fail(core::ErrorCode::InvalidData, u"Cannot play %1: %2"_s.arg(name, problem));
    if (clip.left.empty()) return core::fail(core::ErrorCode::InvalidData, u"Cannot play %1: it holds no audio"_s.arg(name));
    qCInfo(lcEngine).noquote() << "Backing track read:" << path << "," << clip.seconds() << "s at" << sampleRate << "Hz,"
                               << (sourceChannels == 1 ? "mono" : "stereo");
    return clip;
}

// One try at decodeSampleFile(); `stalled` set when the decoder went silent.
core::Result<SampleClip> decodeSampleFileOnce(const QString& path, double sampleRate, double keepSeconds, const std::atomic<bool>* cancel,
                                              bool& stalled)
{
    const QString name = QFileInfo(path).fileName();
    if (!QFileInfo::exists(path)) return core::fail(core::ErrorCode::FileNotFound, u"Sample not found: %1"_s.arg(path));
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || !std::isfinite(keepSeconds) || keepSeconds <= 0.0) {
        return core::fail(core::ErrorCode::InvalidData, u"Cannot read %1 without an audio output running"_s.arg(name));
    }
    // Its own channels and rate first (one buffer tells).
    int sourceChannels = 0;
    double sourceRate = 0.0;
    {
        QAudioDecoder probe;
        const DecodeEnd probed = runDecoder(probe, path, cancel, [&](const QAudioBuffer& buffer) {
            sourceChannels = buffer.format().channelCount();
            sourceRate = buffer.format().sampleRate();
            return false;
        });
        stalled = probed.stalled;
        const QString& problem = probed.problem;
        if (sourceChannels <= 0 || sourceRate <= 0.0) {
            return core::fail(core::ErrorCode::InvalidData,
                              u"Cannot read the sample %1: %2"_s.arg(name, problem.isEmpty() ? u"it holds no audio"_s : problem));
        }
    }
    const int channels = std::min(sourceChannels, 2);
    QAudioFormat format;
    format.setSampleRate(static_cast<int>(std::lround(sampleRate)));
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Float);

    SampleClip read;
    read.sourceRate = sourceRate;
    read.clip.path = path;
    read.clip.sampleRate = sampleRate;
    const auto keepFrames = static_cast<std::size_t>(keepSeconds * sampleRate);
    QString stopped;
    QAudioDecoder decoder;
    decoder.setAudioFormat(format);
    const DecodeEnd decoded = runDecoder(decoder, path, cancel, [&](const QAudioBuffer& buffer) {
        if (buffer.format().sampleFormat() != QAudioFormat::Float || buffer.format().channelCount() != channels) {
            stopped = u"it came out in an unexpected sample format"_s;
            return false;
        }
        const std::span<const float> samples(buffer.constData<float>(), static_cast<std::size_t>(buffer.sampleCount()));
        const auto step = static_cast<std::size_t>(channels);
        for (std::size_t i = 0; i + step - 1 < samples.size() && read.clip.left.size() < keepFrames; i += step) {
            const float left = samples.subspan(i).front();
            read.clip.left.push_back(left);
            read.clip.right.push_back(channels == 2 ? samples.subspan(i + 1).front() : left);
        }
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            stopped = u"reading it was cancelled"_s;
            return false;
        }
        return read.clip.left.size() < keepFrames; // enough: the rest is never decoded
    });
    stalled = decoded.stalled;
    QString problem = decoded.problem;
    if (!stopped.isEmpty()) problem = stopped;
    if (!problem.isEmpty()) return core::fail(core::ErrorCode::InvalidData, u"Cannot read the sample %1: %2"_s.arg(name, problem));
    if (read.clip.left.empty()) return core::fail(core::ErrorCode::InvalidData, u"Cannot read the sample %1: it holds no audio"_s.arg(name));
    return read;
}

} // namespace

// A stall is the decoder's, not the file's (the same file reads the next
// time): it is tried once more before it counts as a failure.
core::Result<AudioClip> decodeAudioFile(const QString& path, double sampleRate, const std::atomic<bool>* cancel)
{
    bool stalled = false;
    auto clip = decodeAudioFileOnce(path, sampleRate, cancel, stalled);
    if (clip || !stalled) return clip;
    qCWarning(lcEngine).noquote() << clip.error().message << "- trying it once more";
    return decodeAudioFileOnce(path, sampleRate, cancel, stalled);
}

core::Result<SampleClip> decodeSampleFile(const QString& path, double sampleRate, double keepSeconds, const std::atomic<bool>* cancel)
{
    bool stalled = false;
    auto read = decodeSampleFileOnce(path, sampleRate, keepSeconds, cancel, stalled);
    if (read || !stalled) return read;
    qCWarning(lcEngine).noquote() << read.error().message << "- trying it once more";
    return decodeSampleFileOnce(path, sampleRate, keepSeconds, cancel, stalled);
}

TrackRead readTrackSet(const std::vector<QString>& paths, double sampleRate, const std::atomic<bool>* cancel)
{
    TrackRead read;
    read.set.paths = paths;
    read.set.sampleRate = sampleRate;
    read.set.clips.resize(paths.size());
    const auto budget = static_cast<int64_t>(kMaxTrackSetSeconds * sampleRate);
    int64_t used = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const QString& path = paths.at(i);
        if (path.isEmpty()) continue;
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) break;
        auto clip = decodeAudioFile(path, sampleRate, cancel);
        if (!clip) {
            qCWarning(lcEngine).noquote() << clip.error().message;
            read.problems.push_back(clip.error().message);
            read.failed.push_back(path);
            continue;
        }
        if (used + clip->frames() > budget) {
            const QString why = u"Left out %1: a song's tracks together may be at most %2 minutes of audio (they are all kept in memory)"_s
                                    .arg(QFileInfo(path).fileName())
                                    .arg(kMaxTrackSetSeconds / 60.0);
            qCWarning(lcEngine).noquote() << why;
            read.problems.push_back(why);
            read.failed.push_back(path);
            continue;
        }
        used += clip->frames();
        clip->path = path;
        read.set.clips.at(i) = std::move(*clip);
    }
    return read;
}

} // namespace gigchain::engine
