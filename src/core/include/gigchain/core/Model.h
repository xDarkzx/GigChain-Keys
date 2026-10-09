#pragma once

#include "gigchain/core/Ids.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <map>
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
    // Slots with the same share id (and plugin) in different songs play one
    // loaded instance, and a change to it changes all of them: MainStage's
    // channel strip aliases (PluginSharing.h). Empty: the slot's own song's.
    QString shareId;

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
    int curve = 0;         // how its travel is shaped (core::KnobCurve: straight, gentle or quick start)
    // It takes the parameter over only once it reaches it (no jump on stage,
    // KnobPickup.h); false: the parameter follows it at once.
    bool pickup = true;

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
    // The pedals and controllers it takes from the keyboard (MainStage's MIDI
    // input filter): a pad that ignores the sustain pedal while the piano
    // holds, say.
    bool takesSustain = true;    // CC 64
    bool takesExpression = true; // CC 11
    bool takesModWheel = true;   // CC 1
    bool takesPitchBend = true;
    bool takesAftertouch = true; // channel and key pressure
    std::vector<ControlMapping> mappings;
    // An audio input played through the channel's effects instead of an
    // instrument (a vocal mic, a guitar): 1-based input numbers of the audio
    // interface, 0 = none. Mono when only `inputLeft` is set.
    int inputLeft = 0;
    int inputRight = 0;
    // Where it plays: 0 the mix (outputs 1-2, through the master); n the
    // interface's outputs 2n+1 and 2n+2 directly (3-4, 5-6...): a pad to
    // the desk, a guide to the in-ears.
    int outputPair = 0;
    // Its MIDI effects (MidiEffects.h): one key plays a chord; held keys
    // play as an arpeggio on the song's tempo.
    int chord = 0;      // ChordTrigger
    int arpeggio = 0;   // ArpPattern (0 off)
    int arpRate = 1;    // ArpRate (an eighth)
    int arpOctaves = 1; // 1-3

    friend bool operator==(const Channel&, const Channel&) = default;
};

// A song section (Intro, Verse, Chorus...). Stepping through a song means
// stepping through its patches in order.
// What a sound plays where its song's sections do not say otherwise.
enum class PlayMode : int {
    All,      // every channel together (layers: piano, pad and synth on every chord)
    Selected, // only the selected channel (one instrument at a time, chosen live)
};

struct Patch
{
    PatchId id;
    QString name;
    std::vector<Channel> channels;
    PlayMode playMode = PlayMode::All;

    friend bool operator==(const Patch&, const Patch&) = default;
};

// The instruments one section of a song plays (the chart's Intro, Verse 1,
// Chorus...), found by the section's name and which occurrence it is.
struct SectionSetup
{
    QString name;       // as the chart names it, e.g. "Verse 1" (matched ignoring case)
    int occurrence = 1; // 2 = the second section with this name
    int bars = 0;       // its length; 0 = guessed from the chart
    // false: the default (the patch's play mode: every channel, or the
    // selected one). true: `channels`, which may be empty (a silent break).
    bool assigned = false;
    std::vector<ChannelId> channels;

    friend bool operator==(const SectionSetup&, const SectionSetup&) = default;
};

// A section of a song's chart, by name: the `occurrence`-th section with that
// name (matched ignoring case). One part of the song's flow.
struct SectionRef
{
    QString name;
    int occurrence = 1;

    friend bool operator==(const SectionRef&, const SectionRef&) = default;
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
    // A synced loop's length in bars: it closes by itself at the end (and,
    // stopped early, keeps what fills it evenly). 0 = open: stopped where wanted.
    int loopBars = 4;
    // What the chart's sections play; a section not listed plays the default.
    std::vector<SectionSetup> sections;
    // The order the song is played in (Verse 1, Chorus, Verse 2, Chorus,
    // Chorus...), sections of its chart; empty: the chart's own order.
    // Chord follow only moves forward along it.
    std::vector<SectionRef> flow;
    // The inversion the player chose for a chord of the chart (its diagram),
    // by chord name: 0 root position ... 3 third inversion. Not listed: none chosen.
    std::map<QString, int> chordInversions;

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

// Keyboard knobs and faders learned for the mixer (right-click > Learn),
// kept with the setlist: the master, and each strip's volume and pan by its
// place in the mixer (the first strip of whichever sound is playing), as a
// controller's eight faders sit over eight strips.
struct MixerControls
{
    static constexpr std::size_t kStrips = 16;
    LearnedControl master;
    std::array<LearnedControl, kStrips> volume{};
    std::array<LearnedControl, kStrips> pan{};

    friend bool operator==(const MixerControls&, const MixerControls&) = default;
};

struct Setlist
{
    std::vector<Song> songs;
    LoopControls loopControls;
    MixerControls mixerControls;

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
