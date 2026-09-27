#pragma once

#include "gigchain/core/Ids.h"

#include <QString>

#include <array>
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

// What the keyboard is doing right now, as the instruments hear it: for the
// on-screen keyboard that lights up with the keys played.
struct MidiActivity
{
    std::array<uint8_t, 128> velocity{}; // per note: 0 = up, else how hard it was played
    int pitchBend = 8192;                // 0..16383, 8192 = centre
    int modWheel = 0;                    // 0..127
    bool sustain = false;                // the sustain pedal is down

    bool operator==(const MidiActivity&) const = default;
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

// A song's sections for the engine: how long each is and which channels of
// `patch` play in it (see IEngine::setSongSections).
struct SongSections
{
    struct Section
    {
        int bars = 4;
        std::vector<core::ChannelId> live;
    };
    core::PatchId patch; // the patch `live` was worked out for
    std::vector<Section> sections;
    bool switchEarly = false; // a beat before each section instead of a sixteenth

    bool operator==(const SongSections&) const = default;
};

// Where the song is (IEngine::songPosition).
struct SongPosition
{
    bool playing = false;
    bool countingIn = false;
    int section = -1; // in force; -1 = the song has no sections
    int bar = 0;      // 1-based within the section; 0 while stopped or counting in
    int bars = 0;     // the section's length

    bool operator==(const SongPosition&) const = default;
};

// ---- The loop station (one audio loop per channel)

// What a looper button asks for.
enum class LoopCommand : uint8_t
{
    Record = 1, // start recording, close the loop, record a layer on top, end the layer
    PlayStop,   // start or stop the loop (ends a recording or layer too)
    Undo,       // takes off the last layer (or the one being recorded)
    Stop,       // stops at once (a recording in progress is dropped)
    Clear,      // empties it
};

enum class LoopState : uint8_t
{
    Empty,
    Armed,        // recording starts on the next bar
    Recording,
    Closing,      // the loop closes on the next bar
    Playing,
    OverdubArmed, // a layer starts on the next bar
    Overdubbing,
    Stopped,      // has a loop, not playing
    StartArmed,   // starts on the next bar
    StopArmed,    // stops on the next bar
};

// One channel's loop as the screen shows it.
struct ChannelLoop
{
    core::ChannelId channel;
    LoopState state = LoopState::Empty;
    double progress = 0.0; // where the loop is, 0-1 (while recording: 0)
    int bar = 0;           // 1-based bar of the loop playing (0 when not playing)
    int bars = 0;          // its length in bars (0 until known)
    int layers = 0;        // layers recorded on top

    bool operator==(const ChannelLoop&) const = default;
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
