#include "AudioDevice.h"
#include "AudioApis.h"

#include "EngineLog.h"

#include "gigchain/core/Branding.h"

#include "gigchain/platform/Timing.h"

#include <RtAudio.h>

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <span>
#include <string_view>
#include <thread>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

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

// How long a stop waits for a callback already running (a plugin's block) to end.
constexpr auto kCallbackIdleWait = std::chrono::milliseconds(200);
// RtAudio's warning when JACK runs at another block size than the stream's (6.0.1).
constexpr std::string_view kJackSizeChanged = "JACK buffer size has changed";
// At most one reopen for a size change in this long (the graph may still be moving).
constexpr auto kSizeReopenGap = std::chrono::seconds(1);

} // namespace

// Stops a running stream at once. JACK's stopStream() first waits for its last
// blocks to play out, but RtAudio refuses every block of a size other than the
// one asked for, so with PipeWire's graph running at another size the wait never
// ends and the app hangs (#54). abortStream() skips that wait: no callback runs
// once it returns, which is all a pause or a close needs. But PipeWire stops its
// JACK thread with pthread_cancel, which must not land inside a plugin (#81): the
// callback goes quiet first, and the stop waits (briefly) for the one running.
bool AudioDevice::stopRunning()
{
    if (m_rtaudio->getCurrentApi() != RtAudio::UNIX_JACK) return m_rtaudio->stopStream() == RTAUDIO_NO_ERROR;
    m_quiet.store(true);
    const auto until = std::chrono::steady_clock::now() + kCallbackIdleWait;
    while (m_inCallback.load() > 0 && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (m_inCallback.load() > 0) {
        qCWarning(lcEngine).noquote() << "A block of" << m_choice.name << "was still playing after"
                                      << kCallbackIdleWait.count() << "ms: stopping it anyway";
    }
    return m_rtaudio->abortStream() == RTAUDIO_NO_ERROR;
}

AudioDevice::AudioDevice() = default;

AudioDevice::~AudioDevice()
{
    close();
}

std::vector<AudioDeviceInfo> AudioDevice::listOutputs()
{
    std::vector<AudioDeviceInfo> outputs;
    for (const AudioApi api : systemAudioDrivers()) {
        const auto rt = probe(api);
        for (const unsigned int id : rt->getDeviceIds()) {
            const RtAudio::DeviceInfo info = rt->getDeviceInfo(id);
            if (info.outputChannels < 2) continue;
            AudioDeviceInfo device;
            device.api = api;
            device.name = QString::fromStdString(info.name);
            device.outputChannels = static_cast<int>(info.outputChannels);
            device.preferredSampleRate = info.preferredSampleRate;
            device.isDefault = api == AudioApi::System && info.isDefaultOutput;
            device.sampleRates.assign(info.sampleRates.begin(), info.sampleRates.end());
            if (device.preferredSampleRate != 0 &&
                std::ranges::find(device.sampleRates, device.preferredSampleRate) == device.sampleRates.end()) {
                device.sampleRates.push_back(device.preferredSampleRate);
            }
            std::ranges::sort(device.sampleRates);
            outputs.push_back(std::move(device));
        }
    }
    return outputs;
}

std::vector<AudioDeviceInfo> AudioDevice::listInputs()
{
    std::vector<AudioDeviceInfo> inputs;
    for (const AudioApi api : systemAudioDrivers()) {
        const auto rt = probe(api);
        for (const unsigned int id : rt->getDeviceIds()) {
            const RtAudio::DeviceInfo info = rt->getDeviceInfo(id);
            if (info.inputChannels < 1) continue;
            inputs.push_back(AudioDeviceInfo{.api = api,
                                             .name = QString::fromStdString(info.name),
                                             .outputChannels = static_cast<int>(info.outputChannels),
                                             .inputChannels = static_cast<int>(info.inputChannels),
                                             .preferredSampleRate = info.preferredSampleRate,
                                             .isDefault = api == AudioApi::System && info.isDefaultInput,
                                             .sampleRates = {info.sampleRates.begin(), info.sampleRates.end()}});
        }
    }
    return inputs;
}

core::Result<void> AudioDevice::open(std::optional<DeviceChoice> choice, unsigned int bufferFrames, RenderCallback render,
                                     unsigned int askedRate, std::optional<DeviceChoice> input)
{
    m_render = std::move(render);
    m_wanted = choice;
    m_wantedRate = askedRate;
    m_wantedInput = input;
    m_standingIn = false;
    auto opened = openUnlogged(std::move(choice), bufferFrames, askedRate, std::move(input));
    if (opened) {
        qCInfo(lcEngine).noquote() << "Audio output:" << m_choice.name << "(" << apiName(m_choice.api) << ")"
                                   << m_sampleRate << "Hz," << m_maxBlock << "frames (asked" << bufferFrames << "), latency"
                                   << m_latencyMs << "ms";
        if (m_inputChannels > 0) {
            qCInfo(lcEngine).noquote() << "Audio input:" << m_input.name << "," << m_inputChannels << "channels";
        }
    } else {
        qCWarning(lcEngine).noquote() << opened.error().message;
    }
    return opened;
}

core::Result<void> AudioDevice::openUnlogged(std::optional<DeviceChoice> choice, unsigned int bufferFrames,
                                             unsigned int askedRate, std::optional<DeviceChoice> input)
{
    close();
    m_requestedFrames = bufferFrames;
    m_requestedRate = askedRate;
    m_requestedInput = input;
    m_inputChannels = 0;
    const AudioApi driver = choice ? choice->api : AudioApi::System;
    auto rt = std::make_unique<RtAudio>(
        toRtApi(driver), [this](RtAudioErrorType type, const std::string& text) { onError(type, text); });

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
                          choice ? QCoreApplication::translate("Engine", "No audio output named \"%1\" (%2)").arg(choice->name, apiName(driver))
                                 : QCoreApplication::translate("Engine", "This computer has no default audio output"));
    }
    const RtAudio::DeviceInfo info = rt->getDeviceInfo(*deviceId);

    RtAudio::StreamParameters output;
    output.deviceId = *deviceId;
    // Every output it has (up to kMaxAudioOutputs): 1-2 carry the mix, the
    // others what is sent there (a click for the in-ears, a channel to the desk).
    output.nChannels = std::clamp(info.outputChannels, 2U, static_cast<unsigned int>(kMaxAudioOutputs));
    output.firstChannel = 0;
    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_SCHEDULE_REALTIME | latencyFlags(driver);
    options.streamName = branding::name().toStdString(); // what Windows shows for our audio
    const unsigned int ownRate = info.preferredSampleRate != 0 ? info.preferredSampleRate : 48000;
    const unsigned int rate = askedRate != 0 ? askedRate : ownRate;
    if (askedRate != 0 && askedRate != info.preferredSampleRate &&
        std::ranges::find(info.sampleRates, askedRate) == info.sampleRates.end()) {
        return core::fail(core::ErrorCode::InvalidData, QCoreApplication::translate("Engine", "%1 (%2) cannot run at %3 Hz")
                                                            .arg(QString::fromStdString(info.name), apiName(driver))
                                                            .arg(askedRate));
    }
    unsigned int frames = bufferFrames;

    // The inputs, when asked for: a device of the same driver.
    RtAudio::StreamParameters inputParameters;
    int openInputs = 0;
    if (input) {
        if (input->api != driver) {
            return core::fail(core::ErrorCode::InvalidData,
                              QCoreApplication::translate("Engine", "The input %1 (%2) and the output %3 (%4) must use the same driver")
                                  .arg(input->name, apiName(input->api), QString::fromStdString(info.name), apiName(driver)));
        }
        std::optional<unsigned int> inputId;
        for (const unsigned int id : rt->getDeviceIds()) {
            const RtAudio::DeviceInfo candidate = rt->getDeviceInfo(id);
            if (QString::fromStdString(candidate.name) == input->name && candidate.inputChannels > 0) {
                inputId = id;
                openInputs = std::min(static_cast<int>(candidate.inputChannels), kMaxAudioInputs);
                break;
            }
        }
        if (!inputId) {
            return core::fail(core::ErrorCode::InvalidData,
                              QCoreApplication::translate("Engine", "No audio input named \"%1\" (%2)").arg(input->name, apiName(driver)));
        }
        inputParameters.deviceId = *inputId;
        inputParameters.nChannels = static_cast<unsigned int>(openInputs);
        inputParameters.firstChannel = 0;
    }

    const auto takeLastError = [this](const QString& fallback) {
        const std::scoped_lock lock(m_errorMutex);
        QString text = m_errors.empty() ? fallback : m_errors.back();
        m_errors.clear();
        return text;
    };

    m_outputChannels = static_cast<int>(output.nChannels); // read by the callback
    bool opened = rt->openStream(&output, input ? &inputParameters : nullptr, RTAUDIO_FLOAT32, rate, &frames,
                                 &AudioDevice::callback, this, &options) == RTAUDIO_NO_ERROR;
    if (!opened && output.nChannels > 2) {
        // Some drivers refuse all their outputs at once: the mix alone still plays.
        qCWarning(lcEngine).noquote() << "Opening" << output.nChannels << "outputs of" << QString::fromStdString(info.name)
                                      << "failed (" << takeLastError(u"unknown error"_s) << "): opening outputs 1-2 only";
        output.nChannels = 2;
        m_outputChannels = 2;
        opened = rt->openStream(&output, input ? &inputParameters : nullptr, RTAUDIO_FLOAT32, rate, &frames,
                                &AudioDevice::callback, this, &options) == RTAUDIO_NO_ERROR;
    }
    if (!opened) {
        m_outputChannels = 2;
        return core::fail(core::ErrorCode::DeviceUnavailable,
                          input ? QCoreApplication::translate("Engine", "Could not open %1 with the inputs of %2 (%3): %4")
                                      .arg(QString::fromStdString(info.name), input->name, apiName(driver),
                                           takeLastError(QCoreApplication::translate("Engine", "unknown error")))
                                : QCoreApplication::translate("Engine", "Could not open %1 (%2): %3")
                                      .arg(QString::fromStdString(info.name), apiName(driver),
                                           takeLastError(QCoreApplication::translate("Engine", "unknown error"))));
    }
    m_inputChannels = openInputs;
    if (input) m_input = *input;
    m_maxBlock = static_cast<int>(frames);
    m_sampleRate = rt->getStreamSampleRate();
    m_quiet.store(false); // (quiet since the last stop)
    m_serverSizeChanged.store(false); // (refusals of the stream before are over)
    if (rt->startStream() != RTAUDIO_NO_ERROR) {
        rt->closeStream();
        m_inputChannels = 0;
        return core::fail(core::ErrorCode::DeviceUnavailable,
                          QCoreApplication::translate("Engine", "Could not start %1 (%2): %3")
                              .arg(QString::fromStdString(info.name), apiName(driver),
                                   takeLastError(QCoreApplication::translate("Engine", "unknown error"))));
    }
    const auto latencyFrames = static_cast<double>(rt->getStreamLatency());
    m_latencyMs = m_sampleRate > 0.0 ? 1000.0 * std::max(latencyFrames, static_cast<double>(frames)) / m_sampleRate : 0.0;
    m_choice = DeviceChoice{.api = driver, .name = QString::fromStdString(info.name)};
    m_rtaudio = std::move(rt);
    m_expectRunning = true;
    return {};
}

