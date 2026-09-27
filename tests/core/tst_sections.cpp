// Song sections: what each section of the chart plays in a patch.
#include "gigchain/core/Editing.h"
#include "gigchain/core/Sections.h"

#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

// Piano, Strings, a vocal mic (not an instrument) and Lead, with a chart of
// Intro, Verse, Chorus, Verse, Chorus.
Song fourChannelSong()
{
    Song song = makeSong(u"Song"_s);
    Patch& patch = song.patches.front();
    Channel mic = makeChannel(u"Vocal"_s);
    mic.inputLeft = 1;
    patch.channels.push_back(mic); // first, but not an instrument
    for (const QString& name : {u"Piano"_s, u"Strings"_s, u"Lead"_s}) {
        Channel c = makeChannel(name);
        c.instrument = PluginSlot{.pluginId = u"fake."_s + name, .displayName = name, .bypass = false, .state = {}};
        patch.channels.push_back(c);
    }
    song.chart = u"{comment: Intro}\n[C] [G]\n{comment: Verse}\n[C]words [G]more\n{comment: Chorus}\n[F] [G] [C]\n"
                 "{comment: Verse}\n[Am]words\n{comment: Chorus}\n[F]la\n"_s;
    return song;
}

const ChannelId& idOf(const Song& song, int channel)
{
    return song.patches.front().channels.at(static_cast<std::size_t>(channel)).id;
}

} // namespace

class TestSections : public QObject
{
    Q_OBJECT

private slots:
    void everySectionPlaysTheFirstInstrumentByDefault()
    {
        const Song song = fourChannelSong();
        const auto sections = resolveSections(song, song.patches.front());
        QCOMPARE(sections.size(), std::size_t{5});
        for (const ResolvedSection& s : sections) {
            QVERIFY(!s.assigned);
            QVERIFY(s.guessed);
            QCOMPARE(s.live, std::vector<ChannelId>{idOf(song, 1)}); // Piano: the mic is not an instrument
        }
        QCOMPARE(sections.at(2).bars, 3);
        QCOMPARE(sections.at(3).chart.name, u"Verse"_s);
        QCOMPARE(sections.at(3).chart.occurrence, 2);
    }

    void aSectionPlaysWhatItIsGiven()
    {
        Song song = fourChannelSong();
        Setlist setlist;
        setlist.songs = {song};
        // The second chorus: Strings and Lead (listed out of order), 8 bars.
        QVERIFY(setSectionSetup(setlist, 0, SectionSetup{.name = u"chorus"_s, .occurrence = 2, .bars = 8,
                                                         .assigned = true, .channels = {idOf(song, 3), idOf(song, 2)}}));
        // The first verse: nothing (a break).
        QVERIFY(setSectionSetup(setlist, 0, SectionSetup{.name = u"Verse"_s, .occurrence = 1, .bars = 0,
                                                         .assigned = true, .channels = {}}));
        const Song& edited = setlist.songs.front();
        const auto sections = resolveSections(edited, edited.patches.front());
        QVERIFY(sections.at(1).live.empty());
        QVERIFY(sections.at(1).assigned);
        QCOMPARE(sections.at(4).live, (std::vector<ChannelId>{idOf(song, 2), idOf(song, 3)})); // the patch's order
        QCOMPARE(sections.at(4).bars, 8);
        QVERIFY(!sections.at(4).guessed);
        QCOMPARE(sections.at(2).live, std::vector<ChannelId>{idOf(song, 1)}); // the first chorus: still the default

        // Setting the same section again replaces it.
        QVERIFY(setSectionSetup(setlist, 0, SectionSetup{.name = u"CHORUS"_s, .occurrence = 2, .bars = 4,
                                                         .assigned = false, .channels = {}}));
        QCOMPARE(setlist.songs.front().sections.size(), std::size_t{2});
        QCOMPARE(resolveSections(setlist.songs.front(), setlist.songs.front().patches.front()).at(4).bars, 4);
    }

