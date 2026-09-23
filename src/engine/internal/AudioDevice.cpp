#include "AudioDevice.h"

#include "EngineLog.h"

#include <RtAudio.h>

#include <xmmintrin.h>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace openstage::engine {
namespace {

RtAudio::Api toRtApi(AudioApi api)
{
    return api == AudioApi::Asio ? RtAudio::WINDOWS_ASIO : RtAudio::WINDOWS_WASAPI;
}

// An RtAudio instance for listing devices; its complaints go straight to the log.
std::unique_ptr<RtAudio> probe(AudioApi api)
{
    return std::make_unique<RtAudio>(toRtApi(api), [api](RtAudioErrorType type, const std::string& text) {
        if (type == RTAUDIO_WARNING) {
            qCInfo(lcEngine).noquote() << apiName(api) << "probe:" << QString::fromStdString(text);
        } else {
            qCWarning(lcEngine).noquote() << apiName(api) << "probe:" << QString::fromStdString(text);
        }
    });
}

} // namespace

QString apiName(AudioApi api)
{
    return api == AudioApi::Asio ? u"ASIO"_s : u"WASAPI"_s;
}

AudioDevice::AudioDevice() = default;

AudioDevice::~AudioDevice()
{
    close();
}

std::vector<AudioDeviceInfo> AudioDevice::listOutputs()
{
    std::vector<AudioDeviceInfo> outputs;
    for (const AudioApi api : {AudioApi::Wasapi, AudioApi::Asio}) {
        const auto rt = probe(api);
        for (const unsigned int id : rt->getDeviceIds()) {
            const RtAudio::DeviceInfo info = rt->getDeviceInfo(id);
            if (info.outputChannels < 2) continue;
            outputs.push_back(AudioDeviceInfo{api, QString::fromStdString(info.name), static_cast<int>(info.outputChannels),
                                              info.preferredSampleRate, api == AudioApi::Wasapi && info.isDefaultOutput});
        }
    }
    return outputs;
}

core::Result<void> AudioDevice::open(std::optional<DeviceChoice> choice, unsigned int bufferFrames, RenderCallback render)
{
    m_render = std::move(render);
    m_asioRetried = false;
    auto opened = openUnlogged(std::move(choice), bufferFrames);
    if (opened) {
        qCInfo(lcEngine).noquote() << "Audio output:" << m_choice.name << "(" << apiName(m_choice.api) << ")"
                                   << m_sampleRate << "Hz," << m_maxBlock << "frames, latency" << m_latencyMs << "ms";
    } else {
        qCWarning(lcEngine).noquote() << opened.error().message;
    }
    return opened;
}

core::Result<void> AudioDevice::openUnlogged(std::optional<DeviceChoice> choice, unsigned int bufferFrames)
{
    close();
    m_requestedFrames = bufferFrames;
    const AudioApi api = choice ? choice->api : AudioApi::Wasapi;
    auto rt = std::make_unique<RtAudio>(
        toRtApi(api), [this](RtAudioErrorType type, const std::string& text) { onError(type, text); });

    std::optional<unsigned int> deviceId;
    if (!choice) {
        const unsigned int id = rt->getDefaultOutputDevice();
        if (id != 0) deviceId = id;
    } else {
        for (const unsigned int id : rt->getDeviceIds()) {
            const RtAudio::DeviceInfo info = rt->getDeviceInfo(id);
            if (QString::fromStdString(info.name) == choice->name && info.outputChannels >= 2) {
                deviceId = id;
                break;
            }
        }
    }
    if (!deviceId) {
        return core::fail(core::ErrorCode::InvalidData,
                          choice ? u"No audio output named \"%1\" (%2)"_s.arg(choice->name, apiName(api))
                                 : u"This computer has no default audio output"_s);
    }
    const RtAudio::DeviceInfo info = rt->getDeviceInfo(*deviceId);

    RtAudio::StreamParameters output;
    output.deviceId = *deviceId;
    output.nChannels = 2;
    output.firstChannel = 0;
    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
    options.streamName = "OpenStage";
    const unsigned int rate = info.preferredSampleRate != 0 ? info.preferredSampleRate : 48000;
    unsigned int frames = bufferFrames;

    const auto takeLastError = [this](const QString& fallback) {
        const std::lock_guard lock(m_errorMutex);
        QString text = m_errors.empty() ? fallback : m_errors.back();
        m_errors.clear();
        return text;
    };

    if (rt->openStream(&output, nullptr, RTAUDIO_FLOAT32, rate, &frames, &AudioDevice::callback, this, &options) !=
        RTAUDIO_NO_ERROR) {
        return core::fail(core::ErrorCode::DeviceUnavailable,
                          u"Could not open %1 (%2): %3"_s.arg(QString::fromStdString(info.name), apiName(api),
                                                             takeLastError(u"unknown error"_s)));
    }
    m_maxBlock = static_cast<int>(frames);
    m_sampleRate = rt->getStreamSampleRate();
    if (rt->startStream() != RTAUDIO_NO_ERROR) {
        rt->closeStream();
        return core::fail(core::ErrorCode::DeviceUnavailable,
                          u"Could not start %1 (%2): %3"_s.arg(QString::fromStdString(info.name), apiName(api),
                                                              takeLastError(u"unknown error"_s)));
    }
    const double latencyFrames = static_cast<double>(rt->getStreamLatency());
    m_latencyMs = m_sampleRate > 0.0 ? 1000.0 * std::max(latencyFrames, static_cast<double>(frames)) / m_sampleRate : 0.0;
    m_choice = DeviceChoice{api, QString::fromStdString(info.name)};
    m_rtaudio = std::move(rt);
    return {};
}

