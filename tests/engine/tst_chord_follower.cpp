// The five rules of chord follow, played as note sequences (no audio).
#include "ChordFollower.h"

#include "gigchain/core/Chords.h"

#include <QtTest>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <vector>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// Keys: middle C is 60.
constexpr int E2 = 40, C3 = 48, D3 = 50, E3 = 52, F3 = 53, G3 = 55, Gs3 = 56, A3 = 57, B3 = 59;
constexpr int C4 = 60, Cs4 = 61, D4 = 62, Ds4 = 63, E4 = 64, Fs4 = 66, G4 = 67, A4 = 69, B4 = 71;

ChordFollowMap mapOf(std::initializer_list<std::pair<const char*, int>> chords)
{
    ChordFollowMap map;
    int sections = 0;
    for (const auto& [name, section] : chords) {
        const auto shape = core::parseChordName(QString::fromLatin1(name));
        if (!shape) throw std::logic_error(name);
        map.steps.push_back(followStepOf(*shape, section));
        sections = std::max(sections, section + 1);
    }
    map.sectionStarts.assign(static_cast<std::size_t>(sections), -1);
    for (std::size_t i = map.steps.size(); i-- > 0;) {
        const int s = map.steps.at(i).section;
        if (s >= 0) map.sectionStarts.at(static_cast<std::size_t>(s)) = static_cast<int>(i);
    }
    // The song's parts in playing order: a part each time the section changes.
    for (std::size_t i = 0; i < map.steps.size(); ++i) {
        if (i == 0 || map.steps.at(i).section != map.steps.at(i - 1).section) map.partStarts.push_back(static_cast<int>(i));
        map.steps.at(i).part = static_cast<int>(map.partStarts.size()) - 1;
    }
    return map;
}

MidiEvent key(uint8_t status, int note, int velocity, int offset = 0)
{
    return MidiEvent{.status = status, .data1 = static_cast<uint8_t>(note), .data2 = static_cast<uint8_t>(velocity), .sampleOffset = offset};
}

// A player at the keyboard: each call is one 10 ms block (1 sample = 1 ms).
struct Player
{
    ChordFollower follower;
    ChordFollowMap map;
    uint64_t generation = 1;
    SectionGate gate;
    std::vector<MidiEvent> handover;
    std::vector<MidiEvent> events;

    explicit Player(ChordFollowMap songMap) : map(std::move(songMap)) {}
    void block(int ms = 10)
    {
        gate = follower.process(&map, generation, events, ms, 1000.0);
        const auto moved = follower.handover();
        handover.assign(moved.begin(), moved.end());
        events.clear();
    }
    void press(std::initializer_list<int> keys, int offset = 0)
    {
        std::ranges::transform(keys, std::back_inserter(events), [offset](int k) { return key(0x90, k, 100, offset); });
        block();
    }
    void release(std::initializer_list<int> keys)
    {
        std::ranges::transform(keys, std::back_inserter(events), [](int k) { return key(0x80, k, 0); });
        block();
    }
    void pedal(bool down)
    {
        events.push_back(key(0xB0, 64, down ? 127 : 0));
        block();
    }
    void wait(int ms) { block(ms); }
    // Plays a chord key by key, lets it go, and waits until it is forgotten.
    void chord(std::initializer_list<int> keys)
    {
        for (const int k : keys) press({k});
        release(keys);
        wait(600);
    }
    [[nodiscard]] int step() const { return follower.position().step; }
    [[nodiscard]] bool started() const { return follower.position().started; }
    [[nodiscard]] int section() const { return follower.position().section; }
};

} // namespace