void AudioDevice::close()
{
    m_expectRunning = false;
    const bool disconnected = m_disconnectReported.exchange(false);
    if (!m_rtaudio) return;
    // JACK closes the stream itself when its server goes away, on a thread of
    // its own, and reports the disconnect from inside that close: wait for it
    // to finish, never stop or close what it is closing.
    if (disconnected && m_rtaudio->getCurrentApi() == RtAudio::UNIX_JACK) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (m_rtaudio->isStreamOpen() && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (m_rtaudio->isStreamOpen()) {
            qCCritical(lcEngine).noquote() << "JACK did not finish closing" << m_choice.name
                                           << "within 2 s: it is left to finish on its own (its memory is not given back)";
            // Deleting it under JACK's closing thread would crash the app.
            [[maybe_unused]] const RtAudio* leftToJack = m_rtaudio.release();
            return;
        }
    }
    if (m_rtaudio->isStreamRunning() && !stopRunning()) {
        qCWarning(lcEngine).noquote() << "Stopping" << m_choice.name << "reported an error";
    }
    if (m_rtaudio->isStreamOpen()) m_rtaudio->closeStream();
    m_rtaudio.reset();
}

core::Result<void> AudioDevice::pause()
{
    if (!m_rtaudio || !m_rtaudio->isStreamRunning()) return {};
    if (!stopRunning()) {
        const std::scoped_lock lock(m_errorMutex);
        const QString why = m_errors.empty() ? QCoreApplication::translate("Engine", "unknown error") : m_errors.back();
        m_errors.clear();
        return core::fail(core::ErrorCode::DeviceUnavailable, QCoreApplication::translate("Engine", "Could not pause %1: %2").arg(m_choice.name, why));
    }
    m_expectRunning = false; // stopped on purpose
    return {};
}

