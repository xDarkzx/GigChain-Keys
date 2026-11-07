#include "LeakCheck.h"
#include "gigchain/core/Chart.h"
#include "gigchain/core/Editing.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/SongMap.h"
#include "gigchain/core/Validation.h"

#include <algorithm>

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
