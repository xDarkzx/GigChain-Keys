// Warm-ups: the exercises of each level, scoring a run, and moving on.
#include "gigchain/core/Warmup.h"

#include <QtTest>

#include <algorithm>
#include <set>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

WarmupExercise exercise(WarmupLevel level, const QString& id)
{
    const std::vector<WarmupExercise> all = warmupExercises(level);
    const auto found = std::ranges::find(all, id, &WarmupExercise::id);
    return found != all.end() ? *found : WarmupExercise{};
}

// Every note played exactly on time, at velocity 100 (shifted by `lateMs`
// at `tempo`, the left hand by `leftLateMs` more).
std::vector<PlayedKey> playedPerfectly(const std::vector<WarmupNote>& notes, double tempo, double lateMs = 0.0, double leftLateMs = 0.0)
{
    std::vector<PlayedKey> keys;
    for (const WarmupNote& note : notes) {
        const double ms = lateMs + (note.left ? leftLateMs : 0.0);
        keys.push_back(PlayedKey{.pitch = note.pitch, .beat = note.start + (ms / 1000.0 * tempo / 60.0), .velocity = 100});
    }
    return keys;
}

std::vector<int> pitches(const std::vector<WarmupNote>& notes)
{
    std::vector<int> list;
    std::ranges::transform(notes, std::back_inserter(list), &WarmupNote::pitch);
    return list;
}

std::vector<int> fingers(const std::vector<WarmupNote>& notes)
{
    std::vector<int> list;
    std::ranges::transform(notes, std::back_inserter(list), &WarmupNote::finger);
    return list;
}

} // namespace

class TestWarmup : public QObject
{
    Q_OBJECT

private slots:
    // Each level's exercises, in order, from the teachers' warm-up: hand
    // position and five-finger patterns, then scales, chords and a melody,
    // then Hanon, two-octave scales, arpeggios and inversions.
    void eachLevelHasItsExercises()
    {
        const auto ids = [](WarmupLevel level) {
            QStringList list;
            for (const WarmupExercise& e : warmupExercises(level)) list << e.id;
            return list;
        };
        QCOMPARE(ids(WarmupLevel::Beginner), (QStringList{u"b-run"_s, u"b-five-c"_s, u"b-five-g"_s, u"b-skips"_s}));
        QCOMPARE(ids(WarmupLevel::Intermediate),
                 (QStringList{u"i-five-keys"_s, u"i-scale-c"_s, u"i-scale-g"_s, u"i-broken-chords"_s, u"i-ode"_s, u"i-contrary"_s}));
        QCOMPARE(ids(WarmupLevel::Pro),
                 (QStringList{u"p-hanon-1"_s, u"p-scale-c"_s, u"p-contrary-c"_s, u"p-arpeggio-c"_s, u"p-inversions"_s}));
        for (int level = 0; level < kWarmupLevels; ++level) {
            for (const WarmupExercise& e : warmupExercises(static_cast<WarmupLevel>(level))) {
                QVERIFY2(!e.name.isEmpty() && !e.tip.isEmpty(), qPrintable(e.id));
                QVERIFY2(e.startTempo > 0.0 && e.startTempo < e.targetTempo, qPrintable(e.id));
            }
        }
        QCOMPARE(exercise(WarmupLevel::Beginner, u"b-five-c"_s).startTempo, 60.0);
        QCOMPARE(exercise(WarmupLevel::Beginner, u"b-five-c"_s).targetTempo, 90.0);
        QCOMPARE(exercise(WarmupLevel::Pro, u"p-hanon-1"_s).targetTempo, 120.0);
    }

