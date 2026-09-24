#pragma once

#include <QString>

#include <vector>

namespace openstage::engine {

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
};

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

    bool operator==(const AudioSetup&) const = default;
};

struct MidiPort
{
    QString name;
    bool enabled = true; // false = switched off in Settings
};

// Linear signal level, 0 (silence) to 1 (full scale).
struct LevelReading
{
    float peak = 0.0F;
    float rms = 0.0F;
};

} // namespace openstage::engine