core::Result<void> AudioDevice::resume()
{
    if (!m_rtaudio || !m_rtaudio->isStreamOpen()) {
        return core::fail(core::ErrorCode::DeviceUnavailable, QCoreApplication::translate("Engine", "No audio output is open to resume"));
    }
    if (m_rtaudio->isStreamRunning()) {
        m_expectRunning = true;
        return {};
    }
    m_quiet.store(false); // (quiet since the pause)
    if (m_rtaudio->startStream() != RTAUDIO_NO_ERROR) {
        const std::scoped_lock lock(m_errorMutex);
        const QString why = m_errors.empty() ? QCoreApplication::translate("Engine", "unknown error") : m_errors.back();
        m_errors.clear();
        return core::fail(core::ErrorCode::DeviceUnavailable, QCoreApplication::translate("Engine", "Could not restart %1: %2").arg(m_choice.name, why));
    }
    m_expectRunning = true;
    return {};
}

bool AudioDevice::isOpen() const
{
    return m_rtaudio && m_rtaudio->isStreamOpen();
}

std::vector<Notice> AudioDevice::poll()
{
    std::vector<Notice> notices;
    std::vector<QString> errors;
    {
        const std::scoped_lock lock(m_errorMutex);
        errors.swap(m_errors);
    }
    for (const QString& error : errors) {
        qCWarning(lcEngine).noquote() << "Audio device" << m_choice.name << "reported:" << error;
    }
    if (const uint64_t dropouts = m_underflows.exchange(0); dropouts > 0) {
        qCWarning(lcEngine).noquote() << "Audio dropped out" << dropouts << "time(s) on" << m_choice.name;
        m_untoldDropouts += dropouts;
    }
    // Told to the player too (MainStage's overload message), once a minute at most.
    const auto polledAt = std::chrono::steady_clock::now();
    if (m_tellDropouts && m_untoldDropouts > 0 && (!m_droppedToldAt || polledAt - *m_droppedToldAt >= std::chrono::minutes(1))) {
        notices.push_back(Notice::warning(
            QCoreApplication::translate("Engine", "The audio dropped out (%1 time(s)): the computer could not keep up. A bigger "
                                                  "buffer in Settings > Audio usually stops it; so does closing other programs")
                .arg(m_untoldDropouts)));
        m_untoldDropouts = 0;
        m_droppedToldAt = polledAt;
    } else if (!m_tellDropouts) {
        m_untoldDropouts = 0;
    }

    // Stopped working: the driver said so (a disconnect), or the stream
    // stopped by itself (WASAPI reports an unplugged device as a driver
    // error and stops), or nothing could be opened last time.
    const bool stoppedByItself = m_expectRunning && m_rtaudio && !m_rtaudio->isStreamRunning();
    if (m_deviceLost.exchange(false) || stoppedByItself) {
        recover(notices);
        return notices;
    }
    // JACK runs at another size than the stream: silent until it is opened again (#82).
    // Not while paused on purpose (the flag waits for resume()).
    if (m_serverSizeChanged.load() && m_rtaudio && m_expectRunning
        && std::chrono::steady_clock::now() - m_lastSizeReopen >= kSizeReopenGap) {
        reopenAtServerSize(notices);
        return notices;
    }
    const bool changed = std::exchange(m_devicesChanged, false);
    const auto now = std::chrono::steady_clock::now();
    if (m_standingIn) { // (with the stand-in playing, or nothing at all)
        // Waiting for the wanted device: on every device change, and every
        // few seconds (a change can go unnoticed).
        if (changed || now - m_lastLook >= std::chrono::seconds(3)) {
            m_lastLook = now;
            takeWantedBack(notices);
        }
    } else if (!m_wanted && changed) {
        followDefault(notices);
    }
    return notices;
}

