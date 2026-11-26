#include "gigchain/core/Editing.h"
#include "gigchain/core/PluginSharing.h"

#include <QtTest>

#include <map>
#include <optional>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

PluginSlot piano(const QString& shareId = {})
{
    return PluginSlot{.pluginId = u"plugins/Piano.vst3"_s, .displayName = u"Piano"_s, .bypass = false, .state = "bright",
                      .shareId = shareId};
}

Song songWith(const QString& name, const std::vector<PluginSlot>& instruments)
{
    Song song = makeSong(name);
    for (const PluginSlot& instrument : instruments) {
        Channel channel = makeChannel(instrument.displayName);
        channel.instrument = instrument;
        song.patches.front().channels.push_back(channel);
    }
    return song;
}

// A channel's instrument's share id ("-" for no instrument: never a real one).
QString shareOf(const Channel& channel)
{
    return channel.instrument ? channel.instrument->shareId : u"-"_s;
}

QString keyOf(const Song& song, int channel, std::size_t patch = 0)
{
    const std::vector<PluginUse> uses = pluginUses(song.id, song.patches.at(patch));
    const auto found = std::ranges::find_if(uses, [channel](const PluginUse& use) { return use.channel == channel && use.effect < 0; });
    return found != uses.end() ? found->key : QString();
}

} // namespace

class TestPluginSharing : public QObject
{
    Q_OBJECT

private slots:
    // Slots linked by a share id play one instance whatever the song; the
    // same plugin without a link is each song's own, even with the same
    // settings (plugins write their instance's name into them).
    void linkedSlotsShareOneInstanceAcrossSongs()
    {
        const Song ballad = songWith(u"Ballad"_s, {piano(u"grand"_s)});
        const Song funk = songWith(u"Funk"_s, {piano(u"grand"_s)});
        const Song blues = songWith(u"Blues"_s, {piano()});
        const Song jazz = songWith(u"Jazz"_s, {piano()});
        QCOMPARE(keyOf(ballad, 0), keyOf(funk, 0));
        QVERIFY(keyOf(blues, 0) != keyOf(jazz, 0));
        QVERIFY(keyOf(ballad, 0) != keyOf(blues, 0));

        Setlist setlist;
        setlist.songs = {ballad, funk, blues};
        QCOMPARE(songsUsing(setlist, keyOf(ballad, 0)), (std::vector<int>{0, 1}));
        QCOMPARE(songsUsing(setlist, keyOf(blues, 0)), (std::vector<int>{2}));
        QVERIFY(songsUsing(setlist, u"nothing"_s).empty());
    }

    // A song's patches share its plugins (by how many times a patch used
    // it); the same plugin twice in one patch (a layer) is two instances;
    // a bypassed effect has none.
    void aSongsPatchesShareAndALayerIsTwoInstances()
    {
        Song song = songWith(u"Layered"_s, {piano(), piano()});
        song.patches.front().channels.at(0).effects = {
            PluginSlot{.pluginId = u"plugins/Reverb.vst3"_s, .displayName = u"Reverb"_s, .bypass = false, .state = "hall", .shareId = {}},
            PluginSlot{.pluginId = u"plugins/Delay.vst3"_s, .displayName = u"Delay"_s, .bypass = true, .state = "echo", .shareId = {}}};
        song.patches.push_back(song.patches.front());
        const auto uses = pluginUses(song.id, song.patches.front());
        QCOMPARE(uses.size(), std::size_t{3}); // piano, its reverb, the second piano
        QCOMPARE(uses.at(1).effect, 0);
        QCOMPARE(uses.at(2).channel, 1);
        QVERIFY(uses.at(0).key != uses.at(2).key);
        QCOMPARE(keyOf(song, 0, 1), keyOf(song, 0, 0));
    }

    // Linking gives each instance of a song one share id (its patches keep
    // sharing); unlinking takes a song's slots back to its own instances.
    void linkingAndUnlinkingASong()
    {
        Song song = songWith(u"Ballad"_s, {piano(), piano()});
        song.patches.push_back(song.patches.front());
        linkForSharing(song);
        const auto& first = song.patches.at(0).channels;
        const auto& second = song.patches.at(1).channels;
        QVERIFY(!shareOf(first.at(0)).isEmpty());
        QCOMPARE(shareOf(second.at(0)), shareOf(first.at(0))); // one instance, one id
        QVERIFY(shareOf(first.at(1)) != shareOf(first.at(0))); // the layer is another
        QCOMPARE(keyOf(song, 0, 1), keyOf(song, 0, 0));

        const QString linked = shareOf(first.at(0));
        linkForSharing(song); // already linked: kept
        QCOMPARE(shareOf(first.at(0)), linked);

        unlinkFromSharing(song, linked);
        QVERIFY(shareOf(first.at(0)).isEmpty());
        QVERIFY(shareOf(second.at(0)).isEmpty());
        QVERIFY(!shareOf(first.at(1)).isEmpty()); // another instance: untouched
        QCOMPARE(keyOf(song, 0, 1), keyOf(song, 0, 0));      // still shared by the song's patches
    }

    // A duplicated song plays the same instruments as its original.
    void aDuplicatedSongSharesItsInstruments()
    {
        Setlist setlist;
        setlist.songs = {songWith(u"Ballad"_s, {piano()})};
        const auto copy = duplicateSong(setlist, 0);
        QVERIFY(copy.has_value());
        QCOMPARE(keyOf(setlist.songs.at(1), 0), keyOf(setlist.songs.at(0), 0));
        QCOMPARE(songsUsing(setlist, keyOf(setlist.songs.at(0), 0)), (std::vector<int>{0, 1}));
    }

    // Between two versions of a setlist (an undo of a strip moved, #94):
    // the key each instance had and has now, by the slot that plays it, so
    // every slot gets its own back. What cannot be told one to one (an
    // instance shared, or split) is left out (relinkInstances takes it).
    void instanceRenamesFollowEachSlot()
    {
        Setlist before;
        before.songs = {songWith(u"Ballad"_s, {piano(), piano()})};
        Setlist after = before;
        QVERIFY(instanceRenames(before, after).empty()); // nothing changed
        const auto renamed = moveChannel(after, Cursor{0, 0}, 1, 0);
        QVERIFY(renamed.has_value() && !renamed->empty());
        QCOMPARE(instanceRenames(before, after), *renamed);
        std::map<QString, QString> back;
        for (const auto& [oldKey, newKey] : *renamed) back.emplace(newKey, oldKey);
        QCOMPARE(instanceRenames(after, before), back); // undone: each back to its own

        // Two songs' own instances becoming one (linked) or one becoming two: left out.
        Setlist own;
        own.songs = {songWith(u"Ballad"_s, {piano()}), songWith(u"Funk"_s, {piano()})};
        Setlist merged = own;
        for (Song& song : merged.songs) {
            std::optional<PluginSlot>& instrument = song.patches.at(0).channels.at(0).instrument;
            QVERIFY(instrument.has_value());
            if (instrument) instrument->shareId = u"one"_s;
        }
        QCOMPARE(keyOf(merged.songs.at(0), 0), keyOf(merged.songs.at(1), 0)); // (one instance)
        QVERIFY(instanceRenames(own, merged).empty());
        QVERIFY(instanceRenames(merged, own).empty());
        Setlist other;
        other.songs = {songWith(u"Funk"_s, {piano()})}; // another song: nothing in common
        QVERIFY(instanceRenames(before, other).empty());
    }
};

QTEST_GUILESS_MAIN(TestPluginSharing)
#include "tst_plugin_sharing.moc"