    // The first warm-up: one flowing run of single notes that works every
    // finger, one finger per key, C D E F G F E D over and over (fingers 1 2
    // 3 4 5 4 3 2), in eighth notes, ending on a held C: never a note struck
    // twice in a row, never two at once.
    void theFirstWarmupIsAFlowingRunForEveryFinger()
    {
        const WarmupExercise run = exercise(WarmupLevel::Beginner, u"b-run"_s);
        const std::vector<WarmupNote> right = warmupNotes(run, WarmupHands::Right);
        const std::vector<int> played = pitches(right);
        QCOMPARE(std::vector<int>(played.begin(), played.begin() + 9), (std::vector<int>{60, 62, 64, 65, 67, 65, 64, 62, 60}));
        const std::vector<int> used = fingers(right);
        QCOMPARE(std::vector<int>(used.begin(), used.begin() + 8), (std::vector<int>{1, 2, 3, 4, 5, 4, 3, 2}));
        for (int finger = 1; finger <= 5; ++finger) QVERIFY2(std::ranges::count(used, finger) >= 4, qPrintable(u"finger %1"_s.arg(finger)));
        for (std::size_t i = 1; i < right.size(); ++i) {
            QVERIFY(right.at(i).pitch != right.at(i - 1).pitch); // no key twice in a row
            QVERIFY(right.at(i).start > right.at(i - 1).start);  // one note at a time
        }
        QCOMPARE(right.at(1).start - right.at(0).start, 0.5); // eighth notes: it flows
        QCOMPARE(right.back().pitch, 60);
        QVERIFY(right.back().length >= 2.0); // the last C held
        const std::vector<WarmupNote> left = warmupNotes(run, WarmupHands::Left);
        QCOMPARE(left.front().pitch, 48);
        QCOMPARE(left.front().finger, 5); // the left hand's little finger on C
        QCOMPARE(left.size(), right.size());
    }

    // C-D-E-F-G and back: the right hand thumb on middle C (fingers 1 to 5),
    // the left hand an octave lower, little finger on C (5 to 1).
    void theFiveFingerPatternFromC()
    {
        const WarmupExercise five = exercise(WarmupLevel::Beginner, u"b-five-c"_s);
        const std::vector<WarmupNote> right = warmupNotes(five, WarmupHands::Right);
        QCOMPARE(pitches(right), (std::vector<int>{60, 62, 64, 65, 67, 65, 64, 62, 60}));
        QCOMPARE(fingers(right), (std::vector<int>{1, 2, 3, 4, 5, 4, 3, 2, 1}));
        QVERIFY(std::ranges::none_of(right, &WarmupNote::left));
        QCOMPARE(right.front().start, 0.0);
        QCOMPARE(right.at(1).start, 1.0); // quarter notes
        const std::vector<WarmupNote> left = warmupNotes(five, WarmupHands::Left);
        QCOMPARE(pitches(left), (std::vector<int>{48, 50, 52, 53, 55, 53, 52, 50, 48}));
        QCOMPARE(fingers(left), (std::vector<int>{5, 4, 3, 2, 1, 2, 3, 4, 5}));
        QVERIFY(std::ranges::all_of(left, &WarmupNote::left));
        const std::vector<WarmupNote> both = warmupNotes(five, WarmupHands::Both);
        QCOMPARE(both.size(), right.size() + left.size());
        QVERIFY(std::ranges::is_sorted(both, {}, &WarmupNote::start));
    }

    // Every exercise of every level, each hand and both, can be played:
    // notes in order, fingers 1-5, on a piano, each hand its own notes, the
    // left hand below the right where both start together.
    void everyExerciseIsPlayable()
    {
        for (int level = 0; level < kWarmupLevels; ++level) {
            for (const WarmupExercise& e : warmupExercises(static_cast<WarmupLevel>(level))) {
                const auto right = warmupNotes(e, WarmupHands::Right);
                const auto left = warmupNotes(e, WarmupHands::Left);
                const auto both = warmupNotes(e, WarmupHands::Both);
                QVERIFY2(right.size() >= 8 && left.size() >= 4, qPrintable(e.id));
                QCOMPARE(both.size(), right.size() + left.size());
                QVERIFY2(std::ranges::none_of(right, &WarmupNote::left) && std::ranges::all_of(left, &WarmupNote::left), qPrintable(e.id));
                for (const auto* notes : {&right, &left, &both}) {
                    QVERIFY2(std::ranges::is_sorted(*notes, {}, &WarmupNote::start), qPrintable(e.id));
                    for (const WarmupNote& n : *notes) {
                        QVERIFY2(n.finger >= 1 && n.finger <= 5, qPrintable(e.id));
                        QVERIFY2(n.pitch >= 21 && n.pitch <= 108 && n.length > 0.0, qPrintable(e.id));
                    }
                }
                for (const WarmupNote& l : left) {
                    for (const WarmupNote& r : right) {
                        if (qFuzzyCompare(l.start + 1.0, r.start + 1.0)) QVERIFY2(l.pitch < r.pitch, qPrintable(e.id));
                    }
                }
            }
        }
    }

