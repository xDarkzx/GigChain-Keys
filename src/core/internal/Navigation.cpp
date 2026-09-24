#include "gigchain/core/Navigation.h"

#include <algorithm>

namespace gigchain::core {
namespace {

int songCount(const Setlist& setlist)
{
    return static_cast<int>(setlist.songs.size());
}

int patchCount(const Setlist& setlist, int song)
{
    if (song < 0 || song >= songCount(setlist)) return 0;
    return static_cast<int>(setlist.songs[static_cast<std::size_t>(song)].patches.size());
}

bool pointsAtPatch(const Setlist& setlist, Cursor cursor)
{
    return cursor.patch >= 0 && cursor.patch < patchCount(setlist, cursor.song);
}

} // namespace

Cursor firstPatch(const Setlist& setlist)
{
    for (int song = 0; song < songCount(setlist); ++song) {
        if (patchCount(setlist, song) > 0) return Cursor{song, 0};
    }
    return {};
}

Cursor clampCursor(const Setlist& setlist, Cursor cursor)
{
    if (pointsAtPatch(setlist, cursor)) return cursor;
    const int count = patchCount(setlist, cursor.song);
    if (count > 0) return Cursor{cursor.song, std::clamp(cursor.patch, 0, count - 1)};
    if (cursor.song >= songCount(setlist)) {
        for (int song = songCount(setlist) - 1; song >= 0; --song) {
            if (patchCount(setlist, song) > 0) return Cursor{song, 0};
        }
    }
    return firstPatch(setlist);
}

Cursor nextPatch(const Setlist& setlist, Cursor cursor)
{
    cursor = clampCursor(setlist, cursor);
    if (!cursor.isValid()) return cursor;
    if (cursor.patch + 1 < patchCount(setlist, cursor.song)) return Cursor{cursor.song, cursor.patch + 1};
    for (int song = cursor.song + 1; song < songCount(setlist); ++song) {
        if (patchCount(setlist, song) > 0) return Cursor{song, 0};
    }
    return cursor;
}

Cursor previousPatch(const Setlist& setlist, Cursor cursor)
{
    cursor = clampCursor(setlist, cursor);
    if (!cursor.isValid()) return cursor;
    if (cursor.patch > 0) return Cursor{cursor.song, cursor.patch - 1};
    for (int song = cursor.song - 1; song >= 0; --song) {
        const int count = patchCount(setlist, song);
        if (count > 0) return Cursor{song, count - 1};
    }
    return cursor;
}

Cursor nextSong(const Setlist& setlist, Cursor cursor)
{
    cursor = clampCursor(setlist, cursor);
    if (!cursor.isValid()) return cursor;
    for (int song = cursor.song + 1; song < songCount(setlist); ++song) {
        if (patchCount(setlist, song) > 0) return Cursor{song, 0};
    }
    return cursor;
}

Cursor previousSong(const Setlist& setlist, Cursor cursor)
{
    cursor = clampCursor(setlist, cursor);
    if (!cursor.isValid()) return cursor;
    for (int song = cursor.song - 1; song >= 0; --song) {
        if (patchCount(setlist, song) > 0) return Cursor{song, 0};
    }
    return cursor;
}

std::optional<Cursor> findPatch(const Setlist& setlist, const PatchId& id)
{
    for (int song = 0; song < songCount(setlist); ++song) {
        const auto& patches = setlist.songs[static_cast<std::size_t>(song)].patches;
        for (std::size_t patch = 0; patch < patches.size(); ++patch) {
            if (patches[patch].id == id) return Cursor{song, static_cast<int>(patch)};
        }
    }
    return std::nullopt;
}

const Patch* patchAt(const Setlist& setlist, Cursor cursor)
{
    if (!pointsAtPatch(setlist, cursor)) return nullptr;
    return &setlist.songs[static_cast<std::size_t>(cursor.song)].patches[static_cast<std::size_t>(cursor.patch)];
}

Patch* patchAt(Setlist& setlist, Cursor cursor)
{
    if (!pointsAtPatch(setlist, cursor)) return nullptr;
    return &setlist.songs[static_cast<std::size_t>(cursor.song)].patches[static_cast<std::size_t>(cursor.patch)];
}

} // namespace gigchain::core