class TestChordFollower : public QObject
{
    Q_OBJECT

private slots:
    void itWaitsForTheFirstChord()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 0}}));
        p.block();
        QVERIFY(p.follower.position().active);
        QVERIFY(!p.started());
        QCOMPARE(p.section(), 0); // the first section is in force before the start
        p.press({C4, G4});        // not Am
        QVERIFY(!p.started());
        p.press({A3}); // A + C: the root and one more
        QVERIFY(p.started());
        QCOMPARE(p.step(), 0);
    }

    // Rule 2, and every way of playing it.
    void theRootAndOneMoreMovesOn_data()
    {
        QTest::addColumn<QList<int>>("keys");
        QTest::newRow("root position") << QList<int>{G4, B4, D4};
        QTest::newRow("inversion") << QList<int>{B3, D4, G4};
        QTest::newRow("bass note left, chord right") << QList<int>{G3 - 12, B3, D4, G4};
        QTest::newRow("octave left") << QList<int>{G3 - 24, G3 - 12, B3, D4};
        QTest::newRow("root and fifth left") << QList<int>{G3 - 12, D3, B3, G4};
        QTest::newRow("only root and third") << QList<int>{G3, B3};
        QTest::newRow("power chord") << QList<int>{G3, D4};
        QTest::newRow("with a melody note") << QList<int>{G3, B3, D4, A4};
        QTest::newRow("one wrong note") << QList<int>{G3, B3, Cs4};
    }
    void theRootAndOneMoreMovesOn()
    {
        QFETCH(QList<int>, keys);
        Player p(mapOf({{"C", 0}, {"G", 0}}));
        p.chord({C4, E4});
        QCOMPARE(p.step(), 0);
        for (const int k : keys) p.press({k});
        QCOMPARE(p.step(), 1);
    }

    void aMinorSeventhTakesSusPowerAndWrongThird_data()
    {
        QTest::addColumn<QList<int>>("keys");
        QTest::newRow("G#m7") << QList<int>{Gs3, B3, Ds4, Fs4};
        QTest::newRow("G#5") << QList<int>{Gs3, Ds4};
        QTest::newRow("G#sus4") << QList<int>{Gs3, Cs4, Ds4};
        QTest::newRow("G# major") << QList<int>{Gs3, C4, Ds4};
    }
    void aMinorSeventhTakesSusPowerAndWrongThird()
    {
        QFETCH(QList<int>, keys);
        Player p(mapOf({{"C", 0}, {"G#m7", 0}})); // C shares no note with G#m7
        p.chord({C3, E3});
        QCOMPARE(p.step(), 0);
        for (const int k : keys) p.press({k});
        QCOMPARE(p.step(), 1);
    }

    void aBrokenChordAddsUp()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}}));
        p.press({A3});
        p.release({A3});
        p.wait(200);
        p.press({C4}); // A was let go 210 ms ago: still counts
        QCOMPARE(p.step(), 0);
    }

    // A chord changed in one move (legato: the old keys up and the new down
    // in the same few milliseconds, or the audio running late and handing
    // both over in one block): the new chord is heard, not the old one too.
    void aChordChangedInOneMoveMovesOn()
    {
        Player p(mapOf({{"Am", 0}, {"G", 0}, {"F", 1}, {"C", 1}}));
        p.press({A3, C4, E4});
        QCOMPARE(p.step(), 0);
        p.wait(500);
        for (const int k : {A3, C4, E4}) p.events.push_back(key(0x80, k, 0));
        for (const int k : {G3, B3, D4}) p.events.push_back(key(0x90, k, 100));
        p.block();
        QCOMPARE(p.step(), 1);
    }

    void aKeyLetGoLongAgoIsForgotten()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}}));
        p.press({A3});
        p.release({A3});
        p.wait(600);
        p.press({C4});
        QVERIFY(!p.started());
    }

    void thePedalKeepsKeys()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}}));
        p.pedal(true);
        p.press({A3});
        p.release({A3});
        p.wait(900);
        p.press({E4});
        QCOMPARE(p.step(), 0);
    }

    void holdingAChordNeverMovesOn()
    {
        Player p(mapOf({{"C", 0}, {"G", 0}}));
        p.press({C4, E4, G4}); // G is in C, but G's root alone is not "the root and one more"
        for (int i = 0; i < 50; ++i) p.wait(20);
        QCOMPARE(p.step(), 0);
    }

    void aSlashChordNeedsItsBass()
    {
        Player p(mapOf({{"C", 0}, {"C/E", 0}}));
        p.chord({C4, E4, G4});
        QCOMPARE(p.step(), 0);
        p.chord({C3, E4, G4}); // C at the bottom: still C
        QCOMPARE(p.step(), 0);
        p.press({E2, C4, G4}); // E at the bottom
        QCOMPARE(p.step(), 1);
    }

    void oneStrayChordNeverJumps()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 0}, {"G", 0}, {"D", 1}, {"Bm", 1}}));
        p.chord({A3, C4, E4});
        p.chord({D4, Fs4, A4}); // the chorus's first chord, alone
        QCOMPARE(p.step(), 0);
        QCOMPARE(p.section(), 0);
        p.press({G3, B3, D4}); // then something else
        QCOMPARE(p.step(), 0);
        QCOMPARE(p.section(), 0);
    }

    void twoChordsOfASectionJumpThere()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 0}, {"G", 0}, {"D", 1}, {"Bm", 1}}));
        p.chord({A3, C4, E4});
        p.chord({D4, Fs4, A4});
        p.press({B3, D4, Fs4}, 7);
        QCOMPARE(p.step(), 5);
        QCOMPARE(p.section(), 1);
        QCOMPARE(p.gate.before, 0);
        QCOMPARE(p.gate.after, 1);
        QCOMPARE(p.gate.switchAt, 7);
    }

    void sameOpeningGoesToTheNextOne()
    {
        // Verse 1 (C G), Chorus (F Bb), Verse 2 (C G), Bridge (Dm A).
        Player p(mapOf({{"C", 0}, {"G", 0}, {"F", 1}, {"Bb", 1}, {"C", 2}, {"G", 2}, {"Dm", 3}, {"A", 3}}));
        // From the chorus, a verse's opening goes to Verse 2 (the next one)...
        p.block(); // (the song chosen: a section asked before it would be dropped)
        p.follower.jumpToSection(1);
        p.block();
        p.chord({C4, E4, G4});
        p.chord({G3, B3, D4});
        QCOMPARE(p.section(), 2);
        QCOMPARE(p.step(), 5);
        // ... and from the bridge (the last part), it never goes back round:
        // the song's flow only goes forward.
        p.follower.jumpToSection(3);
        p.block();
        p.chord({C4, E4, G4});
        p.chord({G3, B3, D4});
        QCOMPARE(p.section(), 3);
        QCOMPARE(p.step(), 6);
    }

    // "Slow": the verse, pre-chorus and chorus open alike (C#m E/D# E).
    // Played in the verse, a chord missed and the verse's chords going on:
    // it stays in the verse (it once jumped to the chorus here).
    void sectionsThatOpenAlikeNeverPullItAway()
    {
        constexpr int Gs4 = 68;
        constexpr int Ds3 = 51;
        // Verse 1 (C#m E/D# E C#m E/D# E), Chorus (C#m E/D# E), Verse 2 (C#m E/D# E).
        Player p(mapOf({{"C#m", 0}, {"E/D#", 0}, {"E", 0}, {"C#m", 0}, {"E/D#", 0}, {"E", 0},
                        {"C#m", 1}, {"E/D#", 1}, {"E", 1},
                        {"C#m", 2}, {"E/D#", 2}, {"E", 2}}));
        p.chord({Cs4 - 12, E4, Gs4});        // C#m
        p.chord({Ds3, E4, Gs4, B4});         // E/D#
        QCOMPARE(p.step(), 1);
        p.chord({Cs4 - 12, E4, Gs4});        // C#m (the E missed): the verse's next C#m
        QCOMPARE(p.step(), 3);
        QCOMPARE(p.section(), 0);
        p.chord({Ds3, E4, Gs4, B4});         // E/D#: still the verse
        QCOMPARE(p.step(), 4);
        QCOMPARE(p.section(), 0);
        p.chord({E3, Gs4, B4});              // E
        p.chord({Cs4 - 12, E4, Gs4});        // the chorus, in its turn
        QCOMPARE(p.step(), 6);
        QCOMPARE(p.section(), 1);
    }

    // A chord missed (the next skipped, the one after played): it catches up.
    void aMissedChordIsCaughtUp()
    {
        Player p(mapOf({{"C", 0}, {"G", 0}, {"Am", 0}, {"F", 0}}));
        p.chord({C4, E4, G4});
        p.chord({A3, C4, E4}); // G missed
        QCOMPARE(p.step(), 2);
        p.chord({C4, E4, G4}); // back to C: never backwards
        QCOMPARE(p.step(), 2);
    }

    // The flow can play a section twice (Verse, Chorus, Verse, Chorus): the
    // pedal's "go to the chorus" goes to the next chorus along, not the first.
    void aSectionChosenByHandIsTheNextOneAlongTheFlow()
    {
        // Parts: Verse (0), Chorus (1), Verse (0), Chorus (1).
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 1}, {"G", 1}, {"Am", 0}, {"F", 0}, {"C", 1}, {"G", 1}}));
        p.block();
        p.follower.jumpToSection(0);
        p.block();
        QCOMPARE(p.step(), 0);
        p.chord({A3, C4, E4});
        p.chord({F3, A3, C4});
        p.chord({C4, E4, G4});
        QCOMPARE(p.step(), 2); // in the first chorus
        p.follower.jumpToSection(0);
        p.block();
        QCOMPARE(p.step(), 4); // the second verse, not back to the first
        p.follower.jumpToSection(1);
        p.block();
        QCOMPARE(p.step(), 6);
    }

    // A part of the flow chosen (Perform's tiles): that very part, back or
    // ahead, the one playing again; a part not of that section: the section.
    void aPartChosenIsThatPart()
    {
        // Parts: Verse (0), Chorus (1), Verse (2), Chorus (3).
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 1}, {"G", 1}, {"Am", 0}, {"F", 0}, {"C", 1}, {"G", 1}}));
        p.block();
        p.follower.jumpToPart(1, 3);
        p.block();
        QCOMPARE(p.step(), 6); // the second chorus, straight from the top
        p.follower.jumpToPart(1, 3);
        p.block();
        QCOMPARE(p.step(), 6); // the same chorus again, not past it
        p.follower.jumpToPart(0, 0);
        p.block();
        QCOMPARE(p.step(), 0); // back to the first verse
        p.follower.jumpToPart(1, 2); // part 2 is a verse: the next chorus instead
        p.block();
        QCOMPARE(p.step(), 2);
        p.follower.jumpToSection(1); // a plain section jump forgets the part asked before
        p.block();
        QCOMPARE(p.step(), 6);
    }

    // G-B-D holds Bm's root and third (B, D) but is G: it never completes a
    // jump to a section starting D, Bm.
    void aChordSharingTwoNotesIsNotClear()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"D", 1}, {"Bm", 1}}));
        p.chord({A3, C4, E4});
        p.chord({D4, Fs4, A4}); // D: the chorus's first chord, remembered
        p.press({B3, D4, G4});  // G in first inversion: B, D and G
        QCOMPARE(p.section(), 0);
    }

    void aSectionChosenByHand()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"D", 1}, {"Bm", 1}}));
        p.block();
        p.follower.jumpToSection(1);
        p.block();
        QVERIFY(p.started());
        QCOMPARE(p.step(), 2);
        QCOMPARE(p.gate.before, 1); // from the block's start
        QCOMPARE(p.gate.after, 1);
    }

    void enteringASectionHandsTheChordOver()
    {
        Player p(mapOf({{"Am", 0}, {"G", 0}, {"F", 1}, {"C", 1}}));
        p.chord({A3, C4, E4});
        p.chord({G3, B3, D4});
        p.press({C3}); // a bass note first: C alone is not F
        QCOMPARE(p.section(), 0);
        p.press({F3}); // F's root with C (its fifth): the chorus, entered on F's key
        QCOMPARE(p.section(), 1);
        QCOMPARE(p.gate.switchAt, 0);
        // At the switch: C (held from before, pressed since the last chord) goes over.
        QVERIFY(std::ranges::any_of(p.handover, [](const MidiEvent& e) { return e.status == 0x90 && e.data1 == C3; }));
        QVERIFY(std::ranges::any_of(p.handover, [](const MidiEvent& e) { return e.status == 0x80 && e.data1 == C3; }));
        // The key that did it (F) is not handed over: it already went to the chorus.
        QVERIFY(std::ranges::none_of(p.handover, [](const MidiEvent& e) { return e.data1 == F3; }));
    }

    void sustainedKeysAreNotHandedOver()
    {
        Player p(mapOf({{"Am", 0}, {"F", 1}}));
        p.press({A3, C4, E4});
        p.pedal(true);
        p.release({A3, C4, E4}); // under the pedal: still sounding, but up
        p.wait(600);
        p.press({F3, A4});
        QCOMPARE(p.section(), 1);
        QVERIFY(p.handover.empty()); // no key is down but the new ones
    }

    void withoutSectionsItStillFollows()
    {
        Player p(mapOf({{"C", -1}, {"G", -1}}));
        p.chord({C4, E4});
        p.press({G3, B3});
        QCOMPARE(p.step(), 1);
        QCOMPARE(p.section(), -1);
        QCOMPARE(p.gate.after, -1); // every instrument plays
    }

    void panicWaitsForTheFirstChordAgain()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}}));
        p.press({A3, C4, E4});
        QVERIFY(p.started());
        p.follower.reset();
        p.block();
        QVERIFY(!p.started());
        // "All notes off" forgets what is held.
        p.press({A3});
        p.events.push_back(key(0xB0, 123, 0));
        p.block();
        p.press({C4}); // A was let go by "all notes off" (and not remembered)
        QVERIFY(!p.started());
    }

    void aNewMapStartsFreshOrResumes()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 0}}));
        p.press({A3, C4, E4});
        QCOMPARE(p.step(), 0);
        ++p.generation; // the song changed
        p.block();
        QVERIFY(!p.started());
        p.map.resumeAt = 2; // the chart edited while playing its third chord
        ++p.generation;
        p.block();
        QCOMPARE(p.step(), 2);
    }

    // A melody over a held chord is not the next chord, even with its notes:
    // the next chord's root has to be struck.
    void aMelodyOverAHeldChordStays()
    {
        Player p(mapOf({{"C", 0}, {"G", 0}}));
        p.press({C4, E4, G4});
        QCOMPARE(p.step(), 0);
        p.wait(200);
        p.press({D4 + 12}); // D over C-E-G: G and D sound, but G was not struck
        p.release({D4 + 12});
        p.press({B4});
        QCOMPARE(p.step(), 0);
        p.press({G3}); // now G is struck
        QCOMPARE(p.step(), 1);
    }

    // The verse ends on C and the chorus opens on C: only playing the chord
    // again enters the chorus, not a melody note over the held one.
    // D written twice in a row in a part: comping on the D (struck again a
    // moment later) stays on the first; struck again after a while (1.5 s
    // or more since the first D came in), it is the second.
    void theSameChordTwiceInAPartNeedsTimeToMoveOn()
    {
        Player p(mapOf({{"D", 0}, {"D", 0}, {"G", 0}, {"A", 0}}));
        p.chord({D4, Fs4, A4});
        QCOMPARE(p.step(), 0);
        p.chord({D4, Fs4, A4}); // comped again, 0.6 s later: still the first D
        QCOMPARE(p.step(), 0);
        p.wait(1000);
        p.chord({D4, Fs4, A4}); // 2.3 s after the first: the second D
        QCOMPARE(p.step(), 1);
        p.chord({G3, B3, D4});
        QCOMPARE(p.step(), 2);
    }

    void theSameChordAcrossSectionsIsPlayedAgain()
    {
        Player p(mapOf({{"G", 0}, {"C", 0}, {"C", 1}, {"F", 1}}));
        p.chord({G3, B3, D4});
        p.press({C4, E4, G4});
        QCOMPARE(p.step(), 1);
        p.wait(200);
        p.press({D4 + 12});
        p.press({C4 + 12}); // a melody C: struck, but not the chord
        QCOMPARE(p.step(), 1);
        p.release({C4, E4, G4, D4 + 12, C4 + 12});
        p.wait(200);
        p.press({C4, E4, G4}); // the chord again
        QCOMPARE(p.step(), 2);
        QCOMPARE(p.section(), 1);
    }

    // The chorus and the bridge both open on C: from the verse, C then Em is
    // not the chorus (next) and the bridge is not next: it stays in the verse.
    void onlyTheNextPartCanBeJumpedTo()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 1}, {"G", 1}, {"C", 2}, {"Em", 2}}));
        p.chord({A3, C4, E4});
        p.chord({C4, E4, G4});
        p.press({E3, G3, B3});
        QCOMPARE(p.section(), 0);
        QCOMPARE(p.step(), 0);
    }

    // A section asked for before a new song's map is not applied to it: the
    // new song waits for its first chord.
    void aJumpAskedBeforeANewMapIsDropped()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"D", 1}, {"Bm", 1}}));
        p.block();
        p.follower.jumpToSection(1);
        ++p.generation;
        p.block();
        QVERIFY(!p.started());
        // Nor one asked while nothing was followed.
        ChordFollower follower;
        follower.jumpToSection(1);
        (void)follower.process(nullptr, 0, {}, 10, 1000.0);
        (void)follower.process(&p.map, 1, {}, 10, 1000.0);
        QVERIFY(!follower.position().started);
    }

    // Chords written above the first section title belong to it: its
    // instruments play them, not every section's at once.
    void chordsBeforeTheFirstTitlePlayTheFirstSection()
    {
        Player p(mapOf({{"C", -1}, {"Am", 0}, {"F", 1}}));
        p.block();
        QCOMPARE(p.gate.before, 0);
        p.press({C4, E4, G4});
        QCOMPARE(p.step(), 0);
        QCOMPARE(p.section(), 0);
        QCOMPARE(p.gate.after, 0);
    }

    // A map that does not hang together is refused before it reaches the
    // audio thread (where a bad index would end the app), saying what is wrong.
    void aBrokenMapIsRefused_data()
    {
        QTest::addColumn<int>("breakage");
        QTest::addColumn<QString>("said");
        QTest::newRow("section start past the chords") << 0 << u"section 2 starts at chord 10"_s;
        QTest::newRow("section start on another section's chord") << 1 << u"section 2 starts at chord 1"_s;
        QTest::newRow("note out of the octave") << 2 << u"chord 1"_s;
        QTest::newRow("section out of range") << 3 << u"chord 2"_s;
        QTest::newRow("resume past the chords") << 4 << u"resume"_s;
        QTest::newRow("too many sections") << 5 << u"sections"_s;
        QTest::newRow("too many chords") << 6 << u"chords"_s;
        QTest::newRow("no notes") << 7 << u"chord 1"_s;
        QTest::newRow("part start past the chords") << 8 << u"part 2"_s;
        QTest::newRow("part starts out of order") << 9 << u"part 2"_s;
        QTest::newRow("chord in a part not listed") << 10 << u"chord 1"_s;
    }
    void aBrokenMapIsRefused()
    {
        QFETCH(int, breakage);
        QFETCH(QString, said);
        ChordFollowMap map = mapOf({{"Am", 0}, {"F", 0}, {"D", 1}, {"Bm", 1}});
        QVERIFY(ChordFollower::check(map).has_value());
        switch (breakage) {
        case 0: map.sectionStarts.at(1) = 9; break;
        case 1: map.sectionStarts.at(1) = 0; break;
        case 2: map.steps.at(0).root = 12; break;
        case 3: map.steps.at(1).section = 7; break;
        case 4: map.resumeAt = 4; break;
        case 5: map.sectionStarts.assign(65, -1); break;
        case 6: map.steps.resize(4097, map.steps.front()); break;
        case 7: map.steps.at(0).family = 0; break;
        case 8: map.partStarts.at(1) = 9; break;
        case 9: map.partStarts.at(1) = 0; break;
        case 10: map.steps.at(0).part = 5; break;
        default: QFAIL("no such breakage");
        }
        const auto checked = ChordFollower::check(map);
        QVERIFY(!checked.has_value());
        QVERIFY2(checked.error().message.contains(said), qPrintable(checked.error().message));
    }

    void noMapIsNotFollowing()
    {
        ChordFollower follower;
        const std::array events{key(0x90, 60, 100)};
        const SectionGate gate = follower.process(nullptr, 0, events, 10, 1000.0);
        QVERIFY(!follower.position().active);
        QCOMPARE(gate.before, -1);
    }
};

QTEST_GUILESS_MAIN(TestChordFollower)
#include "tst_chord_follower.moc"
