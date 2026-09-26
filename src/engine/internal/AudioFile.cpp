#include "AudioFile.h"

#include "EngineLog.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

// Runs `decoder` on `path` until it finishes, fails, or `onBuffer` says to
// stop (returns false). Returns what went wrong, or an empty string.
QString runDecoder(QAudioDecoder& decoder, const QString& path, const std::function<bool(const QAudioBuffer&)>& onBuffer)
{
    QString problem;
    decoder.setSource(QUrl::fromLocalFile(path));
    QEventLoop loop;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
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
    decoder.start();
    if (decoder.error() != QAudioDecoder::NoError) return decoder.errorString();
    loop.exec();
    return problem;
}

} // namespace

core::Result<AudioClip> decodeAudioFile(const QString& path, double sampleRate, const std::atomic<bool>* cancel)
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
        const QString problem = runDecoder(probe, path, [&sourceChannels](const QAudioBuffer& buffer) {
            sourceChannels = buffer.format().channelCount();
            return false; // one buffer tells
        });
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
    QString problem = runDecoder(decoder, path, [&](const QAudioBuffer& buffer) {
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
    if (!stopped.isEmpty()) problem = stopped;

    if (!problem.isEmpty()) return core::fail(core::ErrorCode::InvalidData, u"Cannot play %1: %2"_s.arg(name, problem));
    if (clip.left.empty()) return core::fail(core::ErrorCode::InvalidData, u"Cannot play %1: it holds no audio"_s.arg(name));
    qCInfo(lcEngine).noquote() << "Backing track read:" << path << "," << clip.seconds() << "s at" << sampleRate << "Hz,"
                               << (sourceChannels == 1 ? "mono" : "stereo");
    return clip;
}

} // namespace gigchain::engine