    // The melody (Ode to Joy, Beethoven) in both hands, an octave apart.
    void theMelodyIsPlayedByBothHands()
    {
        const WarmupExercise ode = exercise(WarmupLevel::Intermediate, u"i-ode"_s);
        const std::vector<int> tune = pitches(warmupNotes(ode, WarmupHands::Right));
        QVERIFY(tune.size() >= 8);
        QCOMPARE(std::vector<int>(tune.begin(), tune.begin() + 8), (std::vector<int>{64, 64, 65, 67, 67, 65, 64, 62}));
        const std::vector<int> low = pitches(warmupNotes(ode, WarmupHands::Left));
        QCOMPARE(std::vector<int>(low.begin(), low.begin() + 4), (std::vector<int>{52, 52, 53, 55}));
    }

    // A warm-up works every finger: each Beginner and Intermediate exercise
    // uses all five fingers of each hand. (Pro's arpeggios and inversions keep
    // the fingering pianists use for them.)
    void everyFingerIsWorked()
    {
        for (const WarmupLevel level : {WarmupLevel::Beginner, WarmupLevel::Intermediate}) {
            for (const WarmupExercise& e : warmupExercises(level)) {
                for (const WarmupHands hands : {WarmupHands::Right, WarmupHands::Left}) {
                    const std::vector<int> used = fingers(warmupNotes(e, hands));
                    for (int finger = 1; finger <= 5; ++finger) {
                        QVERIFY2(std::ranges::count(used, finger) > 0,
                                 qPrintable(u"%1 (%2 hand) never uses finger %3"_s.arg(e.id, hands == WarmupHands::Right ? u"right"_s : u"left"_s).arg(finger)));
                    }
                }
            }
        }
    }

    // As the Practice tab plays it: a count-in bar, notes starting together
    // one chord, each note's finger kept.
    void theTimelineHasACountInAndFingers()
    {
        const auto both = warmupNotes(exercise(WarmupLevel::Beginner, u"b-five-c"_s), WarmupHands::Both);
        const PracticeTimeline timeline = warmupTimeline(both);
        QCOMPARE(timeline.chords.size(), std::size_t{9});
        const PracticeChord& first = timeline.chords.front();
        QCOMPARE(first.start, kWarmupCountIn);
        QCOMPARE(first.right, std::vector<int>{60});
        QCOMPARE(first.left, std::vector<int>{48});
        QCOMPARE(first.fingers.at(60), 1);
        QCOMPARE(first.fingers.at(48), 5);
        QVERIFY(timeline.length >= kWarmupCountIn + 9.0);
    }

    // Played exactly: every note right, on time, three stars, clean.
    void aPerfectRunIsClean()
    {
        const WarmupExercise five = exercise(WarmupLevel::Beginner, u"b-five-c"_s);
        const auto notes = warmupNotes(five, WarmupHands::Right);
        const WarmupScore score = scoreWarmup(notes, playedPerfectly(notes, 60.0), 60.0, WarmupLevel::Beginner);
        QCOMPARE(score.notes, 9);
        QCOMPARE(score.right, 9);
        QCOMPARE(score.missed, 0);
        QCOMPARE(score.extra, 0);
        QVERIFY(score.timingMs < 1.0);
        QCOMPARE(score.stars, 3);
        QVERIFY(score.clean);
        QCOMPARE(score.handsApartMs, -1.0); // one hand
        QVERIFY(!score.tip.isEmpty());
        QVERIFY(std::ranges::all_of(score.perNote, [](const auto& n) { return n.kind == WarmupNoteResult::OnTime; }));
    }

