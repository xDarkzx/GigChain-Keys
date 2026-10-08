#include "gigchain/core/Warmup.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

constexpr double kQuarter = 1.0;
constexpr double kEighth = 0.5;
constexpr double kSixteenth = 0.25;

QString tr(const char* text, const char* disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("Warmup", text, disambiguation, n);
}

// A major scale's degree (0 = the root; 7 = an octave up; negative = below) from `root`.
int degree(int root, int d)
{
    static constexpr std::array<int, 7> kMajor{0, 2, 4, 5, 7, 9, 11};
    const int octave = d >= 0 ? d / 7 : -((-d + 6) / 7);
    return root + (octave * 12) + kMajor.at(static_cast<std::size_t>(d - (octave * 7)));
}

// One hand's line: notes one after another (`fingers` one per pitch).
struct Line
{
    std::vector<WarmupNote> notes;
    bool left = false;
    double at = 0.0; // where the next note starts

    void note(int pitch, int finger, double length)
    {
        notes.push_back(WarmupNote{.pitch = pitch, .start = at, .length = length, .left = left, .finger = finger});
        at += length;
    }
    // Several notes struck together (a chord).
    void chord(const std::vector<int>& pitches, const std::vector<int>& fingers, double length)
    {
        for (std::size_t i = 0; i < pitches.size(); ++i) {
            notes.push_back(WarmupNote{.pitch = pitches.at(i), .start = at, .length = length, .left = left, .finger = fingers.at(i)});
        }
        at += length;
    }
    // Scale degrees from `root`, each `length` long, fingered `fingers`.
    void degrees(int root, const std::vector<int>& ds, const std::vector<int>& fingers, double length)
    {
        for (std::size_t i = 0; i < ds.size(); ++i) note(degree(root, ds.at(i)), fingers.at(i), length);
    }
};

struct Hands
{
    Line right{.notes = {}, .left = false, .at = 0.0};
    Line left{.notes = {}, .left = true, .at = 0.0};
};

// Fingers for scale degrees in a five-finger position: the right hand's
// thumb on the root, the left hand's little finger on it.
std::vector<int> rightFive(const std::vector<int>& ds)
{
    std::vector<int> f;
    std::ranges::transform(ds, std::back_inserter(f), [](int d) { return d + 1; });
    return f;
}
std::vector<int> leftFive(const std::vector<int>& ds)
{
    std::vector<int> f;
    std::ranges::transform(ds, std::back_inserter(f), [](int d) { return 5 - d; });
    return f;
}

// A five-finger pattern in both hands, the left an octave below; the last
// note held for `last` beats.
void fivePattern(Hands& h, int root, const std::vector<int>& ds, double length, double last)
{
    for (std::size_t i = 0; i < ds.size(); ++i) {
        const double l = i + 1 == ds.size() ? last : length;
        h.right.note(degree(root, ds.at(i)), rightFive({ds.at(i)}).front(), l);
        h.left.note(degree(root - 12, ds.at(i)), leftFive({ds.at(i)}).front(), l);
    }
}

std::vector<int> reversedTail(const std::vector<int>& up)
{
    // Back down without the top note again.
    std::vector<int> down(up.rbegin() + 1, up.rend());
    return down;
}

