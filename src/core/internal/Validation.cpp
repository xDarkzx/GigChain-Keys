#include "gigchain/core/Validation.h"

#include <QUrl>

#include "gigchain/core/Chart.h"
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
    if (slot.state.size() > limits::kMaxPluginStateBytes) {
        return fail(ErrorCode::LimitExceeded,
                    u"%1.state is larger than %2 bytes"_s.arg(path).arg(limits::kMaxPluginStateBytes));
    }
    return validateName(slot.displayName, path + ".displayName"_L1);
}

Result<void> validateLength(const QString& text, int max, const QString& path)
{
    if (text.size() > max) {
        return fail(ErrorCode::LimitExceeded, u"%1 is longer than %2 characters"_s.arg(path).arg(max));
    }
    return {};
}

} // namespace

Result<void> validateFileName(const QString& name, const QString& path)
{
    if (auto r = validateLength(name, limits::kMaxFileNameLength, path); !r) return r;
    // A plain file name inside the setlist's folder: no folders, drives or
    // "..", so a setlist cannot reach other files on the computer.
    if (name.trimmed().isEmpty() || name.contains(u'/') || name.contains(u'\\') || name.contains(u':') ||
        name == u"."_s || name == u".."_s) {
        return fail(ErrorCode::InvalidData, u"%1 must be a plain file name"_s.arg(path));
    }
    return {};
}

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
        if (auto r = validateSlot(channel.effects.at(i), u"%1.effects[%2]"_s.arg(path).arg(i)); !r) return r;
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
    if (auto r = checkRange(channel.midiChannel, limits::kMinMidiChannel, limits::kMaxMidiChannel, path + ".midiChannel"_L1);
        !r) {
        return r;
    }
    if (auto r = checkRange(channel.velocityLow, limits::kMinVelocity, limits::kMaxVelocity, path + ".velocityLow"_L1); !r) {
        return r;
    }
    if (auto r = checkRange(channel.velocityHigh, limits::kMinVelocity, limits::kMaxVelocity, path + ".velocityHigh"_L1);
        !r) {
        return r;
    }
    if (channel.velocityLow > channel.velocityHigh) {
        return fail(ErrorCode::OutOfRange, u"%1.velocityLow must not be above velocityHigh"_s.arg(path));
    }
    if (auto r = checkRange(channel.inputLeft, 0, limits::kMaxAudioInput, path + ".inputLeft"_L1); !r) return r;
    if (auto r = checkRange(channel.inputRight, 0, limits::kMaxAudioInput, path + ".inputRight"_L1); !r) return r;
    if (channel.inputLeft == 0 && channel.inputRight != 0) {
        return fail(ErrorCode::InvalidData, u"%1.inputRight needs inputLeft"_s.arg(path));
    }
    if (channel.mappings.size() > static_cast<std::size_t>(limits::kMaxMappingsPerChannel)) {
        return fail(ErrorCode::LimitExceeded,
                    u"%1 has more than %2 control mappings"_s.arg(path).arg(limits::kMaxMappingsPerChannel));
    }
    for (std::size_t i = 0; i < channel.mappings.size(); ++i) {
        const ControlMapping& m = channel.mappings.at(i);
        const QString at = u"%1.mappings[%2]"_s.arg(path).arg(i);
        if (auto r = checkRange(m.midiChannel, limits::kMinMidiChannel, limits::kMaxMidiChannel, at + ".midiChannel"_L1); !r) {
            return r;
        }
        if (auto r = checkRange(m.controller, 0, limits::kMaxController, at + ".controller"_L1); !r) return r;
        if (auto r = checkRange(m.target, -1, static_cast<int>(channel.effects.size()) - 1, at + ".target"_L1); !r) return r;
        if (!std::isfinite(m.minimum) || !std::isfinite(m.maximum) || m.minimum < 0.0 || m.minimum > 1.0 ||
            m.maximum < 0.0 || m.maximum > 1.0) {
            return fail(ErrorCode::OutOfRange, u"%1 range must be between 0 and 1"_s.arg(at));
        }
        if (auto r = validateLength(m.parameterName, limits::kMaxNameLength, at + ".parameterName"_L1); !r) return r;
    }
    return {};
}

