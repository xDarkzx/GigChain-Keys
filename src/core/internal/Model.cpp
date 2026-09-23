#include "openstage/core/Model.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace openstage::core {

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
    for (Patch& patch : song.patches) {
        patch = withFreshIds(std::move(patch));
    }
    return song;
}

} // namespace openstage::core
