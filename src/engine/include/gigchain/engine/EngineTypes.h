#pragma once

#include "gigchain/core/Chords.h"
#include "gigchain/core/Ids.h"
#include "gigchain/core/SongMap.h"

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

// Which of the system's audio systems drives the sound. Each system has its
// own (systemAudioDrivers()): a setup naming one this system lacks (moved
// from another computer) falls back to System, and the player is told.
enum class AudioDriver
{
    System, // the system's own: Windows audio (WASAPI), PulseAudio on Linux; the default
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

// ---- Chord follow: the chart follows what is played

// One chord of the song as it is heard. Pitch classes 0-11 (C = 0).
struct ChordFollowStep
{
    int section = -1;    // in the song's sections (setSongSections); -1 = before the first
    uint16_t family = 0; // its notes: bit n = pitch class n
    int root = 0;
    int bass = -1;       // a slash bass; -1 = none
    int third = -1;      // its third; -1 = none (sus, 5)
    int otherThird = -1; // the third it is not (the major third of a minor chord); -1 = none
    int colour = -1;     // what stands in for a missing third (a sus note, a 5 chord's fifth); -1 = none

    bool operator==(const ChordFollowStep&) const = default;
};

// A song's chords in playing order (see IEngine::setChordFollow).
struct ChordFollowMap
{
    std::vector<ChordFollowStep> steps;
    std::vector<int> sectionStarts; // per section: its first step; -1 = it has none
    int resumeAt = -1;              // a chart edited while following carries on from this step

    bool operator==(const ChordFollowMap&) const = default;
};

// Where following is.
struct ChordFollowPosition
{
    bool active = false;  // a map is being followed
    bool started = false; // its first chord was heard (or a section chosen)
    int step = -1;        // the chord being played; -1 = not started
    int section = -1;     // in force: the first chord's section before the start

    bool operator==(const ChordFollowPosition&) const = default;
};

// The step a chord name makes in `section`.
[[nodiscard]] inline ChordFollowStep followStepOf(const core::ChordShape& shape, int section)
{
    const auto at = [&shape](int interval) { return interval < 0 ? -1 : (shape.root + interval) % 12; };
    const int other = shape.third == 3 ? 4 : shape.third == 4 ? 3 : -1;
    return ChordFollowStep{.section = section,
                           .family = shape.family,
                           .root = shape.root,
                           .bass = shape.bass,
                           .third = at(shape.third),
                           .otherThird = at(other),
                           .colour = at(shape.colour)};
}

// A song's chords as the engine follows them (not resuming: resumeAt -1).
[[nodiscard]] inline ChordFollowMap followMapOf(const core::SongMap& song)
{
    ChordFollowMap map;
    map.sectionStarts = song.sectionStarts;
    map.steps.reserve(song.steps.size());
    for (const core::SongStep& step : song.steps) map.steps.push_back(followStepOf(step.shape, step.section));
    return map;
}

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
