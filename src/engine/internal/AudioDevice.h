#pragma once

#include "INode.h"

#include "gigchain/core/Error.h"

#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class RtAudio;

namespace gigchain::engine {

enum class AudioApi
{
    Wasapi, // Windows system audio: the default
    Asio,   // opt-in, lowest latency
};

struct AudioDeviceInfo
{
    AudioApi api = AudioApi::Wasapi;
    QString name;
    int outputChannels = 0;
    unsigned int preferredSampleRate = 0;
    bool isDefault = false;
    std::vector<unsigned int> sampleRates; // the rates it can run at, ascending
};

struct DeviceChoice
{
    AudioApi api = AudioApi::Wasapi;
    QString name;
};

// Called on the audio thread for every block; must be real-time safe.
using RenderCallback = std::function<void(AudioBlock)>;

// A stereo output stream (first two channels) on one device, wrapping
// RtAudio. The only unit that includes RtAudio. Main-thread API except for
// the render callback it runs.
//
// Recovery policy (user decision 2026-09-24): if the device is lost, an ASIO
// device is reopened once; if that fails, or a WASAPI device is lost, the
// stream moves to the default system output. Every step is logged and
// reported by poll().
class AudioDevice
{
public:
    AudioDevice();
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;
    AudioDevice(AudioDevice&&) = delete;
    AudioDevice& operator=(AudioDevice&&) = delete;

    // Every WASAPI and ASIO output with at least two channels. Problems while
    // probing drivers are logged.
    static std::vector<AudioDeviceInfo> listOutputs();

    // std::nullopt = the default system (WASAPI) output. sampleRate 0 = the
    // device's own rate; any other rate the device does not list is an error.
    core::Result<void> open(std::optional<DeviceChoice> choice, unsigned int bufferFrames, RenderCallback render,
                            unsigned int sampleRate = 0);
    void close();

    // Stop / restart the running stream without closing it. When pause()
    // returns, no render callback is running or will run until resume().
    core::Result<void> pause();
    core::Result<void> resume();

    // Main thread, regularly: logs what went wrong since the last call,
    // recovers from a lost device, and returns user-facing notices.
    std::vector<QString> poll();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] double sampleRate() const { return m_sampleRate; }
    [[nodiscard]] unsigned int requestedSampleRate() const { return m_requestedRate; }
    [[nodiscard]] unsigned int requestedBufferFrames() const { return m_requestedFrames; }
    [[nodiscard]] int maxBlock() const { return m_maxBlock; }
    [[nodiscard]] QString deviceName() const { return m_choice.name; }
    [[nodiscard]] AudioApi api() const { return m_choice.api; }
    [[nodiscard]] double latencyMs() const { return m_latencyMs; }

private:
    static int callback(void* output, void* input, unsigned int frames, double streamTime, unsigned int status,
                        void* user);
    void onError(int type, const std::string& text);
    core::Result<void> openUnlogged(std::optional<DeviceChoice> choice, unsigned int bufferFrames, unsigned int sampleRate);

    std::unique_ptr<RtAudio> m_rtaudio;
    RenderCallback m_render;
    DeviceChoice m_choice;
    unsigned int m_requestedFrames = 256;
    unsigned int m_requestedRate = 0; // 0 = the device's own
    double m_sampleRate = 0.0;
    int m_maxBlock = 0;
    double m_latencyMs = 0.0;
    bool m_asioRetried = false;

    std::atomic<bool> m_deviceLost{false};
    std::atomic<uint64_t> m_underflows{0};
    std::mutex m_errorMutex;           // guards m_errors (RtAudio may report from its own threads)
    std::vector<QString> m_errors;     // errors reported by RtAudio since the last poll
};

QString apiName(AudioApi api);

} // namespace gigchain::engine
