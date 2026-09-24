#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/core/Model.h"
#include "gigchain/core/Navigation.h"

#include <functional>

// Setlist edits. Each function validates its input and leaves the setlist
// unchanged when it returns an error. Names are trimmed.
namespace gigchain::core {

Result<int> addSong(Setlist& setlist, const QString& name);
Result<int> addPatch(Setlist& setlist, int songIndex, const QString& name);
Result<void> renameSong(Setlist& setlist, int songIndex, const QString& name);
Result<void> renamePatch(Setlist& setlist, Cursor cursor, const QString& name);

// Inserts the copy directly after the original and returns its index.
Result<int> duplicateSong(Setlist& setlist, int songIndex);
Result<int> duplicatePatch(Setlist& setlist, Cursor cursor);

Result<void> removeSong(Setlist& setlist, int songIndex);
// A song keeps at least one patch; removing the last one is refused.
Result<void> removePatch(Setlist& setlist, Cursor cursor);

// `to` is the final index of the moved item.
Result<void> moveSong(Setlist& setlist, int from, int to);
Result<void> movePatch(Setlist& setlist, int songIndex, int from, int to);

// New channel named after the instrument. Returns its index.
Result<int> addChannel(Setlist& setlist, Cursor cursor, const PluginSlot& instrument);
Result<void> removeChannel(Setlist& setlist, Cursor cursor, int channelIndex);
Result<void> addEffect(Setlist& setlist, Cursor cursor, int channelIndex, const PluginSlot& effect);
Result<void> removeEffect(Setlist& setlist, Cursor cursor, int channelIndex, int effectIndex);

// Applies `edit` to a copy of the channel and commits it only if the result
// validates, so a bad value from the UI can never enter the model.
Result<void> updateChannel(Setlist& setlist, Cursor cursor, int channelIndex,
                           const std::function<void(Channel&)>& edit);

} // namespace gigchain::core
