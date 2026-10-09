#pragma once

#include "gigchain/core/Ids.h"
#include "gigchain/core/Model.h"

#include <QString>

#include <vector>

namespace gigchain::core {

// Which plugin instance each slot of a patch plays, as a key: slots with the
// same key play one loaded instance.
//
// A song's patches share its plugins (the same plugin, the nth time a patch
// uses it, is one instance). Across songs, slots linked by a share id play
// one instance, as MainStage's channel strip aliases: a piano used in twenty
// songs is loaded once, and a change to it changes every song that plays
// it. Duplicating a song links its copy; a song can take its own copy back.
// Within one patch, the same plugin used twice is two instances.
struct PluginUse
{
    int channel = 0;
    int effect = -1; // -1 = the channel's instrument, else its effect's position
    const PluginSlot* slot = nullptr;
    QString key;
};

// The patch's playing slots in order (each channel's instrument, then its
// effects; a bypassed effect is not played and has no instance).
[[nodiscard]] std::vector<PluginUse> pluginUses(const SongId& song, const Patch& patch);

// The songs (by index, in order) that play the instance with this key.
[[nodiscard]] std::vector<int> songsUsing(const Setlist& setlist, const QString& key);

// Gives every slot of the song a share id (the slots its patches already
// share get the same one), so copies of them play the same instances.
void linkForSharing(Song& song);

// The song's slots linked with the share id go back to the song's own
// instances (the song's patches still share them).
void unlinkFromSharing(Song& song, const QString& shareId);

} // namespace gigchain::core