void AudioDevice::close()
{
    if (!m_rtaudio) return;
    if (m_rtaudio->isStreamRunning() && m_rtaudio->stopStream() != RTAUDIO_NO_ERROR) {
        qCWarning(lcEngine).noquote() << "Stopping" << m_choice.name << "reported an error";
    }
    if (m_rtaudio->isStreamOpen()) m_rtaudio->closeStream();
    m_rtaudio.reset();
}

bool AudioDevice::isOpen() const
{
    return m_rtaudio && m_rtaudio->isStreamOpen();
}

std::vector<QString> AudioDevice::poll()
{
    std::vector<QString> notices;
    std::vector<QString> errors;
    {
        const std::lock_guard lock(m_errorMutex);
        errors.swap(m_errors);
    }
    for (const QString& error : errors) {
        qCWarning(lcEngine).noquote() << "Audio device" << m_choice.name << "reported:" << error;
    }
    if (const uint64_t dropouts = m_underflows.exchange(0); dropouts > 0) {
        qCWarning(lcEngine).noquote() << "Audio dropped out" << dropouts << "time(s) on" << m_choice.name;
    }

    if (!m_deviceLost.exchange(false)) return notices;

    const DeviceChoice lost = m_choice;
    qCWarning(lcEngine).noquote() << "Audio device lost:" << lost.name << "(" << apiName(lost.api) << ")";
    close();

    if (lost.api == AudioApi::Asio && !m_asioRetried) {
        m_asioRetried = true;
        if (auto reopened = openUnlogged(lost, m_requestedFrames)) {
            notices.push_back(u"%1 restarted after the driver asked for a reset"_s.arg(lost.name));
            qCInfo(lcEngine).noquote() << notices.back();
            return notices;
        } else {
            qCWarning(lcEngine).noquote() << reopened.error().message;
        }
    }

    if (auto fallback = openUnlogged(std::nullopt, m_requestedFrames)) {
        notices.push_back(u"%1 stopped working; switched to system audio (%2)"_s.arg(lost.name, m_choice.name));
        qCWarning(lcEngine).noquote() << notices.back();
    } else {
        notices.push_back(u"No audio output is available: %1"_s.arg(fallback.error().message));
        qCWarning(lcEngine).noquote() << notices.back();
    }
    return notices;
}

int AudioDevice::callback(void* output, void*, unsigned int frames, double, unsigned int status, void* user)
{
    auto* self = static_cast<AudioDevice*>(user);
    // Flush denormals to zero: decaying reverb tails otherwise cost huge CPU.
    _mm_setcsr(_mm_getcsr() | 0x8040);
    if ((status & RTAUDIO_OUTPUT_UNDERFLOW) != 0) self->m_underflows.fetch_add(1, std::memory_order_relaxed);
    auto* out = static_cast<float*>(output); // non-interleaved: [left block][right block]
    self->m_render(AudioBlock{out, out + frames, static_cast<int>(frames)});
    return 0;
}

void AudioDevice::onError(int type, const std::string& text)
{
    if (type == RTAUDIO_DEVICE_DISCONNECT) m_deviceLost.store(true);
    const std::lock_guard lock(m_errorMutex);
    m_errors.push_back(QString::fromStdString(text));
}

} // namespace openstage::engine
