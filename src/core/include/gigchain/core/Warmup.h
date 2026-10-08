#pragma once

#include "gigchain/core/Practice.h"

#include <QString>

#include <map>
#include <vector>

namespace gigchain::core {

// ---- Warm-ups (the Practice tab): exercises at three levels, each played
// right hand, left hand, then both; every run scored, and the tempo moving
// up as the runs come out clean (as teachers have it: three clean runs, then
// 5 BPM faster).

enum class WarmupLevel : int {
    Beginner,     // hand position and five-finger patterns, quarter notes
    Intermediate, // patterns in four keys, one-octave scales, broken chords, a melody, eighth notes
    Pro,          // Hanon, two-octave scales and arpeggios, inversions, sixteenth notes
};
inline constexpr int kWarmupLevels = 3;

enum class WarmupHands : int { Right, Left, Both };

// One note of an exercise, in beats from its first note.
struct WarmupNote
{
    int pitch = 60;
    double start = 0.0;
    double length = 1.0;
    bool left = false; // the left hand plays it
    int finger = 1;    // 1 thumb ... 5 little finger

    bool operator==(const WarmupNote&) const = default;
};

struct WarmupExercise
{
    QString id;   // kept with the player's progress: never changes
    QString name; // "Five fingers from C"
    QString tip;  // how to play it, in a sentence
    WarmupLevel level = WarmupLevel::Beginner;
    double startTempo = 60.0;  // BPM a player starts at...
    double targetTempo = 90.0; // ... and passes it at
};

// The exercises of a level, in the order the warm-up plays them.
[[nodiscard]] std::vector<WarmupExercise> warmupExercises(WarmupLevel level);
// An exercise's notes for `hands` (both: the two hands' notes together), by start.
[[nodiscard]] std::vector<WarmupNote> warmupNotes(const WarmupExercise& exercise, WarmupHands hands);
// The notes as the Practice tab plays them: a count-in bar of 4 beats, then
// the notes, those starting together as one chord (fingers kept).
[[nodiscard]] PracticeTimeline warmupTimeline(const std::vector<WarmupNote>& notes);
// Where the notes start in warmupTimeline (the count-in bar), in beats.
inline constexpr double kWarmupCountIn = 4.0;

// ---- Scoring a run

// A key the player pressed: its note, when (beats from the first note) and how hard.
struct PlayedKey
{
    int pitch = 60;
    double beat = 0.0;
    int velocity = 100;
};

struct WarmupNoteResult
{
    enum Kind : int { OnTime, Early, Late, Missed };
    int pitch = 60;
    double start = 0.0;
    bool left = false;
    Kind kind = Missed;
    double offsetMs = 0.0; // + late, - early (0 when missed)
};

// How exact a run must be to be clean, per level.
struct WarmupLimits
{
    double timingMs = 80.0; // the notes' average distance from the beat
    double handsMs = -1.0;  // the hands' average distance on notes meant together; -1: not judged
};
[[nodiscard]] WarmupLimits warmupLimits(WarmupLevel level);

struct WarmupScore
{
    int notes = 0;   // to play
    int right = 0;   // played, at about the right time
    int missed = 0;
    int extra = 0;   // keys pressed that no note asked for
    double timingMs = 0.0;     // mean distance from the beat, of the notes played
    double driftMs = 0.0;      // mean signed: + behind the beat, - ahead
    double evennessMs = 0.0;   // how much the timing wanders (standard deviation)
    double touchSpread = 0.0;  // how much the touch varies (velocity standard deviation)
    double handsApartMs = -1.0; // mean distance between the hands on notes meant together; -1: one hand
    int stars = 0;             // 0-3
    bool clean = false;        // counts towards the next tempo
    QString tip;               // the one thing to do next
    std::vector<WarmupNoteResult> perNote;
};

// `played` scored against `expected` at `tempo` (BPM) for `level`'s limits.
// A key counts for a note when it is the note's pitch within half a beat
// (and at most 300 ms) of its start; each key counts once.
[[nodiscard]] WarmupScore scoreWarmup(const std::vector<WarmupNote>& expected, const std::vector<PlayedKey>& played, double tempo,
                                      WarmupLevel level);

// ---- Progress

struct WarmupProgress
{
    double tempo = 0.0; // BPM it is played at now; 0: not started (its start tempo)
    int cleanRuns = 0;  // clean both-hands runs in a row at this tempo
    bool passed = false;
    int bestStars = 0;

    bool operator==(const WarmupProgress&) const = default;
};

struct WarmupStep
{
    bool tempoUp = false;    // three clean runs: 5 BPM faster
    bool justPassed = false; // three clean runs at the target tempo
};

// A run recorded: its stars kept; a clean both-hands run counts (one hand
// alone is the way there, not the test); three in a row move the tempo up
// 5 BPM (never past the target), or, at the target, pass the exercise. A run
// that is not clean starts the count again.
WarmupStep recordWarmupRun(WarmupProgress& progress, const WarmupExercise& exercise, const WarmupScore& score, WarmupHands hands);
// The tempo an exercise is played at now.
[[nodiscard]] double warmupTempo(const WarmupProgress& progress, const WarmupExercise& exercise);
// The highest level open to the player: Beginner always; the next once
// every exercise of a level is passed. `progress`: by exercise id.
[[nodiscard]] WarmupLevel unlockedWarmupLevel(const std::map<QString, WarmupProgress>& progress);

} // namespace gigchain::core
