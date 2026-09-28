#pragma once

#include "INode.h"

#include "gigchain/core/Error.h"
#include "gigchain/engine/Notice.h"

#include <QString>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class RtAudio;
class TestAudioDevice;

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
    int inputChannels = 0;
    unsigned int preferredSampleRate = 0;
    bool isDefault = false;
    std::vector<unsigned int> sampleRates; // the rates it can run at, ascending
};

struct DeviceChoice
{
    AudioApi api = AudioApi::Wasapi;
    QString name;
};

// Called on the audio thread for every block, with the inputs' audio when
// inputs are open; must be real-time safe.
using RenderCallback = std::function<void(AudioBlock, const AudioInputs&)>;

// The most input channels read from one device.
inline constexpr int kMaxAudioInputs = 16;

// A stereo output stream (first two channels) on one device, and optionally
// the inputs of a device of the same driver (duplex), wrapping RtAudio. The
// only unit that includes RtAudio. Main-thread API except for the render
// callback it runs.
//
// Recovery (user decisions 2026-09-24 and 2026-09-28): when the device stops
// working (unplugged, a driver reset; however the driver says so), the
// device asked for is reopened if it is still there; if not, the sound goes
// on through the default system output, and the device asked for is taken
// back as soon as it is plugged in again (on a device change, and every few
// seconds while it is missing). Following the system default, the output
// moves with Windows' default. Every step is logged and reported by poll().
class AudioDevice
{
    friend class ::TestAudioDevice; // simulates a driver stopping the stream

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
    // Every WASAPI and ASIO device with inputs (microphones, instrument inputs).
    static std::vector<AudioDeviceInfo> listInputs();

    // std::nullopt = the default system (WASAPI) output. wantedRate 0 = the
    // device's own rate; any other rate the device does not list is an error.
    // `input`: a device whose inputs open with the output (same driver; for
    // ASIO the same device); none by default.
    core::Result<void> open(std::optional<DeviceChoice> choice, unsigned int bufferFrames, RenderCallback render,
                            unsigned int wantedRate = 0, std::optional<DeviceChoice> input = std::nullopt);
    void close();

    // Stop / restart the running stream without closing it. When pause()
    // returns, no render callback is running or will run until resume().
    core::Result<void> pause();
    core::Result<void> resume();

    // Main thread, regularly: logs what went wrong since the last call,
    // recovers from a lost device (and takes the wanted one back when it
    // returns), and returns user-facing notices.
    std::vector<Notice> poll();
    // Main thread: the computer's audio devices changed (one plugged in or
    // out, another default); the next poll() looks again.
    void devicesChanged() { m_devicesChanged = true; }
    // Playing through another device than the one asked for (it is missing).
    [[nodiscard]] bool standingIn() const { return m_standingIn; }
    // What was asked for (std::nullopt = the system default), its rate (0 =
    // its own) and inputs: what Settings keeps, whatever stands in for it.
    [[nodiscard]] const std::optional<DeviceChoice>& wanted() const { return m_wanted; }
    [[nodiscard]] unsigned int wantedRate() const { return m_wantedRate; }
    [[nodiscard]] const std::optional<DeviceChoice>& wantedInput() const { return m_wantedInput; }
    // Opened on the system default because `wanted` could not be (missing
    // at start): take it back, at that rate and with those inputs, when it
    // is plugged in.
    void standIn(const DeviceChoice& missing, unsigned int rate, std::optional<DeviceChoice> input);

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] double sampleRate() const { return m_sampleRate; }
    [[nodiscard]] unsigned int requestedSampleRate() const { return m_requestedRate; }
    [[nodiscard]] unsigned int requestedBufferFrames() const { return m_requestedFrames; }
    [[nodiscard]] int maxBlock() const { return m_maxBlock; }
    [[nodiscard]] QString deviceName() const { return m_choice.name; }
    [[nodiscard]] AudioApi api() const { return m_choice.api; }
    [[nodiscard]] double latencyMs() const { return m_latencyMs; }
    // The open input device (empty when none) and its channel count.
    [[nodiscard]] QString inputName() const { return m_inputChannels > 0 ? m_input.name : QString(); }
    [[nodiscard]] int inputChannels() const { return m_inputChannels; }

private:
    static int callback(void* output, void* input, unsigned int frames, double streamTime, unsigned int status,
                        void* user);
    void onError(int type, const std::string& text);
    core::Result<void> openUnlogged(std::optional<DeviceChoice> choice, unsigned int bufferFrames, unsigned int wantedRate,
                                    std::optional<DeviceChoice> input);
    // After the stream stopped working: the wanted device again, else the
    // system default, else nothing (tried again later).
    void recover(std::vector<Notice>& notices);
    // While standing in (or with nothing open): the wanted device, if it is
    // there again.
    void takeWantedBack(std::vector<Notice>& notices);
    // Following the system default: move when Windows' default moved.
    void followDefault(std::vector<Notice>& notices);
    // Whether an output of that driver and name is plugged in (not opened).
    [[nodiscard]] static bool outputPresent(const DeviceChoice& choice);
    // The name of the system's default output now (empty: none).
    [[nodiscard]] static QString defaultOutputName();

    std::unique_ptr<RtAudio> m_rtaudio;
    RenderCallback m_render;
    DeviceChoice m_choice;
    DeviceChoice m_input;
    std::optional<DeviceChoice> m_requestedInput;
    int m_inputChannels = 0;
    std::array<const float*, kMaxAudioInputs> m_inputPointers{}; // audio thread
    unsigned int m_requestedFrames = 256;
    unsigned int m_requestedRate = 0; // 0 = the device's own
    double m_sampleRate = 0.0;
    int m_maxBlock = 0;
    double m_latencyMs = 0.0;
    // What was asked for (std::nullopt = the system default) at what rate and
    // with what inputs, as open() was called: kept while standing in.
    std::optional<DeviceChoice> m_wanted;
    unsigned int m_wantedRate = 0;
    std::optional<DeviceChoice> m_wantedInput;
    bool m_expectRunning = false; // opened and not paused: a stopped stream means it stopped working
    bool m_standingIn = false;    // on the system default while the wanted device is missing (or on nothing)
    bool m_devicesChanged = false;
    std::chrono::steady_clock::time_point m_lastLook{};

    std::atomic<bool> m_deviceLost{false};
    std::atomic<uint64_t> m_underflows{0};
    std::mutex m_errorMutex;           // guards m_errors (RtAudio may report from its own threads)
    std::vector<QString> m_errors;     // errors reported by RtAudio since the last poll
};

QString apiName(AudioApi api);

} // namespace gigchain::engine
