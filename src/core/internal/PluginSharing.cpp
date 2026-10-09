#include "gigchain/core/PluginSharing.h"

#include <QUuid>

#include <map>

using namespace Qt::StringLiterals;

namespace gigchain::core {

namespace {

// Each slot the patch plays (const or not), with its role ("i" or "fx").
template <typename PatchT, typename Visit>
void eachPlayingSlot(PatchT& patch, Visit visit)
{
    for (std::size_t c = 0; c < patch.channels.size(); ++c) {
        auto& channel = patch.channels.at(c);
        if (channel.instrument) visit(static_cast<int>(c), -1, *channel.instrument, u"i"_s);
        for (std::size_t e = 0; e < channel.effects.size(); ++e) {
            auto& effect = channel.effects.at(e);
            if (!effect.bypass) visit(static_cast<int>(c), static_cast<int>(e), effect, u"fx"_s);
        }
    }
}

} // namespace

std::vector<PluginUse> pluginUses(const SongId& song, const Patch& patch)
{
    std::vector<PluginUse> uses;
    std::map<QString, int> counted; // key without its number -> how many this patch used so far
    eachPlayingSlot(patch, [&](int channel, int effect, const PluginSlot& slot, const QString& role) {
        const QString base = slot.shareId.isEmpty() ? song.value() + u'|' + role + u'|' + slot.pluginId
                                                    : u"shared|"_s + slot.shareId + u'|' + role + u'|' + slot.pluginId;
        uses.push_back(PluginUse{.channel = channel, .effect = effect, .slot = &slot,
                                 .key = base + u'#' + QString::number(counted[base]++)});
    });
    return uses;
}

std::vector<int> songsUsing(const Setlist& setlist, const QString& key)
{
    std::vector<int> songs;
    for (std::size_t s = 0; s < setlist.songs.size(); ++s) {
        const Song& song = setlist.songs.at(s);
        bool uses = false;
        for (const Patch& patch : song.patches) {
            for (const PluginUse& use : pluginUses(song.id, patch)) uses = uses || use.key == key;
        }
        if (uses) songs.push_back(static_cast<int>(s));
    }
    return songs;
}

void linkForSharing(Song& song)
{
    std::map<QString, QString> idFor; // the instance a slot plays -> its share id
    for (Patch& patch : song.patches) {
        const std::vector<PluginUse> uses = pluginUses(song.id, patch);
        std::size_t i = 0;
        eachPlayingSlot(patch, [&](int, int, PluginSlot& slot, const QString&) {
            const QString& key = uses.at(i++).key;
            if (!slot.shareId.isEmpty()) return;
            auto& id = idFor[key];
            if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            slot.shareId = id;
        });
    }
}

void unlinkFromSharing(Song& song, const QString& shareId)
{
    if (shareId.isEmpty()) return;
    for (Patch& patch : song.patches) {
        eachPlayingSlot(patch, [&](int, int, PluginSlot& slot, const QString&) {
            if (slot.shareId == shareId) slot.shareId.clear();
        });
    }
}

} // namespace gigchain::core