void AudioDevice::recover(std::vector<Notice>& notices)
{
    const DeviceChoice lost = m_choice;
    qCWarning(lcEngine).noquote() << "Audio device stopped working:" << lost.name << "(" << apiName(lost.api) << ")";
    close();
    const auto tell = [&notices](Notice notice) {
        qCWarning(lcEngine).noquote() << notice.text;
        notices.push_back(std::move(notice));
    };

    // The wanted device again: a driver reset, or it is still there.
    if (!m_wanted || outputPresent(*m_wanted)) {
        if (auto again = openUnlogged(m_wanted, m_requestedFrames, m_wantedRate, m_wantedInput)) {
            m_standingIn = false;
            tell(Notice::info(m_choice.name == lost.name
                                  ? QCoreApplication::translate("Engine", "%1 started again after it stopped").arg(lost.name)
                                  : QCoreApplication::translate("Engine", "%1 stopped: now playing through %2").arg(lost.name, m_choice.name)));
            return;
        } else {
            qCWarning(lcEngine).noquote() << again.error().message;
        }
    }
    // Missing: the system default in the meantime (at its own rate: the
    // chosen one may not exist there; the engine re-prepares the plugins).
    m_standingIn = true;
    m_lastLook = std::chrono::steady_clock::now();
    const bool hadInputs = m_wantedInput.has_value();
    if (auto fallback = openUnlogged(std::nullopt, m_requestedFrames, 0, std::nullopt)) {
        tell(Notice::warning(
            hadInputs ? QCoreApplication::translate("Engine", "%1 stopped (unplugged?): playing through %2, without inputs, until it is back")
                            .arg(lost.name, m_choice.name)
                      : QCoreApplication::translate("Engine", "%1 stopped (unplugged?): playing through %2 until it is back")
                            .arg(lost.name, m_choice.name)));
    } else {
        tell(Notice::error(QCoreApplication::translate("Engine", "No audio output is available (%1): the sound comes back when "
                                                                 "one is plugged in")
                               .arg(fallback.error().message)));
    }
}