    // 100 ms behind every beat: right notes, but late (a beginner's limit is
    // 80 ms): not clean, two stars, and the tip says so.
    void aLateRunIsNotClean()
    {
        const auto notes = warmupNotes(exercise(WarmupLevel::Beginner, u"b-five-c"_s), WarmupHands::Right);
        const WarmupScore score = scoreWarmup(notes, playedPerfectly(notes, 60.0, 100.0), 60.0, WarmupLevel::Beginner);
        QCOMPARE(score.right, 9);
        QVERIFY(qAbs(score.timingMs - 100.0) < 1.0);
        QVERIFY(qAbs(score.driftMs - 100.0) < 1.0);
        QVERIFY(!score.clean);
        QCOMPARE(score.stars, 2);
        QVERIFY2(score.tip.contains(u"behind"_s), qPrintable(score.tip));
        QVERIFY(std::ranges::all_of(score.perNote, [](const auto& n) { return n.kind == WarmupNoteResult::Late; }));
    }

    // A note left out and a key nobody asked for: counted, not clean.
    void missedAndExtraNotesAreCounted()
    {
        const auto notes = warmupNotes(exercise(WarmupLevel::Beginner, u"b-five-c"_s), WarmupHands::Right);
        std::vector<PlayedKey> keys = playedPerfectly(notes, 60.0);
        keys.erase(keys.begin() + 2);                                      // E left out
        keys.push_back(PlayedKey{.pitch = 61, .beat = 1.5, .velocity = 90}); // a wrong key
        keys.push_back(PlayedKey{.pitch = 66, .beat = 3.5, .velocity = 90}); // and another
        const WarmupScore score = scoreWarmup(notes, keys, 60.0, WarmupLevel::Beginner);
        QCOMPARE(score.missed, 1);
        QCOMPARE(score.extra, 2);
        QCOMPARE(score.right, 8);
        QVERIFY(!score.clean);
        QCOMPARE(score.perNote.at(2).kind, WarmupNoteResult::Missed);
        QVERIFY2(score.tip.contains(u"miss"_s, Qt::CaseInsensitive), qPrintable(score.tip));
        // Nothing played at all: no stars.
        QCOMPARE(scoreWarmup(notes, {}, 60.0, WarmupLevel::Beginner).stars, 0);
    }

    // Both hands: how far apart they land on notes meant together. The left
    // hand 70 ms behind is past an intermediate player's 60 ms.
    void theHandsAreJudgedTogether()
    {
        const WarmupExercise five = exercise(WarmupLevel::Intermediate, u"i-five-keys"_s);
        const auto both = warmupNotes(five, WarmupHands::Both);
        const WarmupScore together = scoreWarmup(both, playedPerfectly(both, 70.0), 70.0, WarmupLevel::Intermediate);
        QVERIFY(together.clean);
        QVERIFY(together.handsApartMs >= 0.0 && together.handsApartMs < 1.0);
        const WarmupScore apart = scoreWarmup(both, playedPerfectly(both, 70.0, 0.0, 70.0), 70.0, WarmupLevel::Intermediate);
        QVERIFY(qAbs(apart.handsApartMs - 70.0) < 1.0);
        QVERIFY(!apart.clean);
        QVERIFY2(apart.tip.contains(u"left hand"_s), qPrintable(apart.tip));
        QCOMPARE(warmupLimits(WarmupLevel::Beginner).timingMs, 80.0);
        QCOMPARE(warmupLimits(WarmupLevel::Intermediate).handsMs, 60.0);
        QCOMPARE(warmupLimits(WarmupLevel::Pro).timingMs, 30.0);
        QCOMPARE(warmupLimits(WarmupLevel::Pro).handsMs, 40.0);
    }

