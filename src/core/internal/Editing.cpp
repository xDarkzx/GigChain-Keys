#include "openstage/core/Editing.h"

#include "openstage/core/Limits.h"
#include "openstage/core/Validation.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace openstage::core {
namespace {

constexpr auto kCopySuffix = " (copy)"_L1;

bool inRange(int index, std::size_t size)
{
    return index >= 0 && static_cast<std::size_t>(index) < size;
}

std::size_t toIndex(int index)
{
    return static_cast<std::size_t>(index);
}

tl::unexpected<Error> missing(const QString& what)
{
    return fail(ErrorCode::OutOfRange, u"%1 does not exist"_s.arg(what));
}

Result<QString> cleanName(const QString& name, const QString& what)
{
    QString trimmed = name.trimmed();
    if (auto valid = validateName(trimmed, what); !valid) return tl::unexpected(valid.error());
    return trimmed;
}

QString copyName(const QString& name)
{
    return name.left(limits::kMaxNameLength - kCopySuffix.size()) + kCopySuffix;
}

template <typename T>
void moveElement(std::vector<T>& items, int from, int to)
{
    const auto begin = items.begin();
    if (from < to) {
        std::rotate(begin + from, begin + from + 1, begin + to + 1);
    } else if (from > to) {
        std::rotate(begin + to, begin + from, begin + from + 1);
    }
}

} // namespace

Result<int> addSong(Setlist& setlist, const QString& name)
{
    auto clean = cleanName(name, u"Song name"_s);
    if (!clean) return tl::unexpected(clean.error());
    if (setlist.songs.size() >= toIndex(limits::kMaxSongs)) {
        return fail(ErrorCode::LimitExceeded, u"A setlist can hold at most %1 songs"_s.arg(limits::kMaxSongs));
    }
    setlist.songs.push_back(makeSong(*clean));
    return static_cast<int>(setlist.songs.size()) - 1;
}

Result<int> addPatch(Setlist& setlist, int songIndex, const QString& name)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    auto clean = cleanName(name, u"Patch name"_s);
    if (!clean) return tl::unexpected(clean.error());
    Song& song = setlist.songs[toIndex(songIndex)];
    if (song.patches.size() >= toIndex(limits::kMaxPatchesPerSong)) {
        return fail(ErrorCode::LimitExceeded, u"A song can hold at most %1 patches"_s.arg(limits::kMaxPatchesPerSong));
    }
    song.patches.push_back(makePatch(*clean));
    return static_cast<int>(song.patches.size()) - 1;
}

Result<void> renameSong(Setlist& setlist, int songIndex, const QString& name)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    auto clean = cleanName(name, u"Song name"_s);
    if (!clean) return tl::unexpected(clean.error());
    setlist.songs[toIndex(songIndex)].name = *clean;
    return {};
}

Result<void> renamePatch(Setlist& setlist, Cursor cursor, const QString& name)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    auto clean = cleanName(name, u"Patch name"_s);
    if (!clean) return tl::unexpected(clean.error());
    patch->name = *clean;
    return {};
}

Result<int> duplicateSong(Setlist& setlist, int songIndex)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    if (setlist.songs.size() >= toIndex(limits::kMaxSongs)) {
        return fail(ErrorCode::LimitExceeded, u"A setlist can hold at most %1 songs"_s.arg(limits::kMaxSongs));
    }
    Song copy = withFreshIds(setlist.songs[toIndex(songIndex)]);
    copy.name = copyName(copy.name);
    setlist.songs.insert(setlist.songs.begin() + songIndex + 1, std::move(copy));
    return songIndex + 1;
}

Result<int> duplicatePatch(Setlist& setlist, Cursor cursor)
{
    const Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    Song& song = setlist.songs[toIndex(cursor.song)];
    if (song.patches.size() >= toIndex(limits::kMaxPatchesPerSong)) {
        return fail(ErrorCode::LimitExceeded, u"A song can hold at most %1 patches"_s.arg(limits::kMaxPatchesPerSong));
    }
    Patch copy = withFreshIds(*patch);
    copy.name = copyName(copy.name);
    song.patches.insert(song.patches.begin() + cursor.patch + 1, std::move(copy));
    return cursor.patch + 1;
}