void AudioDevice::reopenAtServerSize(std::vector<Notice>& notices)
{
    m_lastSizeReopen = std::chrono::steady_clock::now();
    const int before = m_maxBlock;
    qCWarning(lcEngine).noquote() << "JACK's server runs at another block size than" << m_choice.name << "was opened at ("
                                  << before << "frames): every cycle is refused; opening it again at the server's size";
    // As it was opened (the wanted device, or the default standing in; the
    // same rate and inputs): only the size is the server's.
    const std::optional<DeviceChoice> device = m_standingIn ? std::nullopt : m_wanted;
    if (auto again = openUnlogged(device, m_requestedFrames, m_requestedRate, m_requestedInput)) {
        qCInfo(lcEngine).noquote() << "Audio output:" << m_choice.name << "(" << apiName(m_choice.api) << ")" << m_sampleRate
                                   << "Hz," << m_maxBlock << "frames, latency" << m_latencyMs << "ms";
        notices.push_back(Notice::info(QCoreApplication::translate("Engine", "The audio server changed its buffer size: %1 plays "
                                                                             "again at %2 samples (%3 ms)")
                                           .arg(m_choice.name)
                                           .arg(m_maxBlock)
                                           .arg(m_latencyMs, 0, 'f', 1)));
        qCInfo(lcEngine).noquote() << notices.back().text;
        return;
    } else {
        qCWarning(lcEngine).noquote() << again.error().message;
    }
    recover(notices); // back to whatever works (its notices say what)
}