    // An uneven touch (one key much harder than the rest) and uneven timing
    // are measured.
    void evennessIsMeasured()
    {
        const auto notes = warmupNotes(exercise(WarmupLevel::Beginner, u"b-five-c"_s), WarmupHands::Right);
        std::vector<PlayedKey> keys = playedPerfectly(notes, 60.0);
        for (std::size_t i = 0; i < keys.size(); ++i) {
            keys.at(i).velocity = i % 2 == 0 ? 40 : 120;
            keys.at(i).beat += (i % 2 == 0 ? -0.03 : 0.03); // ±30 ms at 60 BPM
        }
        const WarmupScore score = scoreWarmup(notes, keys, 60.0, WarmupLevel::Beginner);
        QVERIFY(score.touchSpread > 35.0);
        QVERIFY(score.evennessMs > 25.0);
    }

    // Three clean both-hands runs: 5 BPM faster; a run that is not clean
    // starts the count again; one hand alone does not count; at the target,
    // three clean runs pass it.
    void cleanRunsMoveTheTempoUp()
    {
        const WarmupExercise five = exercise(WarmupLevel::Beginner, u"b-five-c"_s);
        WarmupProgress progress;
        QCOMPARE(warmupTempo(progress, five), 60.0);
        WarmupScore clean;
        clean.clean = true;
        clean.stars = 3;
        WarmupScore sloppy;
        sloppy.stars = 1;
        QVERIFY(!recordWarmupRun(progress, five, clean, WarmupHands::Right).tempoUp); // one hand: practice
        QCOMPARE(progress.cleanRuns, 0);
        QCOMPARE(progress.bestStars, 3);
        (void)recordWarmupRun(progress, five, clean, WarmupHands::Both);
        (void)recordWarmupRun(progress, five, sloppy, WarmupHands::Both);
        QCOMPARE(progress.cleanRuns, 0); // again from the start
        (void)recordWarmupRun(progress, five, clean, WarmupHands::Both);
        (void)recordWarmupRun(progress, five, clean, WarmupHands::Both);
        const WarmupStep third = recordWarmupRun(progress, five, clean, WarmupHands::Both);
        QVERIFY(third.tempoUp);
        QCOMPARE(warmupTempo(progress, five), 65.0);
        QCOMPARE(progress.cleanRuns, 0);
        // Up to the target (90), never past it; three clean there: passed.
        progress.tempo = 88.0;
        for (int i = 0; i < 3; ++i) (void)recordWarmupRun(progress, five, clean, WarmupHands::Both);
        QCOMPARE(warmupTempo(progress, five), 90.0);
        QVERIFY(!progress.passed);
        WarmupStep last;
        for (int i = 0; i < 3; ++i) last = recordWarmupRun(progress, five, clean, WarmupHands::Both);
        QVERIFY(last.justPassed);
        QVERIFY(progress.passed);
        QCOMPARE(warmupTempo(progress, five), 90.0);
    }

    // Beginner is always open; Intermediate once every beginner exercise is
    // passed; Pro after Intermediate.
    void levelsOpenOneAfterAnother()
    {
        std::map<QString, WarmupProgress> progress;
        QCOMPARE(unlockedWarmupLevel(progress), WarmupLevel::Beginner);
        for (const WarmupExercise& e : warmupExercises(WarmupLevel::Beginner)) progress[e.id].passed = true;
        QCOMPARE(unlockedWarmupLevel(progress), WarmupLevel::Intermediate);
        for (const WarmupExercise& e : warmupExercises(WarmupLevel::Intermediate)) progress[e.id].passed = true;
        QCOMPARE(unlockedWarmupLevel(progress), WarmupLevel::Pro);
        progress[u"b-skips"_s].passed = false; // (a beginner exercise not passed after all)
        QCOMPARE(unlockedWarmupLevel(progress), WarmupLevel::Beginner);
    }
};

QTEST_GUILESS_MAIN(TestWarmup)
#include "tst_warmup.moc"
