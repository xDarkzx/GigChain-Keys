#include "LeakCheck.h"
#include "gigchain/core/Editing.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/Validation.h"

#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

Setlist abc()
{
    Setlist setlist;
    for (const QString& name : {u"A"_s, u"B"_s, u"C"_s}) {
        setlist.songs.push_back(makeSong(name));
    }
    return setlist;
}

QStringList songNames(const Setlist& setlist)
{
    QStringList names;
    for (const Song& song : setlist.songs) names << song.name;
    return names;
}

const PluginSlot kPiano{.pluginId = u"fake.grand-piano"_s, .displayName = u"Grand Piano"_s, .bypass = false, .state = {}};
const PluginSlot kReverb{.pluginId = u"fake.reverb"_s, .displayName = u"Reverb"_s, .bypass = false, .state = {}};

} // namespace

class TestEditing : public QObject
{
    Q_OBJECT

private slots:
    void addSongAppendsWithOnePatch()
    {
        Setlist s;
        const auto index = addSong(s, u"  Opener  "_s);
        QVERIFY(index.has_value());
        QCOMPARE(*index, 0);
        QCOMPARE(s.songs[0].name, u"Opener"_s);
        QCOMPARE(s.songs[0].patches.size(), std::size_t{1});
    }

    void addSongRejectsBadNamesAndLimits()
    {
        Setlist s;
        QVERIFY(addSong(s, u"   "_s).error().code == ErrorCode::InvalidData);
        QVERIFY(addSong(s, QString(limits::kMaxNameLength + 1, u'x')).error().code == ErrorCode::LimitExceeded);
        for (int i = 0; i < limits::kMaxSongs; ++i) QVERIFY(addSong(s, u"S"_s).has_value());
        QVERIFY(addSong(s, u"One too many"_s).error().code == ErrorCode::LimitExceeded);
        QCOMPARE(s.songs.size(), std::size_t(limits::kMaxSongs));
    }

    void addPatchChecksSongAndLimit()
    {
        Setlist s = abc();
        QCOMPARE(*addPatch(s, 1, u"Chorus"_s), 1);
        QCOMPARE(s.songs[1].patches[1].name, u"Chorus"_s);
        QVERIFY(addPatch(s, 7, u"X"_s).error().code == ErrorCode::OutOfRange);
        while (s.songs.at(0).patches.size() < static_cast<std::size_t>(limits::kMaxPatchesPerSong)) {
            QVERIFY(addPatch(s, 0, u"P"_s).has_value());
        }
        QVERIFY(addPatch(s, 0, u"P"_s).error().code == ErrorCode::LimitExceeded);
    }

    void renamesTrimAndValidate()
    {
        Setlist s = abc();
        QVERIFY(renameSong(s, 0, u" Intro "_s).has_value());
        QCOMPARE(s.songs[0].name, u"Intro"_s);
        QVERIFY(renamePatch(s, Cursor{0, 0}, u"Verse"_s).has_value());
        QCOMPARE(s.songs[0].patches[0].name, u"Verse"_s);
        QVERIFY(renamePatch(s, Cursor{0, 0}, u""_s).error().code == ErrorCode::InvalidData);
        QCOMPARE(s.songs[0].patches[0].name, u"Verse"_s);
        QVERIFY(renameSong(s, 3, u"X"_s).error().code == ErrorCode::OutOfRange);
    }

    void duplicateSongInsertsCopyWithFreshIds()
    {
        Setlist s = abc();
        QCOMPARE(*duplicateSong(s, 0), 1);
        QCOMPARE(songNames(s), (QStringList{u"A"_s, u"A (copy)"_s, u"B"_s, u"C"_s}));
        QVERIFY(s.songs[1].id != s.songs[0].id);
        QVERIFY(validate(s).has_value());
    }

    void duplicatePatchKeepsNameWithinLimit()
    {
        Setlist s = abc();
        QVERIFY(renamePatch(s, Cursor{0, 0}, QString(limits::kMaxNameLength, u'x')).has_value());
        QCOMPARE(*duplicatePatch(s, Cursor{0, 0}), 1);
        QCOMPARE(s.songs[0].patches[1].name.size(), qsizetype(limits::kMaxNameLength));
        QVERIFY(s.songs[0].patches[1].name.endsWith(u" (copy)"));
        QVERIFY(validate(s).has_value());
    }

