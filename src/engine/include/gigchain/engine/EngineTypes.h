#pragma once

#include "gigchain/core/Ids.h"

#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
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

// Which of the system's audio systems drives the sound. Each system has its
// own (systemAudioDrivers()): a setup naming one this system lacks (moved
// from another computer) falls back to System, and the player is told.
enum class AudioDriver
{
    System, // the system's own: Windows audio (WASAPI), PulseAudio on Linux, Core Audio on the Mac; the default
    Asio,   // Windows: lowest latency; needs the device's ASIO driver
    Jack,   // Linux: the pro-audio server (low latency, routing between apps)
    Alsa,   // Linux: straight to the hardware
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

// A key pressed (note-on), from the keyboard or the screen, with when it
// came: the steady clock (std::chrono::steady_clock), in nanoseconds.
struct KeyPress
{
    int note = 60;
    int velocity = 100;
    int64_t timeNs = 0;

    bool operator==(const KeyPress&) const = default;
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
    // The flow: each part's section, in playing order (a chorus played twice
    // is listed twice); empty: each section once, in order.
    std::vector<int> parts;
    // What plays outside any section (a song without sections): nullopt =
    // every channel; else only these (a sound playing one channel at a time).
    std::optional<std::vector<core::ChannelId>> unsectioned;

    bool operator==(const SongSections&) const = default;
};

// What the keyboard asked of the song since the last look
// (IEngine::takeTransportRequests): flags.
namespace transport {
inline constexpr uint32_t kStart = 1;    // MIDI Start, MMC Play: from the top
inline constexpr uint32_t kContinue = 2; // MIDI Continue: from where it is
inline constexpr uint32_t kStop = 4;     // MIDI Stop, MMC Stop
inline constexpr uint32_t kToggle = 8;   // a double press of the sustain pedal: play or stop
inline constexpr uint32_t kNextPart = 16;     // ▶▶ (MMC Fast Forward, Mackie Control)
inline constexpr uint32_t kPreviousPart = 32; // ◀◀ (MMC Rewind, Mackie Control)
inline constexpr uint32_t kLoopPart = 64;     // Cycle / Loop: the part loops until pressed again
inline constexpr uint32_t kClick = 128;       // the click on or off
inline constexpr uint32_t kNextSong = 256;
inline constexpr uint32_t kPreviousSong = 512;
inline constexpr uint32_t kNextSound = 1024;
inline constexpr uint32_t kPreviousSound = 2048;
} // namespace transport

// Where the song is (IEngine::songPosition).
struct SongPosition
{
    bool playing = false;
    bool countingIn = false;
    int section = -1; // in force; -1 = the song has no sections
    int bar = 0;      // 1-based within the part; 0 while stopped or counting in
    int bars = 0;     // the part's length
    // The song's timeline (its flow): the part in force (-1: none), and how
    // far into it the count is, in quarter notes.
    int part = -1;
    double quarter = 0.0;
    // Queued for the next bar line or the part's end (the live controls).
    int queuedPart = -1; // Next part / Go to part: where; -1 = none
    int repeats = 0;     // Repeat part: times more
    bool hold = false;   // Hold: loops until released
    bool stopAtEnd = false;

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
    double progress = 0.0; // where the loop is, 0-1; while recording: how far into the bar
    int bar = 0;           // 1-based bar playing, or being recorded (0 otherwise)
    int bars = 0;          // its length in bars (0 until known)
    int layers = 0;        // layers recorded on top
    int beatsToGo = 0;     // waiting for the bar (to record, close, start): beats left

    bool operator==(const ChannelLoop&) const = default;
};

// A MIDI input as Settings shows it.
struct MidiPort
{
    QString name;
    bool enabled = false;      // plays into the app
    bool controlsOnly = false; // not played: its buttons and knobs only (transport, learned controls)
    int channel = 0;           // 0 = all channels, 1-16 = only that one

    bool operator==(const MidiPort&) const = default;
};

// Linear signal level, 0 (silence) to 1 (full scale).
struct LevelReading
{
    float peak = 0.0F;
    float rms = 0.0F;
};

} // namespace gigchain::engine