void AudioDevice::standIn(const DeviceChoice& missing, unsigned int rate, std::optional<DeviceChoice> input)
{
    m_wanted = missing;
    m_wantedRate = rate;
    m_wantedInput = std::move(input);
    m_standingIn = true;
    m_lastLook = std::chrono::steady_clock::now();
}

void AudioDevice::takeWantedBack(std::vector<Notice>& notices)
{
    if (m_wanted && !outputPresent(*m_wanted)) {
        // Still missing. With nothing open at all, the system default will do.
        const QString missing = m_wanted->name; // (the open below leaves m_wanted as it is)
        if (!m_rtaudio) {
            if (auto fallback = openUnlogged(std::nullopt, m_requestedFrames, 0, std::nullopt)) {
                notices.push_back(
                    Notice::warning(QCoreApplication::translate("Engine", "Playing through %1 until %2 is back").arg(m_choice.name, missing)));
                qCWarning(lcEngine).noquote() << notices.back().text;
            }
        }
        return;
    }
    const QString before = m_rtaudio ? m_choice.name : QString();
    if (auto back = openUnlogged(m_wanted, m_requestedFrames, m_wantedRate, m_wantedInput)) {
        m_standingIn = false;
        notices.push_back(Notice::info(m_wanted ? QCoreApplication::translate("Engine", "%1 is back: playing through it again").arg(m_choice.name)
                                                : QCoreApplication::translate("Engine", "Playing through %1").arg(m_choice.name)));
        qCInfo(lcEngine).noquote() << notices.back().text;
        return;
    } else {
        qCWarning(lcEngine).noquote() << back.error().message;
    }
    // There, but it would not open: keep playing through the stand-in.
    if (!before.isEmpty()) {
        if (auto fallback = openUnlogged(std::nullopt, m_requestedFrames, 0, std::nullopt); !fallback) {
            notices.push_back(
                Notice::error(QCoreApplication::translate("Engine", "No audio output is available: %1").arg(fallback.error().message)));
            qCWarning(lcEngine).noquote() << notices.back().text;
        }
    }
}

void AudioDevice::followDefault(std::vector<Notice>& notices)
{
    const QString now = defaultOutputName();
    if (now.isEmpty() || now == m_choice.name) return;
    if (auto moved = openUnlogged(std::nullopt, m_requestedFrames, m_wantedRate, m_wantedInput)) {
        notices.push_back(
            Notice::info(QCoreApplication::translate("Engine", "Windows' default output changed: now playing through %1").arg(m_choice.name)));
        qCInfo(lcEngine).noquote() << notices.back().text;
    } else {
        qCWarning(lcEngine).noquote() << "Could not follow the default output to" << now << ":" << moved.error().message;
        recover(notices); // back to whatever works
    }
}

bool AudioDevice::outputPresent(const DeviceChoice& choice)
{
    try {
        RtAudio probe(toRtApi(choice.api), [](RtAudioErrorType, const std::string&) {}); // (probing only)
        return std::ranges::any_of(probe.getDeviceIds(), [&probe, &choice](unsigned int id) {
            const RtAudio::DeviceInfo info = probe.getDeviceInfo(id);
            return QString::fromStdString(info.name) == choice.name && info.outputChannels >= 2;
        });
    } catch (const std::exception& e) {
        qCWarning(lcEngine).noquote() << "Looking for" << choice.name << "failed:" << QString::fromUtf8(e.what());
        return false;
    }
}