    void removePatchRefusesLastPatch()
    {
        Setlist s = abc();
        QVERIFY(removePatch(s, Cursor{0, 0}).error().code == ErrorCode::InvalidData);
        QVERIFY(addPatch(s, 0, u"Two"_s).has_value());
        QVERIFY(removePatch(s, Cursor{0, 0}).has_value());
        QCOMPARE(s.songs[0].patches[0].name, u"Two"_s);
    }

    void removeSongChecksIndex()
    {
        Setlist s = abc();
        QVERIFY(removeSong(s, 1).has_value());
        QCOMPARE(songNames(s), (QStringList{u"A"_s, u"C"_s}));
        QVERIFY(removeSong(s, 2).error().code == ErrorCode::OutOfRange);
    }

    void moveSongReorders()
    {
        Setlist s = abc();
        QVERIFY(moveSong(s, 0, 2).has_value());
        QCOMPARE(songNames(s), (QStringList{u"B"_s, u"C"_s, u"A"_s}));
        QVERIFY(moveSong(s, 2, 0).has_value());
        QCOMPARE(songNames(s), (QStringList{u"A"_s, u"B"_s, u"C"_s}));
        QVERIFY(moveSong(s, 0, 3).error().code == ErrorCode::OutOfRange);
    }

    void movePatchReorders()
    {
        Setlist s = abc();
        QVERIFY(addPatch(s, 0, u"Second"_s).has_value());
        QVERIFY(movePatch(s, 0, 1, 0).has_value());
        QCOMPARE(s.songs[0].patches[0].name, u"Second"_s);
        QVERIFY(movePatch(s, 0, 0, 2).error().code == ErrorCode::OutOfRange);
    }

    void addChannelUsesInstrumentName()
    {
        Setlist s = abc();
        QCOMPARE(*addChannel(s, Cursor{0, 0}, kPiano), 0);
        const Channel& c = s.songs[0].patches[0].channels[0];
        QCOMPARE(c.name, u"Grand Piano"_s);
        QVERIFY(c.instrument == kPiano);
        QVERIFY(addChannel(s, Cursor{5, 0}, kPiano).error().code == ErrorCode::OutOfRange);
        QVERIFY(addChannel(s, Cursor{0, 0}, PluginSlot{}).error().code == ErrorCode::InvalidData);
    }

    // The same instrument twice in a sound (a piano for the verse, the same
    // piano with another preset for the chorus): told apart by a number.
    void theSameInstrumentTwiceIsNumbered()
    {
        Setlist s = abc();
        QVERIFY(addChannel(s, Cursor{0, 0}, kPiano).has_value());
        QVERIFY(addChannel(s, Cursor{0, 0}, kPiano).has_value());
        QVERIFY(addChannel(s, Cursor{0, 0}, kPiano).has_value());
        const auto& channels = s.songs.at(0).patches.at(0).channels;
        QCOMPARE(channels.at(0).name, u"Grand Piano"_s);
        QCOMPARE(channels.at(1).name, u"Grand Piano 2"_s);
        QCOMPARE(channels.at(2).name, u"Grand Piano 3"_s);
        QCOMPARE(channels.at(1).instrument.value_or(PluginSlot{}).displayName, u"Grand Piano"_s); // the plugin is still the plugin
        QVERIFY(addChannel(s, Cursor{1, 0}, kPiano).has_value()); // another sound: its own first
        QCOMPARE(s.songs.at(1).patches.at(0).channels.at(0).name, u"Grand Piano"_s);
    }

    void effectsAddAndRemove()
    {
        Setlist s = abc();
        QVERIFY(addChannel(s, Cursor{0, 0}, kPiano).has_value());
        QVERIFY(addEffect(s, Cursor{0, 0}, 0, kReverb).has_value());
        QCOMPARE(s.songs[0].patches[0].channels[0].effects.size(), std::size_t{1});
        QVERIFY(removeEffect(s, Cursor{0, 0}, 0, 3).error().code == ErrorCode::OutOfRange);
        QVERIFY(removeEffect(s, Cursor{0, 0}, 0, 0).has_value());
        QVERIFY(s.songs[0].patches[0].channels[0].effects.empty());
        for (int i = 0; i < limits::kMaxEffectsPerChannel; ++i) {
            QVERIFY(addEffect(s, Cursor{0, 0}, 0, kReverb).has_value());
        }
        QVERIFY(addEffect(s, Cursor{0, 0}, 0, kReverb).error().code == ErrorCode::LimitExceeded);
    }