std::vector<int> joined(std::vector<int> a, const std::vector<int>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Hands build(const QString& id)
{
    Hands h;
    // ---- Beginner: quarter notes
    if (id == "b-run"_L1) {
        // Every finger, one per key: C D E F G F E D, four times round,
        // flowing, then the C held.
        std::vector<int> ds;
        for (int round = 0; round < 4; ++round) ds.insert(ds.end(), {0, 1, 2, 3, 4, 3, 2, 1});
        ds.push_back(0);
        std::vector<double> lengths(ds.size(), kEighth);
        lengths.back() = 2.0;
        for (std::size_t i = 0; i < ds.size(); ++i) {
            h.right.note(degree(60, ds.at(i)), ds.at(i) + 1, lengths.at(i));
            h.left.note(degree(48, ds.at(i)), 5 - ds.at(i), lengths.at(i));
        }
    } else if (id == "b-five-c"_L1) {
        fivePattern(h, 60, {0, 1, 2, 3, 4, 3, 2, 1, 0}, kQuarter, 2.0);
    } else if (id == "b-five-g"_L1) {
        fivePattern(h, 67, {0, 1, 2, 3, 4, 3, 2, 1, 0}, kQuarter, 2.0);
    } else if (id == "b-skips"_L1) {
        fivePattern(h, 60, {0, 2, 1, 3, 2, 4, 3, 1, 0}, kQuarter, 2.0);
    }
    // ---- Intermediate: eighth notes
    else if (id == "i-five-keys"_L1) {
        for (const int root : {60, 67, 62, 65}) fivePattern(h, root, {0, 1, 2, 3, 4, 3, 2, 1}, kEighth, kEighth);
        fivePattern(h, 60, {0}, kQuarter, 2.0);
    } else if (id == "i-scale-c"_L1 || id == "i-scale-g"_L1) {
        const int root = id == "i-scale-c"_L1 ? 60 : 67;
        const std::vector<int> up{0, 1, 2, 3, 4, 5, 6, 7};
        const std::vector<int> ds = joined(up, reversedTail(up));
        const std::vector<int> rightUp{1, 2, 3, 1, 2, 3, 4, 5};
        const std::vector<int> leftUp{5, 4, 3, 2, 1, 3, 2, 1};
        h.right.degrees(root, ds, joined(rightUp, reversedTail(rightUp)), kEighth);
        h.left.degrees(root - 12, ds, joined(leftUp, reversedTail(leftUp)), kEighth);
    } else if (id == "i-broken-chords"_L1) {
        // I, IV, V7, I in C, each chord up and back down note by note; the V7
        // (B D F G, fingered 1 2 4 5) brings the fourth finger in.
        struct Shape
        {
            std::vector<int> notes;
            std::vector<int> right;
            std::vector<int> left;
        };
        const std::array<Shape, 4> shapes{Shape{.notes = {60, 64, 67}, .right = {1, 3, 5}, .left = {5, 3, 1}},
                                          Shape{.notes = {60, 65, 69}, .right = {1, 2, 5}, .left = {5, 2, 1}},
                                          Shape{.notes = {59, 62, 65, 67}, .right = {1, 2, 4, 5}, .left = {5, 4, 2, 1}},
                                          Shape{.notes = {60, 64, 67}, .right = {1, 3, 5}, .left = {5, 3, 1}}};
        for (const Shape& s : shapes) {
            // Up through the chord and back down to its second note.
            std::vector<std::size_t> order(s.notes.size());
            std::iota(order.begin(), order.end(), std::size_t{0});
            for (std::size_t i = s.notes.size() - 1; i-- > 1;) order.push_back(i);
            for (const std::size_t i : order) {
                h.right.note(s.notes.at(i), s.right.at(i), kEighth);
                h.left.note(s.notes.at(i) - 12, s.left.at(i), kEighth);
            }
        }
        h.right.note(60, 1, 2.0);
        h.left.note(48, 5, 2.0);
    } else if (id == "i-ode"_L1) {
        // Ode to Joy (Beethoven): the tune in C position, in both hands an
        // octave apart (the left hand's little finger on C), every finger playing.
        constexpr int C = 60;
        constexpr int D = 62;
        constexpr int E = 64;
        constexpr int F = 65;
        constexpr int G = 67;
        const std::vector<std::pair<int, double>> tune{
            {E, 1}, {E, 1}, {F, 1}, {G, 1}, {G, 1}, {F, 1}, {E, 1}, {D, 1}, {C, 1}, {C, 1}, {D, 1}, {E, 1}, {E, 1.5}, {D, 0.5}, {D, 2},
            {E, 1}, {E, 1}, {F, 1}, {G, 1}, {G, 1}, {F, 1}, {E, 1}, {D, 1}, {C, 1}, {C, 1}, {D, 1}, {E, 1}, {D, 1.5}, {C, 0.5}, {C, 2}};
        const std::map<int, int> finger{{C, 1}, {D, 2}, {E, 3}, {F, 4}, {G, 5}}; // one finger per key
        for (const auto& [pitch, length] : tune) {
            h.right.note(pitch, finger.at(pitch), length);
            h.left.note(pitch - 12, 6 - finger.at(pitch), length);
        }
    } else if (id == "i-contrary"_L1) {
        // Both thumbs start on C and the hands move apart, then back.
        const std::vector<int> ds{0, 1, 2, 3, 4, 3, 2, 1, 0};
        for (std::size_t i = 0; i < ds.size(); ++i) {
            const double l = i + 1 == ds.size() ? kQuarter : kEighth;
            h.right.note(degree(60, ds.at(i)), ds.at(i) + 1, l);
            h.left.note(degree(48, -ds.at(i)), ds.at(i) + 1, l);
        }
    }
    // ---- Pro: sixteenth notes
    else if (id == "p-hanon-1"_L1) {
        // Hanon, The Virtuoso Pianist, No. 1: up an octave, then back down.
        const std::vector<int> rightUp{1, 2, 3, 4, 5, 4, 3, 2};
        const std::vector<int> leftUp{5, 4, 3, 2, 1, 2, 3, 4};
        for (int g = 0; g < 7; ++g) {
            const std::vector<int> ds{g, g + 2, g + 3, g + 4, g + 5, g + 4, g + 3, g + 2};
            h.right.degrees(60, ds, rightUp, kSixteenth);
            h.left.degrees(48, ds, leftUp, kSixteenth);
        }
        for (int top = 11; top >= 5; --top) {
            const std::vector<int> ds{top, top - 2, top - 3, top - 4, top - 5, top - 4, top - 3, top - 2};
            h.right.degrees(60, ds, leftUp, kSixteenth); // (mirrored)
            h.left.degrees(48, ds, rightUp, kSixteenth);
        }
        h.right.note(60, 1, kQuarter);
        h.left.note(48, 5, kQuarter);
    } else if (id == "p-scale-c"_L1) {
        std::vector<int> up(15);
        std::iota(up.begin(), up.end(), 0);
        const std::vector<int> rightUp{1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 5};
        const std::vector<int> leftUp{5, 4, 3, 2, 1, 3, 2, 1, 4, 3, 2, 1, 3, 2, 1};
        const std::vector<int> ds = joined(up, reversedTail(up));
        h.right.degrees(60, ds, joined(rightUp, reversedTail(rightUp)), kSixteenth);
        h.left.degrees(48, ds, joined(leftUp, reversedTail(leftUp)), kSixteenth);
    } else if (id == "p-contrary-c"_L1) {
        std::vector<int> up(15);
        std::iota(up.begin(), up.end(), 0);
        const std::vector<int> fingersOut{1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 5}; // each hand moving outwards
        const std::vector<int> ds = joined(up, reversedTail(up));
        std::vector<int> down;
        std::ranges::transform(ds, std::back_inserter(down), [](int d) { return -d; });
        h.right.degrees(60, ds, joined(fingersOut, reversedTail(fingersOut)), kSixteenth);
        h.left.degrees(48, down, joined(fingersOut, reversedTail(fingersOut)), kSixteenth);
    } else if (id == "p-arpeggio-c"_L1) {
        const std::vector<int> ds{0, 2, 4, 7, 9, 11, 14};
        const std::vector<int> rightUp{1, 2, 3, 1, 2, 3, 5};
        const std::vector<int> leftUp{5, 4, 2, 1, 4, 2, 1};
        const std::vector<int> all = joined(ds, reversedTail(ds));
        h.right.degrees(60, all, joined(rightUp, reversedTail(rightUp)), kSixteenth);
        h.left.degrees(48, all, joined(leftUp, reversedTail(leftUp)), kSixteenth);
    } else if (id == "p-inversions"_L1) {
        // C major up through its inversions and back: root, 1st, 2nd, root an octave up.
        const std::vector<std::vector<int>> chords{{60, 64, 67}, {64, 67, 72}, {67, 72, 76}, {72, 76, 79}, {67, 72, 76}, {64, 67, 72}, {60, 64, 67}};
        const std::vector<std::vector<int>> rightF{{1, 3, 5}, {1, 2, 5}, {1, 3, 5}, {1, 3, 5}, {1, 3, 5}, {1, 2, 5}, {1, 3, 5}};
        const std::vector<std::vector<int>> leftF{{5, 3, 1}, {5, 3, 1}, {5, 2, 1}, {5, 3, 1}, {5, 2, 1}, {5, 3, 1}, {5, 3, 1}};
        for (std::size_t i = 0; i < chords.size(); ++i) {
            const double l = i + 1 == chords.size() ? 2.0 : kEighth;
            std::vector<int> low;
            std::ranges::transform(chords.at(i), std::back_inserter(low), [](int p) { return p - 12; });
            h.right.chord(chords.at(i), rightF.at(i), l);
            h.left.chord(low, leftF.at(i), l);
        }
    }
    return h;
}

double mean(const std::vector<double>& values)
{
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double spread(const std::vector<double>& values)
{
    if (values.size() < 2) return 0.0;
    const double m = mean(values);
    const double sum = std::accumulate(values.begin(), values.end(), 0.0, [m](double s, double v) { return s + ((v - m) * (v - m)); });
    return std::sqrt(sum / static_cast<double>(values.size()));
}

} // namespace

std::vector<WarmupExercise> warmupExercises(WarmupLevel level)
{
    const auto e = [level](const char* id, const char* name, const char* tip) {
        const double start = level == WarmupLevel::Beginner ? 60.0 : level == WarmupLevel::Intermediate ? 70.0 : 80.0;
        const double target = level == WarmupLevel::Beginner ? 90.0 : level == WarmupLevel::Intermediate ? 110.0 : 120.0;
        return WarmupExercise{.id = QString::fromLatin1(id), .name = tr(name), .tip = tr(tip), .level = level, .startTempo = start, .targetTempo = target};
    };
    switch (level) {
    case WarmupLevel::Beginner:
        return {e("b-run", "Warm-up run",
                  "One finger per key, C to G and back, round and round without stopping: every finger, smooth and even, fingers curved."),
                e("b-five-c", "Five fingers from C", "C D E F G and back, one finger per key, thumb on C (left hand: little finger on C)."),
                e("b-five-g", "Five fingers from G", "The same shape moved to G: G A B C D and back."),
                e("b-skips", "Skipping fingers", "C E D F E G F D C: every other key, fingers lifted only a little.")};
    case WarmupLevel::Intermediate:
        return {e("i-five-keys", "Five fingers in four keys", "C, G, D and F, one after the other: let the hand move as one, black keys and all."),
                e("i-scale-c", "C major scale", "One octave up and back: the thumb passes under after E (right hand) and the third finger crosses over."),
                e("i-scale-g", "G major scale", "The same fingering from G, with F sharp."),
                e("i-broken-chords", "Broken chords I-IV-V-I", "C, F and G chords played one note at a time: the hand stays shaped over each chord."),
                e("i-ode", "Ode to Joy", "Beethoven's tune, one finger per key: right hand, left hand an octave lower, then both together."),
                e("i-contrary", "Hands moving apart", "Both thumbs start on C and the hands move away from each other, then back: a mirror.")};
    case WarmupLevel::Pro:
        return {e("p-hanon-1", "Hanon No. 1", "Every finger equally strong: each note the same loudness, wrists still."),
                e("p-scale-c", "C major, two octaves", "Hands together, the thumbs passing under at the same moment: smooth, no bumps."),
                e("p-contrary-c", "C major in contrary motion", "Two octaves outwards and back: the hands mirror each other's fingering."),
                e("p-arpeggio-c", "C major arpeggio", "Two octaves: the thumb under, the arm leading the hand along."),
                e("p-inversions", "Chord inversions", "C major up through its inversions and back: every note of each chord at once.")};
    }
    return {};
}

std::vector<WarmupNote> warmupNotes(const WarmupExercise& exercise, WarmupHands hands)
{
    Hands h = build(exercise.id);
    std::vector<WarmupNote> notes;
    if (hands != WarmupHands::Left) notes = std::move(h.right.notes);
    if (hands != WarmupHands::Right) notes.insert(notes.end(), h.left.notes.begin(), h.left.notes.end());
    std::ranges::stable_sort(notes, {}, &WarmupNote::start);
    return notes;
}

PracticeTimeline warmupTimeline(const std::vector<WarmupNote>& notes)
{
    PracticeTimeline timeline;
    timeline.beatsPerBar = 4;
    double end = 0.0;
    // Notes starting together and as long make one chord (each its own length otherwise).
    for (const WarmupNote& note : notes) {
        const double start = kWarmupCountIn + note.start;
        auto chord = std::ranges::find_if(timeline.chords, [&](const PracticeChord& c) {
            return std::abs(c.start - start) < 1e-9 && std::abs(c.length - note.length) < 1e-9;
        });
        if (chord == timeline.chords.end()) {
            timeline.chords.push_back(PracticeChord{.name = {}, .section = -1, .start = start, .length = note.length, .bass = note.pitch,
                                                    .left = {}, .right = {}, .fingers = {}});
            chord = std::prev(timeline.chords.end());
        }
        (note.left ? chord->left : chord->right).push_back(note.pitch);
        chord->fingers[note.pitch] = note.finger;
        end = std::max(end, start + note.length);
    }
    for (PracticeChord& chord : timeline.chords) {
        std::ranges::sort(chord.left);
        std::ranges::sort(chord.right);
        chord.bass = !chord.left.empty() ? chord.left.front() : chord.right.empty() ? 36 : chord.right.front();
    }
    std::ranges::stable_sort(timeline.chords, {}, &PracticeChord::start);
    timeline.length = end;
    return timeline;
}

WarmupLimits warmupLimits(WarmupLevel level)
{
    switch (level) {
    case WarmupLevel::Beginner: return WarmupLimits{.timingMs = 80.0, .handsMs = -1.0};
    case WarmupLevel::Intermediate: return WarmupLimits{.timingMs = 50.0, .handsMs = 60.0};
    case WarmupLevel::Pro: return WarmupLimits{.timingMs = 30.0, .handsMs = 40.0};
    }
    return {};
}

WarmupScore scoreWarmup(const std::vector<WarmupNote>& expected, const std::vector<PlayedKey>& played, double tempo, WarmupLevel level)
{
    WarmupScore score;
    const WarmupLimits limits = warmupLimits(level);
    const double msPerBeat = tempo > 0.0 ? 60000.0 / tempo : 1000.0;
    const double window = std::min(300.0, msPerBeat / 2.0) / msPerBeat; // in beats
    std::vector<bool> used(played.size(), false);
    std::vector<double> offsets;
    std::vector<double> velocities;
    score.notes = static_cast<int>(expected.size());
    score.perNote.reserve(expected.size());
    for (const WarmupNote& note : expected) {
        std::size_t best = played.size();
        for (std::size_t k = 0; k < played.size(); ++k) {
            if (used.at(k) || played.at(k).pitch != note.pitch) continue;
            const double distance = std::abs(played.at(k).beat - note.start);
            if (distance > window + 1e-9) continue;
            if (best == played.size() || distance < std::abs(played.at(best).beat - note.start)) best = k;
        }
        WarmupNoteResult result{.pitch = note.pitch, .start = note.start, .left = note.left, .kind = WarmupNoteResult::Missed, .offsetMs = 0.0};
        if (best < played.size()) {
            used.at(best) = true;
            result.offsetMs = (played.at(best).beat - note.start) * msPerBeat;
            result.kind = std::abs(result.offsetMs) <= limits.timingMs ? WarmupNoteResult::OnTime
                          : result.offsetMs < 0.0                     ? WarmupNoteResult::Early
                                                                       : WarmupNoteResult::Late;
            offsets.push_back(result.offsetMs);
            velocities.push_back(played.at(best).velocity);
            ++score.right;
        } else {
            ++score.missed;
        }
        score.perNote.push_back(result);
    }
    // Keys nobody asked for, while the exercise ran.
    double first = 0.0;
    double last = 0.0;
    if (!expected.empty()) {
        first = expected.front().start;
        last = std::accumulate(expected.begin(), expected.end(), 0.0,
                               [](double end, const WarmupNote& note) { return std::max(end, note.start + note.length); });
    }
    for (std::size_t k = 0; k < played.size(); ++k) {
        if (!used.at(k) && played.at(k).beat >= first - window && played.at(k).beat <= last + window) ++score.extra;
    }
    std::vector<double> distances;
    std::ranges::transform(offsets, std::back_inserter(distances), [](double o) { return std::abs(o); });
    score.timingMs = mean(distances);
    score.driftMs = mean(offsets);
    score.evennessMs = spread(offsets);
    score.touchSpread = spread(velocities);
    // The hands on notes meant together: how far apart, and which comes after.
    std::vector<double> apart;
    std::vector<double> leftBehind;
    for (const WarmupNoteResult& l : score.perNote) {
        if (!l.left || l.kind == WarmupNoteResult::Missed) continue;
        const auto r = std::ranges::find_if(score.perNote, [&l](const WarmupNoteResult& n) {
            return !n.left && n.kind != WarmupNoteResult::Missed && std::abs(n.start - l.start) < 1e-9;
        });
        if (r == score.perNote.end()) continue;
        apart.push_back(std::abs(l.offsetMs - r->offsetMs));
        leftBehind.push_back(l.offsetMs - r->offsetMs);
    }
    if (!apart.empty()) score.handsApartMs = mean(apart);

    const bool handsTogether = limits.handsMs < 0.0 || score.handsApartMs < 0.0 || score.handsApartMs <= limits.handsMs;
    score.clean = score.notes > 0 && score.missed == 0 && score.extra <= 1 && score.timingMs <= limits.timingMs && handsTogether;
    const double share = score.notes > 0 ? static_cast<double>(score.right) / score.notes : 0.0;
    score.stars = score.clean                                              ? 3
                  : share >= 0.9 - 1e-9 && score.timingMs <= 2.0 * limits.timingMs ? 2
                  : share >= 0.6                                           ? 1
                                                                           : 0;
    // The one thing to do next.
    if (score.right == 0) {
        score.tip = tr("Nothing was heard: check your keyboard is on (the MIDI light flashes), then play along with the falling notes.");
    } else if (score.missed > 0) {
        score.tip = tr("You missed %n note(s): slow down until every one is there.", nullptr, score.missed);
    } else if (score.extra > 1) {
        score.tip = tr("%n extra key(s) crept in: keep each finger over its own key.", nullptr, score.extra);
    } else if (!handsTogether) {
        const bool leftLate = mean(leftBehind) > 0.0;
        score.tip = tr("Your %1 hand lands %2 ms after your %3: let both hands fall together.")
                        .arg(leftLate ? tr("left") : tr("right"))
                        .arg(std::lround(score.handsApartMs))
                        .arg(leftLate ? tr("right") : tr("left"));
    } else if (score.timingMs > limits.timingMs) {
        score.tip = score.driftMs > 0.0 ? tr("You're %1 ms behind the beat on average: play right as each note lands.").arg(std::lround(score.driftMs))
                                        : tr("You're %1 ms ahead of the beat on average: wait for each note to land.").arg(std::lround(-score.driftMs));
    } else if (score.evennessMs > limits.timingMs / 2.0) {
        score.tip = tr("Right notes, right time: now make the rhythm as even as a clock.");
    } else if (score.touchSpread > 20.0) {
        score.tip = tr("Even it out: every note the same loudness.");
    } else {
        score.tip = tr("Clean! Three clean runs with both hands and the tempo goes up.");
    }
    return score;
}

WarmupStep recordWarmupRun(WarmupProgress& progress, const WarmupExercise& exercise, const WarmupScore& score, WarmupHands hands)
{
    WarmupStep step;
    progress.bestStars = std::max(progress.bestStars, score.stars);
    if (hands != WarmupHands::Both) return step; // one hand alone: the way there, not the test
    if (!score.clean) {
        progress.cleanRuns = 0;
        return step;
    }
    if (++progress.cleanRuns < 3) return step;
    progress.cleanRuns = 0;
    const double now = warmupTempo(progress, exercise);
    if (now >= exercise.targetTempo - 1e-9) {
        step.justPassed = !progress.passed;
        progress.passed = true;
        return step;
    }
    progress.tempo = std::min(now + 5.0, exercise.targetTempo);
    step.tempoUp = true;
    return step;
}

double warmupTempo(const WarmupProgress& progress, const WarmupExercise& exercise)
{
    return progress.tempo > 0.0 ? std::clamp(progress.tempo, exercise.startTempo, exercise.targetTempo) : exercise.startTempo;
}

WarmupLevel unlockedWarmupLevel(const std::map<QString, WarmupProgress>& progress)
{
    const auto passedAll = [&progress](WarmupLevel level) {
        return std::ranges::all_of(warmupExercises(level), [&progress](const WarmupExercise& e) {
            const auto found = progress.find(e.id);
            return found != progress.end() && found->second.passed;
        });
    };
    if (!passedAll(WarmupLevel::Beginner)) return WarmupLevel::Beginner;
    if (!passedAll(WarmupLevel::Intermediate)) return WarmupLevel::Intermediate;
    return WarmupLevel::Pro;
}

} // namespace gigchain::core