Result<void> validateChart(const Song& song, const QString& path)
{
    if (auto r = validateLength(song.chart, limits::kMaxChartLength, path + ".chart"_L1); !r) return r;
    if (auto r = validateLength(song.notes, limits::kMaxNotesLength, path + ".notes"_L1); !r) return r;
    if (auto r = validateLength(song.key, limits::kMaxKeyLength, path + ".key"_L1); !r) return r;
    if (!std::isfinite(song.tempo) || song.tempo < 0.0 || song.tempo > limits::kMaxTempo) {
        return fail(ErrorCode::OutOfRange, u"%1.tempo must be between 0 and %2"_s.arg(path).arg(limits::kMaxTempo));
    }
    if (song.links.size() > static_cast<std::size_t>(limits::kMaxLinksPerSong)) {
        return fail(ErrorCode::LimitExceeded, u"%1 has more than %2 links"_s.arg(path).arg(limits::kMaxLinksPerSong));
    }
    for (std::size_t i = 0; i < song.links.size(); ++i) {
        const QString linkPath = u"%1.links[%2]"_s.arg(path).arg(i);
        const SongLink& link = song.links.at(i);
        if (auto r = validateLength(link.title, limits::kMaxNameLength, linkPath + ".title"_L1); !r) return r;
        if (auto r = validateLength(link.url, limits::kMaxUrlLength, linkPath + ".url"_L1); !r) return r;
        // Links are opened in the browser: a setlist must not be able to
        // start programs or scripts (file:, javascript:...).
        const QUrl url(link.url, QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != u"https"_s && url.scheme() != u"http"_s) || url.host().isEmpty()) {
            return fail(ErrorCode::InvalidData, u"%1.url must be a web address (http or https)"_s.arg(linkPath));
        }
    }
    if (song.attachments.size() > static_cast<std::size_t>(limits::kMaxAttachmentsPerSong)) {
        return fail(ErrorCode::LimitExceeded,
                    u"%1 has more than %2 attachments"_s.arg(path).arg(limits::kMaxAttachmentsPerSong));
    }
    for (std::size_t i = 0; i < song.attachments.size(); ++i) {
        if (auto r = validateFileName(song.attachments.at(i), u"%1.attachments[%2]"_s.arg(path).arg(i)); !r) return r;
    }
    if (!song.backingTrack.isEmpty()) {
        if (auto r = validateFileName(song.backingTrack, path + ".backingTrack"_L1); !r) return r;
    }
    return validateSections(song, path);
}

Result<void> validateSections(const Song& song, const QString& path)
{
    if (!isTimeSignature(song.timeNumerator, song.timeDenominator)) {
        return fail(ErrorCode::OutOfRange, u"%1.timeSignature %2/%3 is not a time signature (1-32 beats of 1, 2, 4, 8, 16 or 32)"_s
                                               .arg(path)
                                               .arg(song.timeNumerator)
                                               .arg(song.timeDenominator));
    }
    if (song.loopBars < 0 || song.loopBars > limits::kMaxLoopBars) {
        return fail(ErrorCode::OutOfRange, u"%1.loopBars must be between 0 (open) and %2"_s.arg(path).arg(limits::kMaxLoopBars));
    }
    if (song.sections.size() > static_cast<std::size_t>(limits::kMaxSectionsPerSong)) {
        return fail(ErrorCode::LimitExceeded,
                    u"%1 has more than %2 sections"_s.arg(path).arg(limits::kMaxSectionsPerSong));
    }
    for (std::size_t i = 0; i < song.sections.size(); ++i) {
        const SectionSetup& section = song.sections.at(i);
        const QString where = u"%1.sections[%2]"_s.arg(path).arg(i);
        if (auto r = validateName(section.name, where + ".name"_L1); !r) return r;
        if (section.occurrence < 1 || section.occurrence > limits::kMaxSectionOccurrence) {
            return fail(ErrorCode::OutOfRange,
                        u"%1.occurrence must be between 1 and %2"_s.arg(where).arg(limits::kMaxSectionOccurrence));
        }
        if (section.bars < 0 || section.bars > limits::kMaxSectionBars) {
            return fail(ErrorCode::OutOfRange, u"%1.bars must be between 0 and %2"_s.arg(where).arg(limits::kMaxSectionBars));
        }
        if (section.channels.size() > static_cast<std::size_t>(limits::kMaxChannelsPerPatch)) {
            return fail(ErrorCode::LimitExceeded,
                        u"%1 lists more than %2 channels"_s.arg(where).arg(limits::kMaxChannelsPerPatch));
        }
        for (std::size_t c = 0; c < section.channels.size(); ++c) {
            if (auto r = validateId(section.channels.at(c).value(), u"%1.channels[%2]"_s.arg(where).arg(c)); !r) return r;
        }
    }
    return {};
}