Result<void> removeSong(Setlist& setlist, int songIndex)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    setlist.songs.erase(setlist.songs.begin() + songIndex);
    return {};
}

Result<void> removePatch(Setlist& setlist, Cursor cursor)
{
    if (patchAt(setlist, cursor) == nullptr) return missing(u"Patch"_s);
    Song& song = setlist.songs[toIndex(cursor.song)];
    if (song.patches.size() == 1) {
        return fail(ErrorCode::InvalidData, u"A song needs at least one patch; delete the song instead"_s);
    }
    song.patches.erase(song.patches.begin() + cursor.patch);
    return {};
}

Result<void> moveSong(Setlist& setlist, int from, int to)
{
    if (!inRange(from, setlist.songs.size()) || !inRange(to, setlist.songs.size())) {
        return missing(u"Song position"_s);
    }
    moveElement(setlist.songs, from, to);
    return {};
}

Result<void> movePatch(Setlist& setlist, int songIndex, int from, int to)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    auto& patches = setlist.songs[toIndex(songIndex)].patches;
    if (!inRange(from, patches.size()) || !inRange(to, patches.size())) return missing(u"Patch position"_s);
    moveElement(patches, from, to);
    return {};
}

Result<int> addChannel(Setlist& setlist, Cursor cursor, const PluginSlot& instrument)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    if (patch->channels.size() >= toIndex(limits::kMaxChannelsPerPatch)) {
        return fail(ErrorCode::LimitExceeded,
                    u"A patch can hold at most %1 channels"_s.arg(limits::kMaxChannelsPerPatch));
    }
    Channel channel = makeChannel(instrument.displayName.trimmed());
    channel.instrument = instrument;
    if (auto valid = validateChannel(channel, u"New channel"_s); !valid) return tl::unexpected(valid.error());
    patch->channels.push_back(std::move(channel));
    return static_cast<int>(patch->channels.size()) - 1;
}

Result<void> removeChannel(Setlist& setlist, Cursor cursor, int channelIndex)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    if (!inRange(channelIndex, patch->channels.size())) return missing(u"Channel %1"_s.arg(channelIndex + 1));
    patch->channels.erase(patch->channels.begin() + channelIndex);
    return {};
}

Result<void> addEffect(Setlist& setlist, Cursor cursor, int channelIndex, const PluginSlot& effect)
{
    return updateChannel(setlist, cursor, channelIndex, [&effect](Channel& channel) {
        channel.effects.push_back(effect);
    });
}

Result<void> removeEffect(Setlist& setlist, Cursor cursor, int channelIndex, int effectIndex)
{
    const Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr || !inRange(channelIndex, patch->channels.size())) {
        return missing(u"Channel %1"_s.arg(channelIndex + 1));
    }
    if (!inRange(effectIndex, patch->channels[toIndex(channelIndex)].effects.size())) {
        return missing(u"Effect %1"_s.arg(effectIndex + 1));
    }
    return updateChannel(setlist, cursor, channelIndex, [effectIndex](Channel& channel) {
        channel.effects.erase(channel.effects.begin() + effectIndex);
    });
}

Result<void> updateChannel(Setlist& setlist, Cursor cursor, int channelIndex,
                           const std::function<void(Channel&)>& edit)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    if (!inRange(channelIndex, patch->channels.size())) return missing(u"Channel %1"_s.arg(channelIndex + 1));

    Channel updated = patch->channels[toIndex(channelIndex)];
    edit(updated);
    updated.name = updated.name.trimmed();
    if (auto valid = validateChannel(updated, u"Channel"_s); !valid) return valid;
    patch->channels[toIndex(channelIndex)] = std::move(updated);
    return {};
}

} // namespace openstage::core
