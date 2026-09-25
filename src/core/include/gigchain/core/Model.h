#pragma once

#include "gigchain/core/Ids.h"

#include <QByteArray>
#include <QString>

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

// One mixer strip: an instrument, its effect chain, and how the keyboard
// reaches it. Several channels in a patch make layers and splits.
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

    friend bool operator==(const Song&, const Song&) = default;
};

struct Setlist
{
    std::vector<Song> songs;

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
