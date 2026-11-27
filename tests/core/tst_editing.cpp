#include "LeakCheck.h"
#include "gigchain/core/Chart.h"
#include "gigchain/core/Editing.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/PluginSharing.h"
#include "gigchain/core/SongMap.h"
#include "gigchain/core/Validation.h"

#include <algorithm>
#include <map>
#include <set>

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
const PluginSlot kOrgan{.pluginId = u"fake.organ"_s, .displayName = u"Organ"_s, .bypass = false, .state = {}};

// Which instance each slot of the song plays: "<patch>/<channel id>/<effect, -1 the instrument>" -> key.
std::map<QString, QString> instancesOf(const Song& song)
{
    std::map<QString, QString> played;
    for (std::size_t p = 0; p < song.patches.size(); ++p) {
        const Patch& patch = song.patches.at(p);
        for (const PluginUse& use : pluginUses(song.id, patch)) {
            const QString& channel = patch.channels.at(static_cast<std::size_t>(use.channel)).id.value();
            played[u"%1/%2/%3"_s.arg(p).arg(channel).arg(use.effect)] = use.key;
        }
    }
    return played;
}

QStringList channelNames(const Patch& patch)
{
    QStringList names;
    for (const Channel& channel : patch.channels) names << channel.name;
    return names;
}

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

    // A strip moved to another place (#94): only the order changes; every
    // plugin plays on as it was, so nothing is renamed or loaded again.
    void moveChannelReorders()
    {
        Setlist s = abc();
        const Cursor at{0, 0};
        QVERIFY(addChannel(s, at, kPiano).has_value());
        QVERIFY(addChannel(s, at, kOrgan).has_value());
        QVERIFY(addEffect(s, at, 1, kReverb).has_value());
        const Song before = s.songs.at(0);
        const auto renamed = moveChannel(s, at, 0, 1);
        QVERIFY(renamed.has_value());
        QVERIFY(renamed->empty()); // no plugin twice: each instance's key stays
        QCOMPARE(channelNames(s.songs.at(0).patches.at(0)), (QStringList{u"Organ"_s, u"Grand Piano"_s}));
        QCOMPARE(instancesOf(s.songs.at(0)), instancesOf(before));
        QVERIFY(s.songs.at(0).patches.at(0).channels.at(1).instrument.value_or(PluginSlot{}).shareId.isEmpty()); // nothing else touched

        QVERIFY(moveChannel(s, at, 1, 1).has_value()); // onto itself: nothing
        QCOMPARE(channelNames(s.songs.at(0).patches.at(0)), (QStringList{u"Organ"_s, u"Grand Piano"_s}));
        QVERIFY(moveChannel(s, at, 0, 2).error().code == ErrorCode::OutOfRange);
        QVERIFY(moveChannel(s, at, -1, 0).error().code == ErrorCode::OutOfRange);
        QVERIFY(moveChannel(s, Cursor{0, 3}, 0, 1).error().code == ErrorCode::OutOfRange);
        QCOMPARE(channelNames(s.songs.at(0).patches.at(0)), (QStringList{u"Organ"_s, u"Grand Piano"_s}));
    }

    // The same piano and reverb on two strips, and the piano shared with the
    // song's other sound: instances go by their order in a sound, so a plain
    // move would hand each strip the other's settings. Each slot keeps its
    // own instance; what changed is said, old key -> new, for the engine.
    void moveChannelKeepsEachStripsOwnPlugins()
    {
        Setlist s = abc();
        const Cursor verse{0, 0};
        QVERIFY(addChannel(s, verse, kPiano).has_value()); // A
        QVERIFY(addEffect(s, verse, 0, kReverb).has_value());
        QVERIFY(addChannel(s, verse, kPiano).has_value()); // B
        QVERIFY(addEffect(s, verse, 1, kReverb).has_value());
        QVERIFY(addPatch(s, 0, u"Chorus"_s).has_value());
        QVERIFY(addChannel(s, Cursor{0, 1}, kPiano).has_value()); // shares A's piano (the first in each)
        const Song before = s.songs.at(0);
        const std::map<QString, QString> was = instancesOf(before);
        QCOMPARE(was.at(u"1/%1/-1"_s.arg(before.patches.at(1).channels.at(0).id.value())),
                 was.at(u"0/%1/-1"_s.arg(before.patches.at(0).channels.at(0).id.value()))); // (shared, as said)

        const auto renamed = moveChannel(s, verse, 1, 0);
        QVERIFY2(renamed.has_value(), qPrintable(renamed ? QString() : renamed.error().message));
        QCOMPARE(channelNames(s.songs.at(0).patches.at(0)), (QStringList{u"Grand Piano 2"_s, u"Grand Piano"_s}));
        // Every slot, in both sounds, plays what it played: its old instance under the new key.
        const std::map<QString, QString> now = instancesOf(s.songs.at(0));
        QCOMPARE(now.size(), was.size());
        for (const auto& [slot, key] : was) {
            QVERIFY2(renamed->contains(key), qPrintable(slot));
            QCOMPARE(now.at(slot), renamed->at(key));
        }
        // ... and each old instance has one new key (none merged, none split).
        std::set<QString> newKeys;
        for (const auto& [oldKey, newKey] : *renamed) newKeys.insert(newKey);
        QCOMPARE(newKeys.size(), renamed->size());

        // From now on the order no longer decides: moving back renames nothing.
        const auto back = moveChannel(s, verse, 0, 1);
        QVERIFY(back.has_value());
        QVERIFY(back->empty());
        QCOMPARE(channelNames(s.songs.at(0).patches.at(0)), (QStringList{u"Grand Piano"_s, u"Grand Piano 2"_s}));
        QCOMPARE(instancesOf(s.songs.at(0)), now);
        QVERIFY(validate(s).has_value());
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

    // A part dragged along the flow (issue #32): the chart's sections are
    // written in the order the flow first plays them, each once; the gaps
    // between sections stay where they were; sections the flow leaves out
    // follow, as written. The flow and the sections' instruments keep to
    // each section (the second chorus written first is now the first).
    void theChartIsWrittenInTheFlowsOrder()
    {
        Setlist s = abc();
        Song& song = s.songs.at(0);
        song.chart = u"{title: Song}\n{comment: Intro}\n[Am]a\n\n{comment: Chorus}\n[C]b\n\n{comment: Verse}\n[G]c\n\n"
                     u"{comment: Chorus}\n[F]d\n\n{comment: Outro}\n[E]e\n"_s;
        song.flow = {{.name = u"Intro"_s}, {.name = u"Verse"_s}, {.name = u"Chorus"_s, .occurrence = 2},
                     {.name = u"Chorus"_s}, {.name = u"Verse"_s}, {.name = u"Chorus"_s, .occurrence = 2}};
        song.sections = {SectionSetup{.name = u"Chorus"_s, .occurrence = 2, .bars = 8, .assigned = true, .channels = {}},
                         SectionSetup{.name = u"Outro"_s, .occurrence = 1, .bars = 2, .assigned = false, .channels = {}}};
        QVERIFY(arrangeSongChart(s, 0).has_value());
        QCOMPARE(song.chart, u"{title: Song}\n{comment: Intro}\n[Am]a\n\n{comment: Verse}\n[G]c\n\n{comment: Chorus}\n[F]d\n\n"
                             u"{comment: Chorus}\n[C]b\n\n{comment: Outro}\n[E]e\n"_s);
        QStringList flow;
        for (const SectionRef& part : song.flow) flow << u"%1 %2"_s.arg(part.name).arg(part.occurrence);
        QCOMPARE(flow, (QStringList{u"Intro 1"_s, u"Verse 1"_s, u"Chorus 1"_s, u"Chorus 2"_s, u"Verse 1"_s, u"Chorus 1"_s}));
        QCOMPARE(song.sections.at(0).name, u"Chorus"_s); // ([F]d, now the first chorus)
        QCOMPARE(song.sections.at(0).occurrence, 1);
        QCOMPARE(song.sections.at(0).bars, 8);
        QCOMPARE(song.sections.at(1).name, u"Outro"_s);
        QCOMPARE(song.sections.at(1).occurrence, 1);

        // Already in order, or no flow: the chart is left as written.
        const QString arranged = song.chart;
        QVERIFY(arrangeSongChart(s, 0).has_value());
        QCOMPARE(song.chart, arranged);
        song.flow.clear();
        song.chart = u"{comment: B}\nb\n{comment: A}\na\n"_s;
        QVERIFY(arrangeSongChart(s, 0).has_value());
        QCOMPARE(song.chart, u"{comment: B}\nb\n{comment: A}\na\n"_s);
        QVERIFY(arrangeSongChart(s, 9).error().code == ErrorCode::OutOfRange);
    }

    // A section copied from one song and pasted into another (#100): its
    // lines (title, words, chords), its length, and its instruments by
    // name (the other song has other channels: those of the same name; none
    // of them there, it plays the default). Pasted after a part of the
    // flow, it is played there and written there; with no flow, it goes
    // where asked in the chart's own order, the song still with no flow.
    // A paste that cannot be made leaves the song as it was.
    void aSectionIsCopiedIntoAnotherSong()
    {
        Setlist s = abc();
        QVERIFY(addChannel(s, Cursor{0, 0}, kPiano).has_value());
        QVERIFY(addChannel(s, Cursor{0, 0}, kOrgan).has_value());
        Song& from = s.songs.at(0);
        from.chart = u"{comment: Verse}\n[Am]a\n\n{start_of_section: Hook}\n[C]Words of the [G]hook\nx2\n\n{comment: Outro}\n[E]e\n"_s;
        from.sections = {SectionSetup{.name = u"Hook"_s, .occurrence = 1, .bars = 8, .assigned = true,
                                      .channels = {from.patches.at(0).channels.at(1).id}}}; // the organ
        const auto copied = copySongSection(s, 0, 1);
        QVERIFY2(copied.has_value(), copied.has_value() ? "" : qPrintable(copied.error().message));
        QCOMPARE(copied->label, u"Hook"_s);
        QCOMPARE(copied->chordPro, u"{start_of_section: Hook}\n[C]Words of the [G]hook\nx2\n"_s);
        QCOMPARE(copied->bars, 8);
        QVERIFY(copied->assigned);
        QCOMPARE(copied->channels, (std::vector<QString>{u"Organ"_s}));
        QVERIFY(copySongSection(s, 0, 3).error().code == ErrorCode::OutOfRange);

        // Into a song with a flow (Verse, Chorus, Verse), after its first part.
        QVERIFY(addChannel(s, Cursor{1, 0}, kOrgan).has_value());
        Song& to = s.songs.at(1);
        to.chart = u"{comment: Verse}\n[D]v\n\n{comment: Chorus}\n[G]c\n"_s;
        to.flow = {{.name = u"Verse"_s}, {.name = u"Chorus"_s}, {.name = u"Verse"_s}};
        QVERIFY2(pasteSongSection(s, 1, *copied, 1).has_value(), "pasted"); // (its place in the flow: the second)
        QCOMPARE(to.chart, u"{comment: Verse}\n[D]v\n\n{start_of_section: Hook}\n[C]Words of the [G]hook\nx2\n\n{comment: Chorus}\n[G]c\n"_s);
        QStringList flow;
        for (const SectionRef& part : to.flow) flow << part.name;
        QCOMPARE(flow, (QStringList{u"Verse"_s, u"Hook"_s, u"Chorus"_s, u"Verse"_s}));
        QCOMPARE(to.sections.size(), 1u);
        QCOMPARE(to.sections.at(0).name, u"Hook"_s);
        QCOMPARE(to.sections.at(0).bars, 8);
        QVERIFY(to.sections.at(0).assigned);
        QCOMPARE(to.sections.at(0).channels, (std::vector<ChannelId>{to.patches.at(0).channels.at(0).id})); // its organ

        // Into a song with no flow and no organ: at the end, playing the default, still no flow.
        Song& third = s.songs.at(2);
        third.chart = u"{comment: Intro}\n[F]i\n"_s;
        QVERIFY(pasteSongSection(s, 2, *copied, -1).has_value());
        QCOMPARE(third.chart, u"{comment: Intro}\n[F]i\n\n{start_of_section: Hook}\n[C]Words of the [G]hook\nx2\n"_s);
        QVERIFY(third.flow.empty());
        QCOMPARE(third.sections.size(), 1u);
        QVERIFY(!third.sections.at(0).assigned);
        QCOMPARE(third.sections.at(0).bars, 8);
        // ...and once more, first (before its intro).
        QVERIFY(pasteSongSection(s, 2, *copied, 0).has_value());
        QVERIFY(third.chart.startsWith(u"{start_of_section: Hook}\n[C]Words of the [G]hook\nx2\n\n{comment: Intro}\n"_s));
        QVERIFY(third.flow.empty());
        QCOMPARE(third.sections.size(), 2u); // each Hook its own (the first and the second)

        // Too long for a chart: refused, the song as it was.
        const Song before = third;
        SectionCopy huge = *copied;
        huge.chordPro += QString(limits::kMaxChartLength, u'a');
        QVERIFY(!pasteSongSection(s, 2, huge, -1).has_value());
        QVERIFY(s.songs.at(2) == before);
        QVERIFY(pasteSongSection(s, 9, *copied, -1).error().code == ErrorCode::OutOfRange);
    }

    // No song at all (-1) is said as such, not as "Song 0 does not exist";
    // a song past the end is named as the player counts (from 1).
    void noSongIsSaidPlainly()
    {
        Setlist s = abc();
        const auto none = setSongChart(s, -1, u"[C]la"_s);
        QVERIFY(!none.has_value());
        QVERIFY(none.error().code == ErrorCode::OutOfRange);
        QCOMPARE(none.error().message, u"There is no song yet: add one first"_s);
        QCOMPARE(setSongChart(s, 7, u"[C]la"_s).error().message, u"Song 8 does not exist"_s);
    }

    // Sections written as {start_of_chorus}...{end_of_chorus} move whole
    // (their end with them); a part naming no section of the chart (renamed
    // away) is passed over and kept as it is.
    void sectionsWithAnEndMoveWhole()
    {
        Setlist s = abc();
        Song& song = s.songs.at(0);
        song.chart = u"{start_of_verse: Verse}\n[Am]a\n{end_of_verse}\n{start_of_chorus}\n[C]b\n{end_of_chorus}\n"_s;
        const std::vector<ChartSection> sections = chartSections(parseChordPro(song.chart));
        QCOMPARE(sections.size(), 2u);
        song.flow = {{.name = u"Gone"_s}, {.name = sections.at(1).name}, {.name = sections.at(0).name}};
        QVERIFY(arrangeSongChart(s, 0).has_value());
        QCOMPARE(song.chart, u"{start_of_chorus}\n[C]b\n{end_of_chorus}\n{start_of_verse: Verse}\n[Am]a\n{end_of_verse}\n"_s);
        QCOMPARE(song.flow.at(0).name, u"Gone"_s);
        QCOMPARE(chartSections(parseChordPro(song.chart)).at(0).name, sections.at(1).name);
    }

    // Charts of every shape put in a flow's order: no line is lost or made,
    // each part of the flow and each section's setup still name the same
    // music, the sections stand in the flow's order, and again changes nothing.
    void anyChartKeepsItsMusicPutInTheFlowsOrder()
    {
        QRandomGenerator random(20261108); // (fixed: a failure can be run again)
        const QStringList names{u"Verse"_s, u"Chorus"_s, u"Bridge"_s, u"Intro"_s};
        for (int round = 0; round < 400; ++round) {
            // The chart: maybe a title and a line first, then sections written
            // as a comment or as a ChordPro block, each with its own lines and a gap.
            QStringList text;
            if (random.bounded(2) == 1) text << u"{title: Song %1}"_s.arg(round);
            if (random.bounded(3) == 0) text << u"[D]before"_s;
            const int count = 1 + random.bounded(7);
            for (int s = 0; s < count; ++s) {
                const QString& name = names.at(random.bounded(static_cast<int>(names.size())));
                const bool block = random.bounded(3) == 0;
                text << (block ? u"{start_of_part: %1}"_s.arg(name) : u"{comment: %1}"_s.arg(name));
                for (int l = random.bounded(4); l > 0; --l) text << u"[C]s%1 line %2"_s.arg(s).arg(l);
                if (block) text << u"{end_of_part}"_s;
                for (int g = random.bounded(3); g > 0; --g) text << QString();
            }
            Setlist s = abc();
            Song& song = s.songs.at(0);
            song.chart = text.join(u'\n');
            const std::vector<ChartSection> sections = chartSections(parseChordPro(song.chart));
            QCOMPARE(static_cast<int>(sections.size()), count);
            const auto refTo = [&sections](int at) {
                return SectionRef{.name = sections.at(static_cast<std::size_t>(at)).name,
                                  .occurrence = sections.at(static_cast<std::size_t>(at)).occurrence};
            };
            for (int p = 1 + random.bounded(8); p > 0; --p) {
                song.flow.push_back(random.bounded(10) == 0 ? SectionRef{.name = u"Gone"_s, .occurrence = 1}
                                                            : refTo(random.bounded(count)));
            }
            for (int at = 0; at < count; ++at) {
                if (random.bounded(2) == 0) continue;
                const SectionRef ref = refTo(at);
                song.sections.push_back(SectionSetup{.name = ref.name, .occurrence = ref.occurrence, .bars = 100 + at,
                                                     .assigned = true, .channels = {}});
            }
            const QString before = song.chart;
            const std::vector<SectionRef> flowBefore = song.flow;
            const std::vector<SectionSetup> setupsBefore = song.sections;

            const QString seed = u"round %1:\n%2"_s.arg(round).arg(before);
            QVERIFY2(arrangeSongChart(s, 0).has_value(), qPrintable(seed));
            QStringList linesBefore = before.split(u'\n');
            QStringList linesAfter = song.chart.split(u'\n');
            linesBefore.sort();
            linesAfter.sort();
            QVERIFY2(linesAfter == linesBefore, qPrintable(seed));
            QCOMPARE(song.flow.size(), flowBefore.size());
            for (std::size_t p = 0; p < flowBefore.size(); ++p) {
                QVERIFY2(musicOf(song.chart, song.flow.at(p)) == musicOf(before, flowBefore.at(p)), qPrintable(seed));
            }
            QCOMPARE(song.sections.size(), setupsBefore.size());
            for (std::size_t i = 0; i < setupsBefore.size(); ++i) {
                const SectionSetup& was = setupsBefore.at(i);
                const SectionSetup& now = song.sections.at(i);
                QCOMPARE(now.bars, was.bars);
                QVERIFY2(musicOf(song.chart, {.name = now.name, .occurrence = now.occurrence})
                             == musicOf(before, {.name = was.name, .occurrence = was.occurrence}),
                         qPrintable(seed));
            }
            // The sections the flow plays come first, in the order it first plays them.
            const std::vector<ChartSection> arranged = chartSections(parseChordPro(song.chart));
            int next = 0;
            std::vector<int> seen;
            for (const SectionRef& part : song.flow) {
                const int at = sectionIndexOf(arranged, part);
                if (at < 0 || std::ranges::find(seen, at) != seen.end()) continue;
                QVERIFY2(at == next, qPrintable(seed));
                seen.push_back(at);
                ++next;
            }
            const QString once = song.chart;
            QVERIFY(arrangeSongChart(s, 0).has_value());
            QVERIFY2(song.chart == once, qPrintable(seed));
        }
    }

private:
    // A section's music: its lines from its title to the next one's, without
    // the gap closing it. Empty: no such section.
    static QString musicOf(const QString& text, const SectionRef& ref)
    {
        const Chart chart = parseChordPro(text);
        const std::vector<ChartSection> sections = chartSections(chart);
        const int at = sectionIndexOf(sections, ref);
        if (at < 0) return {};
        const auto first = static_cast<std::size_t>(sections.at(static_cast<std::size_t>(at)).line);
        std::size_t end = static_cast<std::size_t>(at) + 1 < sections.size()
                              ? static_cast<std::size_t>(sections.at(static_cast<std::size_t>(at) + 1).line)
                              : chart.lines.size();
        while (end > first + 1 && chart.lines.at(end - 1).kind == ChartLine::Kind::Blank) --end;
        QStringList lines;
        for (std::size_t l = first; l < end; ++l) lines << chart.lines.at(l).source;
        return lines.join(u'\n');
    }
};

QTEST_GUILESS_MAIN(TestEditing)
#include "tst_editing.moc"