QString AudioDevice::defaultOutputName()
{
    try {
        RtAudio probe(toRtApi(AudioApi::System), [](RtAudioErrorType, const std::string&) {});
        const unsigned int id = probe.getDefaultOutputDevice();
        return id != 0 ? QString::fromStdString(probe.getDeviceInfo(id).name) : QString();
    } catch (const std::exception& e) {
        qCWarning(lcEngine).noquote() << "Looking for the default output failed:" << QString::fromUtf8(e.what());
        return {};
    }
}

int AudioDevice::callback(void* output, void* input, unsigned int frames, double, unsigned int status, void* user)
{
    auto* self = static_cast<AudioDevice*>(user);
    // Counted while it runs, so a stop knows when no plugin is being called (#81).
    self->m_inCallback.fetch_add(1);
    const struct Leave {
        AudioDevice* device;
        explicit Leave(AudioDevice* d) : device(d) {}
        Leave(const Leave&) = delete;
        Leave& operator=(const Leave&) = delete;
        Leave(Leave&&) = delete;
        Leave& operator=(Leave&&) = delete;
        ~Leave() { device->m_inCallback.fetch_sub(1); }
    } leave{self};
    if (self->m_quiet.load()) {
        // Being stopped: silence, and no plugin called (nothing here may be a
        // cancellation point).
        std::fill_n(static_cast<float*>(output), static_cast<std::size_t>(frames) * static_cast<std::size_t>(self->m_outputChannels), 0.0F);
        return 0;
    }
    // Flush denormals to zero: decaying reverb tails otherwise cost huge CPU.
    platform::flushDenormalsToZeroForThisThread();
    if ((status & (RTAUDIO_OUTPUT_UNDERFLOW | RTAUDIO_INPUT_OVERFLOW)) != 0) {
        self->m_underflows.fetch_add(1, std::memory_order_relaxed);
    }
    // Non-interleaved: [left block][right block][output 3]..., and each input channel's block in turn.
    const auto outputs = static_cast<std::size_t>(self->m_outputChannels);
    const std::span<float> allOutputs(static_cast<float*>(output), static_cast<std::size_t>(frames) * outputs);
    const std::span<float> both = allOutputs.first(static_cast<std::size_t>(frames) * 2);
    // Outputs 3 and up: silent unless something is sent there.
    self->m_extraCount = outputs - 2;
    for (std::size_t c = 2; c < outputs; ++c) {
        float* block = allOutputs.subspan(c * frames, frames).data();
        std::fill_n(block, frames, 0.0F);
        self->m_extraPointers.at(c - 2) = block;
    }
    AudioInputs inputs;
    if (input != nullptr && self->m_inputChannels > 0) {
        const auto count = static_cast<std::size_t>(self->m_inputChannels);
        const std::span<const float> all(static_cast<const float*>(input), count * frames);
        for (std::size_t c = 0; c < count; ++c) self->m_inputPointers.at(c) = all.subspan(c * frames).data();
        inputs = AudioInputs{.channels = std::span<const float* const>(self->m_inputPointers.data(), count),
                             .frames = static_cast<int>(frames)};
    }
    self->m_render(AudioBlock{.left = both.data(), .right = both.subspan(frames).data(), .frames = static_cast<int>(frames)},
                   inputs);
    return 0;
}

void AudioDevice::onError(int type, const std::string& text)
{
    if (type == RTAUDIO_DEVICE_DISCONNECT) {
        m_disconnectReported.store(true);
        m_deviceLost.store(true);
    }
    if (text.find(kJackSizeChanged) != std::string::npos) {
        // Said on every refused cycle (hundreds a second): flagged, logged once by poll().
        m_serverSizeChanged.store(true);
        return;
    }
    const std::scoped_lock lock(m_errorMutex);
    m_errors.push_back(QString::fromStdString(text));
}

} // namespace gigchain::engine