Result<void> validateLoopControls(const LoopControls& controls)
{
    const auto check = [](const LearnedControl& c, const QString& where, bool knob) -> Result<void> {
        if (!c.isSet()) return {};
        const bool known = knob ? c.kind == 0xB0 : (c.kind == 0xB0 || c.kind == 0x90 || c.kind == 0xC0);
        if (!known) {
            return fail(ErrorCode::InvalidData,
                        u"%1 is not a %2"_s.arg(where, knob ? u"keyboard knob"_s : u"keyboard button, pad or pedal"_s));
        }
        if (c.channel < 1 || c.channel > 16 || c.number < 0 || c.number > 127) {
            return fail(ErrorCode::OutOfRange, u"%1 needs a MIDI channel 1-16 and a number 0-127"_s.arg(where));
        }
        return {};
    };
    for (std::size_t i = 0; i < controls.buttons.size(); ++i) {
        if (auto r = check(controls.buttons.at(i), u"loopControls.buttons[%1]"_s.arg(i), false); !r) return r;
    }
    if (auto r = check(controls.selector, u"loopControls.selector"_s, true); !r) return r;
    if (controls.selectorMode < LoopControls::Absolute || controls.selectorMode > LoopControls::RelativeOffset) {
        return fail(ErrorCode::OutOfRange, u"loopControls.selectorMode must be 0, 1 or 2"_s);
    }
    return {};
}

Result<void> validate(const Setlist& setlist)
{
    if (auto r = validateLoopControls(setlist.loopControls); !r) return r;
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
        const Song& song = setlist.songs.at(s);
        const QString songPath = u"songs[%1]"_s.arg(s);
        if (auto r = validateId(song.id.value(), songPath + ".id"_L1); !r) return r;
        if (auto r = unique(song.id.value(), songPath + ".id"_L1); !r) return r;
        if (auto r = validateName(song.name, songPath + ".name"_L1); !r) return r;
        if (auto r = validateChart(song, songPath); !r) return r;
        if (song.patches.empty()) {
            return fail(ErrorCode::InvalidData, u"%1 needs at least one patch"_s.arg(songPath));
        }
        if (song.patches.size() > static_cast<std::size_t>(limits::kMaxPatchesPerSong)) {
            return fail(ErrorCode::LimitExceeded,
                        u"%1 has more than %2 patches"_s.arg(songPath).arg(limits::kMaxPatchesPerSong));
        }
        for (std::size_t p = 0; p < song.patches.size(); ++p) {
            const Patch& patch = song.patches.at(p);
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
                if (auto r = validateChannel(patch.channels.at(c), channelPath); !r) return r;
                if (auto r = unique(patch.channels.at(c).id.value(), channelPath + ".id"_L1); !r) return r;
            }
        }
    }
    return {};
}

} // namespace gigchain::core
