#pragma once

#include "gigchain/core/Ids.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace gigchain::core {

// A plugin placed in a channel, either as its instrument or as an effect.
struct PluginSlot
{
    QString pluginId;
    QString displayName;
    bool bypass = false;
    // The plugin's own settings (its preset, knobs, loaded sounds) as the
    // engine stored them when the setlist was saved; opaque here. Empty: the
    // plugin's defaults.
    QByteArray state;

    friend bool operator==(const PluginSlot&, const PluginSlot&) = default;
};

// A keyboard knob, fader or pedal (MIDI controller) moving one plugin
// parameter on its channel, within a range (MainStage's screen controls).
struct ControlMapping
{
    int midiChannel = 0; // 0 = any, 1..16
    int controller = 0;  // CC 0-127
    int target = -1;     // -1 = the channel's instrument, else the effect's position
    quint32 parameter = 0;
    QString parameterName; // as the plugin names it, for showing
    double minimum = 0.0;  // the parameter (0-1) at the controller's lowest...
    double maximum = 1.0;  // ... and highest position (below minimum: reversed)

    friend bool operator==(const ControlMapping&, const ControlMapping&) = default;
};

// One mixer strip: an instrument (or an audio input), its effect chain, and
// how the keyboard reaches it. Several channels in a patch make layers and
// splits.
struct Channel
{
    ChannelId id;
    QString name;
    std::optional<PluginSlot> instrument;
    std::vector<PluginSlot> effects;
    double volumeDb = 0.0;
    double pan = 0.0; // -1 = hard left, 0 = centre, +1 = hard right
    bool mute = false;
    bool solo = false;
    int keyLow = 0;
    int keyHigh = 127;
    int transpose = 0;
    int midiChannel = 0; // 0 = omni, 1..16 = that channel only
    int velocityLow = 1; // the note-on velocities it plays (a velocity layer)
    int velocityHigh = 127;
    std::vector<ControlMapping> mappings;
    // An audio input played through the channel's effects instead of an
    // instrument (a vocal mic, a guitar): 1-based input numbers of the audio
    // interface, 0 = none. Mono when only `inputLeft` is set.
    int inputLeft = 0;
    int inputRight = 0;

    friend bool operator==(const Channel&, const Channel&) = default;
};

// A song section (Intro, Verse, Chorus...). Stepping through a song means
// stepping through its patches in order.
struct Patch
{
    PatchId id;
    QString name;
    std::vector<Channel> channels;

    friend bool operator==(const Patch&, const Patch&) = default;
};

// The instruments one section of a song plays (the chart's Intro, Verse 1,
// Chorus...), found by the section's name and which occurrence it is.
struct SectionSetup
{
    QString name;       // as the chart names it, e.g. "Verse 1" (matched ignoring case)
    int occurrence = 1; // 2 = the second section with this name
    int bars = 0;       // its length; 0 = guessed from the chart
    // false: the default (the patch's first instrument). true: `channels`,
    // which may be empty (a silent break).
    bool assigned = false;
    std::vector<ChannelId> channels;

    friend bool operator==(const SectionSetup&, const SectionSetup&) = default;
};

// A web page for a song (a chord sheet, a video...). Opened in the browser.
struct SongLink
{
    QString title;
    QString url; // http or https only

    friend bool operator==(const SongLink&, const SongLink&) = default;
};

struct Song
{
    SongId id;
    QString name;
    std::vector<Patch> patches;

    // What the player follows on stage. `chart` is ChordPro: lyrics with
    // chords in brackets over the words they change on ("[Dm]I love
    // [C#m7]you"). It may be lyrics only, chords only, or empty.
    QString chart;
    QString key;       // e.g. "Dm"; empty = not set
    double tempo = 0.0; // beats per minute; 0 = not set
    QString notes;
    std::vector<SongLink> links;
    // Files (PDF chord sheets, Guitar Pro, MIDI...) kept in the setlist's
    // own folder, by file name.
    std::vector<QString> attachments;
    // An audio file played along (WAV, MP3, FLAC...), by file name in the
    // setlist's folder like attachments; empty = none.
    QString backingTrack;
    // For counting the song's bars: beats per bar and the beat's note value.
    int timeNumerator = 4;
    int timeDenominator = 4;
    // Sections switch a beat before their first beat instead of just before it.
    bool switchEarly = false;
    // Loops start and stop on the bars (true) or press to press (free).
    bool loopSync = true;
    // What the chart's sections play; a section not listed plays the default.
    std::vector<SectionSetup> sections;

    friend bool operator==(const Song&, const Song&) = default;
};

// A keyboard button, pad, pedal or knob learned for a control: the kind of
// MIDI message (0 = none, 0xB0 controller, 0x90 note, 0xC0 program), its
// channel (1-16) and number (0-127).
struct LearnedControl
{
    int kind = 0;
    int channel = 0;
    int number = 0;

    [[nodiscard]] bool isSet() const { return kind != 0; }
    friend bool operator==(const LearnedControl&, const LearnedControl&) = default;
};

// The looper's keyboard controls, kept with the setlist (each project its own).
struct LoopControls
{
    enum Button : std::uint8_t { Record, PlayStop, Undo, StopAll, NextChannel, PreviousChannel, ButtonCount };
    enum KnobMode : std::uint8_t { Absolute, Relative, RelativeOffset }; // 0-127 / 1-63 up, 65-127 down / 64 +- n
    std::array<LearnedControl, ButtonCount> buttons{};
    LearnedControl selector; // a knob choosing the instrument (a controller)
    int selectorMode = Absolute;

    friend bool operator==(const LoopControls&, const LoopControls&) = default;
};

struct Setlist
{
    std::vector<Song> songs;
    LoopControls loopControls;

    friend bool operator==(const Setlist&, const Setlist&) = default;
};

// New song with a fresh id and a single patch named "Patch 1".
Song makeSong(const QString& name);
// New patch with a fresh id and no channels.
Patch makePatch(const QString& name);
// New channel with a fresh id, default settings and no plugins.
Channel makeChannel(const QString& name);

// Copies with new ids for the object and everything inside it (for duplicate).
Song withFreshIds(Song song);
Patch withFreshIds(Patch patch);
Channel withFreshIds(Channel channel);

} // namespace gigchain::core