    void updateChannelRejectsInvalidEditAndKeepsOriginal()
    {
        Setlist s = abc();
        QVERIFY(addChannel(s, Cursor{0, 0}, kPiano).has_value());
        const Channel before = s.songs[0].patches[0].channels[0];
        const auto result = updateChannel(s, Cursor{0, 0}, 0, [](Channel& c) {
            c.keyLow = 80;
            c.keyHigh = 20;
        });
        QVERIFY(!result);
        QVERIFY(result.error().code == ErrorCode::OutOfRange);
        QVERIFY(s.songs[0].patches[0].channels[0] == before);

        QVERIFY(updateChannel(s, Cursor{0, 0}, 0, [](Channel& c) { c.transpose = 12; }).has_value());
        QCOMPARE(s.songs[0].patches[0].channels[0].transpose, 12);
        QVERIFY(updateChannel(s, Cursor{0, 0}, 4, [](Channel&) {}).error().code == ErrorCode::OutOfRange);
    }

    void removeChannelChecksIndex()
    {
        Setlist s = abc();
        QVERIFY(addChannel(s, Cursor{0, 0}, kPiano).has_value());
        QVERIFY(removeChannel(s, Cursor{0, 0}, 1).error().code == ErrorCode::OutOfRange);
        QVERIFY(removeChannel(s, Cursor{0, 0}, 0).has_value());
        QVERIFY(s.songs[0].patches[0].channels.empty());
    }

    void editingDoesNotLeak()
    {
        Setlist s = abc();
        QCOMPARE(gigchain::test::leakedBlocks([&s] {
                     const auto index = addSong(s, u"Temp"_s);
                     QVERIFY(index.has_value());
                     QVERIFY(addChannel(s, Cursor{*index, 0}, kPiano).has_value());
                     QVERIFY(duplicateSong(s, *index).has_value());
                     QVERIFY(removeSong(s, *index + 1).has_value());
                     QVERIFY(removeSong(s, *index).has_value());
                 }),
                 0LL);
    }

    // The inversion chosen for a chord of a song; -1 forgets it.
    void anInversionIsChosenForAChord()
    {
        Setlist s = abc();
        QVERIFY(setChordInversion(s, 0, u"E/D#"_s, 1).has_value());
        QCOMPARE(s.songs.at(0).chordInversions.at(u"E/D#"_s), 1);
        QVERIFY(s.songs.at(1).chordInversions.empty());
        QVERIFY(setChordInversion(s, 0, u"E/D#"_s, -1).has_value());
        QVERIFY(s.songs.at(0).chordInversions.empty());
        QVERIFY(setChordInversion(s, 0, u"C"_s, 4).error().code == ErrorCode::OutOfRange); // 0 to 3
        QVERIFY(setChordInversion(s, 0, u""_s, 1).error().code == ErrorCode::InvalidData);
        QVERIFY(setChordInversion(s, 7, u"C"_s, 1).error().code == ErrorCode::OutOfRange);
    }

    // A section renamed: the song's flow and the section's instruments go
    // with it (and the chorus that was the second is now the first).
    void aRenamedSectionKeepsItsFlowAndInstruments()
    {
        Setlist s = abc();
        Song& song = s.songs.at(0);
        song.chart = u"{comment: Chorus}\n[C]a\n{comment: Verse}\n[Am]b\n{comment: Chorus}\n[F]c\n"_s;
        song.flow = {{.name = u"Chorus"_s}, {.name = u"Verse"_s}, {.name = u"Chorus"_s, .occurrence = 2}};
        song.sections = {SectionSetup{.name = u"chorus"_s, .occurrence = 2, .bars = 8, .assigned = true, .channels = {}}};
        const QString renamed = u"{comment: Intro}\n[C]a\n{comment: Verse}\n[Am]b\n{comment: Chorus}\n[F]c\n"_s;
        QVERIFY(renameSongSection(s, 0, renamed).has_value());
        QCOMPARE(song.chart, renamed);
        QStringList flow;
        for (const SectionRef& part : song.flow) flow << u"%1 %2"_s.arg(part.name).arg(part.occurrence);
        QCOMPARE(flow, (QStringList{u"Intro 1"_s, u"Verse 1"_s, u"Chorus 1"_s}));
        QCOMPARE(song.sections.at(0).name, u"Chorus"_s);
        QCOMPARE(song.sections.at(0).occurrence, 1);
        QCOMPARE(song.sections.at(0).bars, 8);
        QVERIFY(renameSongSection(s, 9, renamed).error().code == ErrorCode::OutOfRange);
    }
};

QTEST_GUILESS_MAIN(TestEditing)
#include "tst_editing.moc"
