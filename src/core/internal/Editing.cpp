#include "gigchain/core/Editing.h"

#include "gigchain/core/Chart.h"
#include "gigchain/core/Limits.h"

#include <cmath>
#include "gigchain/core/Validation.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

constexpr auto kCopySuffix = " (copy)"_L1;

bool inRange(int index, std::size_t size)
{
    return index >= 0 && std::cmp_less(index, size);
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
    Song& song = setlist.songs.at(toIndex(songIndex));
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
    setlist.songs.at(toIndex(songIndex)).name = *clean;
    return {};
}

Result<void> setSongChart(Setlist& setlist, int songIndex, const QString& chart)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    if (chart.size() > limits::kMaxChartLength) {
        return fail(ErrorCode::LimitExceeded,
                    u"The chart is longer than %1 characters"_s.arg(limits::kMaxChartLength));
    }
    setlist.songs.at(toIndex(songIndex)).chart = chart;
    return {};
}

Result<void> setSongKeyAndTempo(Setlist& setlist, int songIndex, const QString& key, double tempo)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    if (key.size() > limits::kMaxKeyLength) {
        return fail(ErrorCode::LimitExceeded, u"The key is longer than %1 characters"_s.arg(limits::kMaxKeyLength));
    }
    if (!std::isfinite(tempo) || tempo < 0.0 || tempo > limits::kMaxTempo) {
        return fail(ErrorCode::OutOfRange, u"The tempo must be between 0 and %1"_s.arg(limits::kMaxTempo));
    }
    Song& song = setlist.songs.at(toIndex(songIndex));
    song.key = key.trimmed();
    song.tempo = tempo;
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

Result<void> setSongTimeSignature(Setlist& setlist, int songIndex, int numerator, int denominator)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    if (!isTimeSignature(numerator, denominator)) {
        return fail(ErrorCode::OutOfRange,
                    u"%1/%2 is not a time signature (1-32 beats of a 1, 2, 4, 8, 16 or 32 note)"_s.arg(numerator).arg(denominator));
    }
    Song& song = setlist.songs.at(toIndex(songIndex));
    song.timeNumerator = numerator;
    song.timeDenominator = denominator;
    return {};
}

Result<void> setSongSwitchEarly(Setlist& setlist, int songIndex, bool early)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    setlist.songs.at(toIndex(songIndex)).switchEarly = early;
    return {};
}

Result<void> setSongLoopSync(Setlist& setlist, int songIndex, bool sync)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    setlist.songs.at(toIndex(songIndex)).loopSync = sync;
    return {};
}

Result<void> setSongLoopBars(Setlist& setlist, int songIndex, int bars)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    if (bars < 0 || bars > limits::kMaxLoopBars) {
        return fail(ErrorCode::OutOfRange, u"A loop is 1 to %1 bars long (or open)"_s.arg(limits::kMaxLoopBars));
    }
    setlist.songs.at(toIndex(songIndex)).loopBars = bars;
    return {};
}

Result<void> setLoopControls(Setlist& setlist, const LoopControls& controls)
{
    if (auto r = validateLoopControls(controls); !r) return r;
    setlist.loopControls = controls;
    return {};
}

Result<void> setSectionSetup(Setlist& setlist, int songIndex, const SectionSetup& setup)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    Song edited = setlist.songs.at(toIndex(songIndex));
    SectionSetup clean = setup;
    clean.name = clean.name.simplified();
    const auto same = std::ranges::find_if(edited.sections, [&clean](const SectionSetup& s) {
        return s.occurrence == clean.occurrence && s.name.compare(clean.name, Qt::CaseInsensitive) == 0;
    });
    if (same != edited.sections.end()) *same = clean;
    else edited.sections.push_back(clean);
    if (auto r = validateSections(edited, u"Song %1"_s.arg(songIndex + 1)); !r) return r;
    setlist.songs.at(toIndex(songIndex)) = std::move(edited);
    return {};
}

Result<int> duplicateSong(Setlist& setlist, int songIndex)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    if (setlist.songs.size() >= toIndex(limits::kMaxSongs)) {
        return fail(ErrorCode::LimitExceeded, u"A setlist can hold at most %1 songs"_s.arg(limits::kMaxSongs));
    }
    Song copy = withFreshIds(setlist.songs.at(toIndex(songIndex)));
    copy.name = copyName(copy.name);
    setlist.songs.insert(setlist.songs.begin() + songIndex + 1, std::move(copy));
    return songIndex + 1;
}

Result<int> duplicatePatch(Setlist& setlist, Cursor cursor)
{
    const Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    Song& song = setlist.songs.at(toIndex(cursor.song));
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
    Song& song = setlist.songs.at(toIndex(cursor.song));
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
    auto& patches = setlist.songs.at(toIndex(songIndex)).patches;
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

Result<int> addInputChannel(Setlist& setlist, Cursor cursor, const QString& name, int inputLeft, int inputRight)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    if (patch->channels.size() >= toIndex(limits::kMaxChannelsPerPatch)) {
        return fail(ErrorCode::LimitExceeded,
                    u"A patch can hold at most %1 channels"_s.arg(limits::kMaxChannelsPerPatch));
    }
    if (inputLeft < 1) return fail(ErrorCode::OutOfRange, u"Choose which input the channel plays"_s);
    Channel channel = makeChannel(name.trimmed());
    channel.inputLeft = inputLeft;
    channel.inputRight = inputRight;
    if (auto valid = validateChannel(channel, u"New channel"_s); !valid) return tl::unexpected(valid.error());
    patch->channels.push_back(std::move(channel));
    return static_cast<int>(patch->channels.size()) - 1;
}

Result<void> setSongBackingTrack(Setlist& setlist, int songIndex, const QString& fileName)
{
    if (songIndex < 0 || static_cast<std::size_t>(songIndex) >= setlist.songs.size()) return missing(u"Song"_s);
    if (!fileName.isEmpty()) {
        if (auto valid = validateFileName(fileName, u"The backing track"_s); !valid) return valid;
    }
    setlist.songs.at(static_cast<std::size_t>(songIndex)).backingTrack = fileName;
    return {};
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
    if (!inRange(effectIndex, patch->channels.at(toIndex(channelIndex)).effects.size())) {
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

    Channel updated = patch->channels.at(toIndex(channelIndex));
    edit(updated);
    updated.name = updated.name.trimmed();
    if (auto valid = validateChannel(updated, u"Channel"_s); !valid) return valid;
    patch->channels.at(toIndex(channelIndex)) = std::move(updated);
    return {};
}

} // namespace gigchain::core
