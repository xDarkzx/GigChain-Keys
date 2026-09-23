#pragma once

#include "openstage/core/Model.h"

#include <optional>

namespace openstage::core {

// A position in a setlist. Invalid (-1, -1) when the setlist has no patches.
struct Cursor
{
    int song = -1;
    int patch = -1;

    [[nodiscard]] bool isValid() const { return song >= 0 && patch >= 0; }

    friend bool operator==(const Cursor&, const Cursor&) = default;
};

Cursor firstPatch(const Setlist& setlist);

// Returns `cursor` if it points at a patch; otherwise the nearest sensible
// patch (same song clamped, else last song with patches, else first patch).
Cursor clampCursor(const Setlist& setlist, Cursor cursor);

// Moves one patch forward/back, crossing song boundaries. Stays put at the
// ends of the set (no wrap-around).
Cursor nextPatch(const Setlist& setlist, Cursor cursor);
Cursor previousPatch(const Setlist& setlist, Cursor cursor);

// Jumps to the first patch of the next/previous song. Stays put if none.
Cursor nextSong(const Setlist& setlist, Cursor cursor);
Cursor previousSong(const Setlist& setlist, Cursor cursor);

std::optional<Cursor> findPatch(const Setlist& setlist, const PatchId& id);

// Non-owning. Valid until the setlist is next modified. nullptr when the
// cursor does not point at a patch.
const Patch* patchAt(const Setlist& setlist, Cursor cursor);
Patch* patchAt(Setlist& setlist, Cursor cursor);

} // namespace openstage::core
