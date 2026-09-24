#pragma once

#include "openstage/core/Ids.h"

#include <QString>

#include <optional>
#include <vector>

namespace openstage::core {

// A plugin placed in a channel, either as its instrument or as an effect.
struct PluginSlot
{
    QString pluginId;
    QString displayName;
    bool bypass = false;

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

struct Song
{
    SongId id;
    QString name;
    std::vector<Patch> patches;

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

} // namespace openstage::core
