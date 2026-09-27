#include "gigchain/core/Model.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::core {

Song makeSong(const QString& name)
{
    Song song;
    song.id = SongId::generate();
    song.name = name;
    song.patches.push_back(makePatch(u"Patch 1"_s));
    return song;
}

Patch makePatch(const QString& name)
{
    Patch patch;
    patch.id = PatchId::generate();
    patch.name = name;
    return patch;
}

Channel makeChannel(const QString& name)
{
    Channel channel;
    channel.id = ChannelId::generate();
    channel.name = name;
    return channel;
}

Channel withFreshIds(Channel channel)
{
    channel.id = ChannelId::generate();
    return channel;
}

Patch withFreshIds(Patch patch)
{
    patch.id = PatchId::generate();
    for (Channel& channel : patch.channels) {
        channel = withFreshIds(std::move(channel));
    }
    return patch;
}

Song withFreshIds(Song song)
{
    song.id = SongId::generate();
    // The sections follow their channels to the copies' new ids.
    std::vector<std::pair<ChannelId, ChannelId>> renamed;
    for (Patch& patch : song.patches) {
        const Patch before = patch;
        patch = withFreshIds(std::move(patch));
        for (std::size_t c = 0; c < patch.channels.size(); ++c) {
            renamed.emplace_back(before.channels.at(c).id, patch.channels.at(c).id);
        }
    }
    for (SectionSetup& section : song.sections) {
        for (ChannelId& id : section.channels) {
            const auto it = std::ranges::find_if(renamed, [&id](const auto& pair) { return pair.first == id; });
            if (it != renamed.end()) id = it->second;
        }
    }
    return song;
}

} // namespace gigchain::core