    void channelsOfAnotherPatchFallBackToTheDefault()
    {
        Song song = fourChannelSong();
        song.sections.push_back(SectionSetup{.name = u"Intro"_s, .occurrence = 1, .bars = 0, .assigned = true,
                                             .channels = {ChannelId(u"elsewhere"_s)}});
        const auto sections = resolveSections(song, song.patches.front());
        QCOMPARE(sections.at(0).live, std::vector<ChannelId>{idOf(song, 1)});
    }

    void aChartWithoutSectionsHasNone()
    {
        Song song = fourChannelSong();
        song.chart = u"[C]just words [G]and chords\n"_s;
        QVERIFY(resolveSections(song, song.patches.front()).empty());
        song.chart.clear();
        QVERIFY(resolveSections(song, song.patches.front()).empty());
    }

    void aPatchWithoutInstrumentsPlaysNothingByDefault()
    {
        Song song = fourChannelSong();
        song.patches.front().channels.resize(1); // only the mic
        const auto sections = resolveSections(song, song.patches.front());
        QCOMPARE(sections.size(), std::size_t{5});
        QVERIFY(sections.at(0).live.empty());
        QVERIFY(!firstInstrument(song.patches.front()));
    }

    void badSetupsAreRefusedAndChangeNothing()
    {
        Setlist setlist;
        setlist.songs = {fourChannelSong()};
        const Setlist before = setlist;
        QVERIFY(!setSectionSetup(setlist, 0, SectionSetup{.name = u"Verse"_s, .occurrence = 1, .bars = 1000,
                                                          .assigned = false, .channels = {}}));
        QVERIFY(!setSectionSetup(setlist, 0, SectionSetup{.name = u"  "_s, .occurrence = 1, .bars = 4,
                                                          .assigned = false, .channels = {}}));
        QVERIFY(!setSectionSetup(setlist, 0, SectionSetup{.name = u"Verse"_s, .occurrence = 0, .bars = 4,
                                                          .assigned = false, .channels = {}}));
        QVERIFY(!setSectionSetup(setlist, 3, SectionSetup{.name = u"Verse"_s, .occurrence = 1, .bars = 4,
                                                          .assigned = false, .channels = {}}));
        QVERIFY(!setSongTimeSignature(setlist, 0, 5, 5));
        QVERIFY(!setSongTimeSignature(setlist, 0, 0, 4));
        QVERIFY(!setSongSwitchEarly(setlist, 2, true));
        QVERIFY(setlist == before);
        QVERIFY(setSongTimeSignature(setlist, 0, 6, 8));
        QVERIFY(setSongSwitchEarly(setlist, 0, true));
        QCOMPARE(setlist.songs.front().timeNumerator, 6);
        QCOMPARE(setlist.songs.front().timeDenominator, 8);
        QVERIFY(setlist.songs.front().switchEarly);
    }

    void aCopiedSongKeepsItsSections()
    {
        Setlist setlist;
        setlist.songs = {fourChannelSong()};
        const Song& original = setlist.songs.front();
        QVERIFY(setSectionSetup(setlist, 0, SectionSetup{.name = u"Chorus"_s, .occurrence = 1, .bars = 0,
                                                         .assigned = true, .channels = {idOf(original, 2)}}));
        const auto copy = duplicateSong(setlist, 0);
        QVERIFY(copy);
        const Song& song = setlist.songs.at(1);
        QVERIFY(idOf(song, 2) != idOf(setlist.songs.at(0), 2)); // fresh ids...
        QCOMPARE(song.sections.front().channels, std::vector<ChannelId>{idOf(song, 2)}); // ... followed
        QCOMPARE(resolveSections(song, song.patches.front()).at(2).live, std::vector<ChannelId>{idOf(song, 2)});
    }
};

QTEST_GUILESS_MAIN(TestSections)
#include "tst_sections.moc"
