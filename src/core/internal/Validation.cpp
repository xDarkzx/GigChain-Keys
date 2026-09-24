#include "gigchain/core/Validation.h"

#include "gigchain/core/Limits.h"

#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

Result<void> checkRange(int value, int min, int max, const QString& path)
{
    if (value < min || value > max) {
        return fail(ErrorCode::OutOfRange,
                    u"%1 must be between %2 and %3 (got %4)"_s.arg(path).arg(min).arg(max).arg(value));
    }
    return {};
}

Result<void> validateId(const QString& id, const QString& path)
{
    if (id.isEmpty()) {
        return fail(ErrorCode::InvalidData, u"%1 must not be empty"_s.arg(path));
    }
    if (id.size() > limits::kMaxIdLength) {
        return fail(ErrorCode::LimitExceeded, u"%1 is longer than %2 characters"_s.arg(path).arg(limits::kMaxIdLength));
    }
    return {};
}

Result<void> validateSlot(const PluginSlot& slot, const QString& path)
{
    if (slot.pluginId.isEmpty()) {
        return fail(ErrorCode::InvalidData, u"%1.pluginId must not be empty"_s.arg(path));
    }
    if (slot.pluginId.size() > limits::kMaxPluginIdLength) {
        return fail(ErrorCode::LimitExceeded,
                    u"%1.pluginId is longer than %2 characters"_s.arg(path).arg(limits::kMaxPluginIdLength));
    }
    return validateName(slot.displayName, path + ".displayName"_L1);
}

} // namespace

Result<void> validateName(const QString& name, const QString& path)
{
    if (name.trimmed().isEmpty()) {
        return fail(ErrorCode::InvalidData, u"%1 must not be empty"_s.arg(path));
    }
    if (name.size() > limits::kMaxNameLength) {
        return fail(ErrorCode::LimitExceeded, u"%1 is longer than %2 characters"_s.arg(path).arg(limits::kMaxNameLength));
    }
    return {};
}

Result<void> validateChannel(const Channel& channel, const QString& path)
{
    if (auto r = validateId(channel.id.value(), path + ".id"_L1); !r) return r;
    if (auto r = validateName(channel.name, path + ".name"_L1); !r) return r;
    if (channel.instrument) {
        if (auto r = validateSlot(*channel.instrument, path + ".instrument"_L1); !r) return r;
    }
    if (channel.effects.size() > static_cast<std::size_t>(limits::kMaxEffectsPerChannel)) {
        return fail(ErrorCode::LimitExceeded,
                    u"%1 has more than %2 effects"_s.arg(path).arg(limits::kMaxEffectsPerChannel));
    }
    for (std::size_t i = 0; i < channel.effects.size(); ++i) {
        if (auto r = validateSlot(channel.effects[i], u"%1.effects[%2]"_s.arg(path).arg(i)); !r) return r;
    }
    if (!std::isfinite(channel.volumeDb) || channel.volumeDb < limits::kMinVolumeDb ||
        channel.volumeDb > limits::kMaxVolumeDb) {
        return fail(ErrorCode::OutOfRange, u"%1.volumeDb must be between %2 and %3 dB"_s.arg(path)
                                               .arg(limits::kMinVolumeDb)
                                               .arg(limits::kMaxVolumeDb));
    }
    if (!std::isfinite(channel.pan) || channel.pan < limits::kMinPan || channel.pan > limits::kMaxPan) {
        return fail(ErrorCode::OutOfRange, u"%1.pan must be between -1 and 1"_s.arg(path));
    }
    if (auto r = checkRange(channel.keyLow, limits::kMinMidiNote, limits::kMaxMidiNote, path + ".keyLow"_L1); !r) return r;
    if (auto r = checkRange(channel.keyHigh, limits::kMinMidiNote, limits::kMaxMidiNote, path + ".keyHigh"_L1); !r) return r;
    if (channel.keyLow > channel.keyHigh) {
        return fail(ErrorCode::OutOfRange, u"%1.keyLow must not be above keyHigh"_s.arg(path));
    }
    if (auto r = checkRange(channel.transpose, limits::kMinTranspose, limits::kMaxTranspose, path + ".transpose"_L1); !r) return r;
    return checkRange(channel.midiChannel, limits::kMinMidiChannel, limits::kMaxMidiChannel, path + ".midiChannel"_L1);
}

Result<void> validate(const Setlist& setlist)
{
    if (setlist.songs.size() > static_cast<std::size_t>(limits::kMaxSongs)) {
        return fail(ErrorCode::LimitExceeded, u"A setlist can hold at most %1 songs"_s.arg(limits::kMaxSongs));
    }

    std::set<QString> seenIds;
    const auto unique = [&seenIds](const QString& id, const QString& path) -> Result<void> {
        if (!seenIds.insert(id).second) {
            return fail(ErrorCode::InvalidData, u"%1 repeats an id used elsewhere in the setlist"_s.arg(path));
        }
        return {};
    };

    for (std::size_t s = 0; s < setlist.songs.size(); ++s) {
        const Song& song = setlist.songs[s];
        const QString songPath = u"songs[%1]"_s.arg(s);
        if (auto r = validateId(song.id.value(), songPath + ".id"_L1); !r) return r;
        if (auto r = unique(song.id.value(), songPath + ".id"_L1); !r) return r;
        if (auto r = validateName(song.name, songPath + ".name"_L1); !r) return r;
        if (song.patches.empty()) {
            return fail(ErrorCode::InvalidData, u"%1 needs at least one patch"_s.arg(songPath));
        }
        if (song.patches.size() > static_cast<std::size_t>(limits::kMaxPatchesPerSong)) {
            return fail(ErrorCode::LimitExceeded,
                        u"%1 has more than %2 patches"_s.arg(songPath).arg(limits::kMaxPatchesPerSong));
        }
        for (std::size_t p = 0; p < song.patches.size(); ++p) {
            const Patch& patch = song.patches[p];
            const QString patchPath = u"%1.patches[%2]"_s.arg(songPath).arg(p);
            if (auto r = validateId(patch.id.value(), patchPath + ".id"_L1); !r) return r;
            if (auto r = unique(patch.id.value(), patchPath + ".id"_L1); !r) return r;
            if (auto r = validateName(patch.name, patchPath + ".name"_L1); !r) return r;
            if (patch.channels.size() > static_cast<std::size_t>(limits::kMaxChannelsPerPatch)) {
                return fail(ErrorCode::LimitExceeded,
                            u"%1 has more than %2 channels"_s.arg(patchPath).arg(limits::kMaxChannelsPerPatch));
            }
            for (std::size_t c = 0; c < patch.channels.size(); ++c) {
                const QString channelPath = u"%1.channels[%2]"_s.arg(patchPath).arg(c);
                if (auto r = validateChannel(patch.channels[c], channelPath); !r) return r;
                if (auto r = unique(patch.channels[c].id.value(), channelPath + ".id"_L1); !r) return r;
            }
        }
    }
    return {};
}

} // namespace gigchain::core
