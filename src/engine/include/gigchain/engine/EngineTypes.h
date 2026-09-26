#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace gigchain::engine {

enum class PluginKind
{
    Instrument,
    Effect,
};

struct PluginInfo
{
    QString id;
    QString name;
    QString vendor;
    PluginKind kind = PluginKind::Instrument;
    QString subCategories; // VST3 sub-categories, e.g. "Instrument|Piano"
    QString version;
    QString classId; // VST3 class id, 32 hex digits (names snapshot images)
    // From the plugin's factory: the maker's website and support address.
    QString website;
    QString email;
    QString sdkVersion; // the VST3 SDK it was built with, e.g. "VST 3.7.9"
};

// What a long engine job is doing, for a splash screen or loading overlay.
enum class LoadStage
{
    ScanningPlugins, // `what` = the plugin file being scanned
    LoadingSounds,   // `what` = the plugin being loaded; done == total at the end
};
// Called on the thread that started the job, before each item and once
// more when done (done == total).
using LoadProgress = std::function<void(LoadStage stage, const QString& what, int done, int total)>;

enum class AudioDriver
{
    System, // Windows audio (WASAPI): the default
    Asio,   // lowest latency; needs the device's ASIO driver
};

// An output the Settings page can offer.
struct AudioOutput
{
    AudioDriver driver = AudioDriver::System;
    QString name;
    std::vector<unsigned int> sampleRates; // ascending; only 44.1 to 96 kHz (see IEngine::audioOutputs)
    unsigned int preferredSampleRate = 0;  // the device's own rate; 0 = unknown
    bool isDefault = false;                // the Windows default output
};

// How audio should run (what Settings saves) or runs (IEngine::audioSetup()).
struct AudioSetup
{
    AudioDriver driver = AudioDriver::System;
    QString device;              // empty = the Windows default output
    unsigned int sampleRate = 0; // 0 = the device's own rate
    unsigned int bufferFrames = 256;
    // A device whose inputs (microphone, instrument inputs) channels can
    // play through effects; same driver as the output. Empty = no inputs.
    QString inputDevice;

    bool operator==(const AudioSetup&) const = default;
};

// A device with inputs the Settings page can offer.
struct AudioInputDevice
{
    AudioDriver driver = AudioDriver::System;
    QString name;
    int channels = 0;
};

// One of a plugin's parameters a knob can be mapped to.
struct PluginParameter
{
    uint32_t id = 0;
    QString name;

    bool operator==(const PluginParameter&) const = default;
};

// The song's backing track as it plays now.
struct BackingTrackState
{
    QString path;          // the file asked for; empty = none
    bool loading = false;  // still being read
    bool loaded = false;   // ready to play
    bool playing = false;
    double position = 0.0; // seconds
    double length = 0.0;   // seconds
};

// A MIDI input as Settings shows it.
struct MidiPort
{
    QString name;
    bool enabled = false; // plays into the app
    int channel = 0;      // 0 = all channels, 1-16 = only that one

    bool operator==(const MidiPort&) const = default;
};

// Linear signal level, 0 (silence) to 1 (full scale).
struct LevelReading
{
    float peak = 0.0F;
    float rms = 0.0F;
};

} // namespace gigchain::engine
