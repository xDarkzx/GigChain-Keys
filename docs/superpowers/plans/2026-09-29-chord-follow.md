# Chord Follow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The chart follows the chords the player plays: the song starts on its first chord, the current chord lights up, the chart scrolls, jumps to another section are found from two chords, and a section's instruments take over on the note that entered it.

**Architecture:** `core` parses chord names (`Chords`) and turns a chart into steps (`SongMap`). The engine gets the steps as a `ChordFollowMap` through `IEngine::setChordFollow` (published like the song timeline) and an audio-thread `ChordFollower` that applies the five rules to each block's MIDI and returns the block's `SectionGate` (with a hand-over of held keys at the switch). The UI builds the map in `DocumentController::applySectionsToEngine`, reads `IEngine::chordFollow()` through `EngineStatus`, and lights the chart.

**Tech Stack:** C++20, Qt 6.10 (QML, QtTest), MSVC, CMake presets, the existing engine (`RenderGraph`, `SongTransport`, `HazardExchange`).

**Spec:** `docs/superpowers/specs/2026-09-29-chord-follow-design.md`

## Global Constraints

- Audio thread code (`ChordFollower::process`, `ChannelStrip::render`) allocates nothing and takes no locks; state crossing threads is atomics or a `HazardExchange`.
- Never swallow errors: every failure is returned with its cause and logged (`qCWarning(lcEngine)`/`lcUi`).
- Source edits with the Edit tool only (no sed/Python rewrites).
- A task is done only when `tools\verify.ps1` passes (build `/W4 /WX`, clang-tidy and cppcheck with no findings on changed lines, qmllint, every test) and its tests failed before the change.
- No product name typed in `src/` (branding comes from `branding.cmake`).
- UI text in NZ/British spelling ("colour"), plain words, no jargon.
- Tests: Qt Test, one executable per suite via `gigchain_add_test(<name> SOURCES ... LIBS ...)`; run one with `tools\build.ps1 -Target <name> -Filter <name>`.
- Chord follow is on by default per song ("My chords"); the tempo mode ("Play") stays as it is.

## Review Focus

1. **Two sections starting with the same two chords (Verse 1 and Verse 2):** a jump goes to the next one after the current section, not the first in the song. Test: Task 3, `sameOpeningGoesToTheNextOne`.
2. **Keys let go under the pedal at a section switch:** they are not handed to the new instruments (no note-off would ever follow: stuck notes). Test: Task 3, `sustainedKeysAreNotHandedOver`.
3. **A chart edited while following:** the chart keeps its place instead of jumping back to the start. Test: Task 7, `anEditedChartKeepsItsPlace`.
4. **A chart with chords but no sections:** following highlights and moves, and every instrument keeps playing. Test: Task 3, `withoutSectionsItStillFollows`; Task 5 routes the gate only when the song has sections.
5. **Panic or "all notes off" mid-song:** following waits for the first chord again and forgets held keys. Test: Task 3, `panicWaitsForTheFirstChordAgain`.

---

### Task 1: Chord names (core::parseChordName)

**Files:**
- Create: `src/core/include/gigchain/core/Chords.h`
- Create: `src/core/internal/Chords.cpp`
- Modify: `src/core/CMakeLists.txt` (add both files to the `gigchain_core` sources)
- Create: `tests/core/tst_chords.cpp`
- Modify: `tests/core/CMakeLists.txt` (add `gigchain_add_test(tst_chords SOURCES tst_chords.cpp LIBS gigchain::core)`)

**Interfaces:**
- Produces: `struct core::ChordShape { int root; uint16_t family; int bass; int third; int colour; }` and `std::optional<core::ChordShape> core::parseChordName(const QString& name)`. `family` bit n = pitch class n (C = 0) of every note the name gives, root included; `third` and `colour` are intervals in semitones above the root (third 3 or 4, -1 none; colour 2, 5 or 7 when there is no third, else -1); `bass` is a pitch class or -1.

- [ ] **Step 1: Write the failing test**

`tests/core/tst_chords.cpp`:

```cpp
// Chord names as charts write them, to the notes they mean (for following
// what is played).
#include "gigchain/core/Chords.h"

#include <QtTest>

#include <initializer_list>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

uint16_t notes(std::initializer_list<int> pitchClasses)
{
    uint16_t bits = 0;
    for (const int pc : pitchClasses) bits |= static_cast<uint16_t>(1U << pc);
    return bits;
}

} // namespace

class TestChords : public QObject
{
    Q_OBJECT

private slots:
    void names_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("root");
        QTest::addColumn<int>("family");
        QTest::addColumn<int>("bass");
        QTest::addColumn<int>("third");
        QTest::addColumn<int>("colour");
        QTest::newRow("C") << u"C"_s << 0 << int(notes({0, 4, 7})) << -1 << 4 << -1;
        QTest::newRow("Am") << u"Am"_s << 9 << int(notes({9, 0, 4})) << -1 << 3 << -1;
        QTest::newRow("G#m7") << u"G#m7"_s << 8 << int(notes({8, 11, 3, 6})) << -1 << 3 << -1;
        QTest::newRow("Bbmaj7/D") << u"Bbmaj7/D"_s << 10 << int(notes({10, 2, 5, 9})) << 2 << 4 << -1;
        QTest::newRow("CM7") << u"CM7"_s << 0 << int(notes({0, 4, 7, 11})) << -1 << 4 << -1;
        QTest::newRow("C-7") << u"C-7"_s << 0 << int(notes({0, 3, 7, 10})) << -1 << 3 << -1;
        QTest::newRow("Csus") << u"Csus"_s << 0 << int(notes({0, 5, 7})) << -1 << -1 << 5;
        QTest::newRow("Csus2") << u"Csus2"_s << 0 << int(notes({0, 2, 7})) << -1 << -1 << 2;
        QTest::newRow("D7sus4") << u"D7sus4"_s << 2 << int(notes({2, 7, 9, 0})) << -1 << -1 << 5;
        QTest::newRow("E5") << u"E5"_s << 4 << int(notes({4, 11})) << -1 << -1 << 7;
        QTest::newRow("F#m7b5") << u"F#m7b5"_s << 6 << int(notes({6, 9, 0, 4})) << -1 << 3 << -1;
        QTest::newRow("Adim7") << u"Adim7"_s << 9 << int(notes({9, 0, 3, 6})) << -1 << 3 << -1;
        QTest::newRow("Caug") << u"Caug"_s << 0 << int(notes({0, 4, 8})) << -1 << 4 << -1;
        QTest::newRow("Dadd9") << u"Dadd9"_s << 2 << int(notes({2, 6, 9, 4})) << -1 << 4 << -1;
        QTest::newRow("Cmaj7(no3)") << u"Cmaj7(no3)"_s << 0 << int(notes({0, 7, 11})) << -1 << -1 << 7;
        QTest::newRow("Ebmaj9") << u"Ebmaj9"_s << 3 << int(notes({3, 7, 10, 2, 5})) << -1 << 4 << -1;
        QTest::newRow("C/E") << u"C/E"_s << 0 << int(notes({0, 4, 7})) << 4 << 4 << -1;
        QTest::newRow("spaces") << u"  Am7  "_s << 9 << int(notes({9, 0, 4, 7})) << -1 << 3 << -1;
    }
    void names()
    {
        QFETCH(QString, name);
        QFETCH(int, root);
        QFETCH(int, family);
        QFETCH(int, bass);
        QFETCH(int, third);
        QFETCH(int, colour);
        const auto shape = parseChordName(name);
        if (!shape) QFAIL(qPrintable(u"not understood: "_s + name));
        QCOMPARE(shape->root, root);
        QCOMPARE(int(shape->family), family);
        QCOMPARE(shape->bass, bass);
        QCOMPARE(shape->third, third);
        QCOMPARE(shape->colour, colour);
    }

    void notChords_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("no chord") << u"N.C."_s;
        QTest::newRow("repeat") << u"x2"_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("a word") << u"Hello"_s;
        QTest::newRow("lower case") << u"am"_s;
    }
    void notChords()
    {
        QFETCH(QString, name);
        QVERIFY(!parseChordName(name).has_value());
    }
};

QTEST_GUILESS_MAIN(TestChords)
#include "tst_chords.moc"
```

- [ ] **Step 2: Run it to see it fail**

Run: `tools\build.ps1 -Target tst_chords -Filter tst_chords`
Expected: build error, `gigchain/core/Chords.h` not found.

- [ ] **Step 3: Write the header**

`src/core/include/gigchain/core/Chords.h`:

```cpp
#pragma once

#include <QString>

#include <cstdint>
#include <optional>

namespace gigchain::core {

// What a chord name means, for following what is played. Pitch classes
// 0-11 (C = 0).
struct ChordShape
{
    int root = 0;        // pitch class
    uint16_t family = 0; // every note the name gives, root included: bit n = pitch class n
    int bass = -1;       // a written slash bass ("D/E": E); -1 = none
    int third = -1;      // semitones above the root: 3 minor, 4 major; -1 = none (sus, 5, no3)
    int colour = -1;     // without a third, what stands in: 2 or 5 (sus2, sus4), 7 (a 5 chord); else -1

    friend bool operator==(const ChordShape&, const ChordShape&) = default;
};

// The chord a name like "G#m7", "Bbmaj7/D", "Csus", "E5" or "F#m7b5"
// means. A root A-G with # or b, then m/min/-, maj/M/Δ, dim/°, aug/+, ø,
// sus2/sus4/sus, 5, 6, 7, 9, 11, 13, add9/add11, no3, b5 #5 b9 #9 #11 b13
// and /bass, in any order; anything else after a clear root is skipped.
// nullopt when there is no clear root (a typo, "N.C.", a word).
[[nodiscard]] std::optional<ChordShape> parseChordName(const QString& name);

} // namespace gigchain::core
```

- [ ] **Step 4: Write the parser**

`src/core/internal/Chords.cpp`:

```cpp
#include "gigchain/core/Chords.h"

#include <QStringView>

#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

constexpr uint16_t bit(int interval)
{
    return static_cast<uint16_t>(1U << (((interval % 12) + 12) % 12));
}

int letterClass(QChar letter)
{
    switch (letter.unicode()) {
    case u'C': return 0;
    case u'D': return 2;
    case u'E': return 4;
    case u'F': return 5;
    case u'G': return 7;
    case u'A': return 9;
    case u'B': return 11;
    default: return -1;
    }
}

// A note name at the start of `text` ("F#", "Bb", "E"): its pitch class and
// how many characters it takes; {-1, 0} when there is none.
std::pair<int, qsizetype> readNote(QStringView text)
{
    if (text.isEmpty()) return {-1, 0};
    const int letter = letterClass(text.front());
    if (letter < 0) return {-1, 0};
    if (text.size() > 1) {
        const QChar accidental = text.at(1);
        if (accidental == u'#' || accidental == u'♯') return {(letter + 1) % 12, 2};
        if (accidental == u'b' || accidental == u'♭') return {(letter + 11) % 12, 2};
    }
    return {letter, 1};
}

} // namespace

std::optional<ChordShape> parseChordName(const QString& name)
{
    const QString text = name.trimmed();
    const auto [root, rootLength] = readNote(text);
    if (root < 0) return std::nullopt;
    QString quality = text.mid(rootLength);

    ChordShape shape;
    shape.root = root;
    // "/E" is a bass note; "6/9" is not one.
    if (const qsizetype slash = quality.lastIndexOf(u'/'); slash >= 0) {
        const auto [bass, length] = readNote(QStringView(quality).sliced(slash + 1));
        if (bass >= 0 && slash + 1 + length == quality.size()) {
            shape.bass = bass;
            quality.truncate(slash);
        }
    }
    quality.remove(u'(').remove(u')').remove(u' ');

    const auto take = [&quality](QStringView token, Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive) {
        if (!quality.startsWith(token, sensitivity)) return false;
        quality.remove(0, token.size());
        return true;
    };
    int third = 4;
    int fifth = 7;
    int seventh = -1;
    bool majorSeventh = false; // "maj7", "M9": the seventh a numbered chord adds is major
    bool power = false;
    int sus = -1;
    uint16_t added = 0;

    // The chord's kind first.
    if (take(u"maj") || take(u"M", Qt::CaseSensitive)) {
        majorSeventh = true;
    } else if (take(u"Δ")) { // Δ: a major seventh, even written alone
        majorSeventh = true;
        seventh = 11;
    } else if (take(u"min") || take(u"m", Qt::CaseSensitive) || take(u"-")) {
        third = 3;
        if (take(u"maj") || take(u"M", Qt::CaseSensitive)) majorSeventh = true; // mMaj7
    } else if (take(u"dim") || take(u"°") || take(u"o", Qt::CaseSensitive)) {
        third = 3;
        fifth = 6;
        if (take(u"7")) seventh = 9; // a diminished seventh
    } else if (take(u"aug") || take(u"+")) {
        fifth = 8;
    } else if (take(u"ø")) { // ø: half-diminished
        third = 3;
        fifth = 6;
        seventh = 10;
        (void)take(u"7");
    }
    const auto addSeventh = [&seventh, majorSeventh] {
        if (seventh < 0) seventh = majorSeventh ? 11 : 10;
    };
    // Then numbers, sus, add and alterations, in any order; anything else is skipped.
    while (!quality.isEmpty()) {
        if (take(u"sus2")) {
            sus = 2;
        } else if (take(u"sus4") || take(u"sus")) {
            sus = 5;
        } else if (take(u"add9") || take(u"add2")) {
            added |= bit(2);
        } else if (take(u"add11") || take(u"add4")) {
            added |= bit(5);
        } else if (take(u"no3")) {
            third = -1;
        } else if (take(u"13")) {
            addSeventh();
            added |= bit(2) | bit(9);
        } else if (take(u"11")) {
            addSeventh();
            added |= bit(2) | bit(5);
        } else if (take(u"9")) {
            addSeventh();
            added |= bit(2);
        } else if (take(u"7")) {
            addSeventh();
        } else if (take(u"6")) {
            added |= bit(9);
        } else if (take(u"b5")) {
            fifth = 6;
        } else if (take(u"#5")) {
            fifth = 8;
        } else if (take(u"5")) {
            power = true;
        } else if (take(u"b9")) {
            added |= bit(1);
        } else if (take(u"#9")) {
            added |= bit(3);
        } else if (take(u"#11")) {
            added |= bit(6);
        } else if (take(u"b13")) {
            added |= bit(8);
        } else {
            quality.remove(0, 1);
        }
    }
    if (power || sus >= 0) third = -1;

    uint16_t intervals = bit(0) | bit(fifth) | added;
    if (third >= 0) intervals |= bit(third);
    if (seventh >= 0) intervals |= bit(seventh);
    if (sus >= 0) intervals |= bit(sus);
    for (int interval = 0; interval < 12; ++interval) {
        if ((intervals & bit(interval)) != 0) shape.family |= bit(root + interval);
    }
    shape.third = third;
    shape.colour = third >= 0 ? -1 : sus >= 0 ? sus : 7;
    return shape;
}

} // namespace gigchain::core
```

Add to `src/core/CMakeLists.txt`, in the `gigchain_core` source list next to `Chart`:

```cmake
    include/gigchain/core/Chords.h
    internal/Chords.cpp
```

- [ ] **Step 5: Run the test to see it pass**

Run: `tools\build.ps1 -Target tst_chords -Filter tst_chords`
Expected: PASS, every row of `names` and `notChords`.

- [ ] **Step 6: Commit**

```bash
git add src/core/include/gigchain/core/Chords.h src/core/internal/Chords.cpp src/core/CMakeLists.txt tests/core/tst_chords.cpp tests/core/CMakeLists.txt
git commit -m "feat: chord names to the notes they mean (for chord follow)"
```

---

### Task 2: The song map (core::buildSongMap)

**Files:**
- Modify: `src/core/include/gigchain/core/Chart.h` (declare `lineRepeats`, `sectionRepeats`)
- Modify: `src/core/internal/Chart.cpp` (define them from the existing `repeatCount`, `kRepeatMark`, `kRepeatInLabel`)
- Create: `src/core/include/gigchain/core/SongMap.h`
- Create: `src/core/internal/SongMap.cpp`
- Modify: `src/core/CMakeLists.txt`
- Create: `tests/core/tst_song_map.cpp`
- Modify: `tests/core/CMakeLists.txt` (`gigchain_add_test(tst_song_map SOURCES tst_song_map.cpp LIBS gigchain::core)`)

**Interfaces:**
- Consumes: `core::parseChordName`, `core::ChordShape` (Task 1); `core::chartSections`, `core::ChartSection` (existing).
- Produces:
  - `int core::lineRepeats(const ChartLine& line)`, `int core::sectionRepeats(const ChartSection& section)` (1 without a repeat mark).
  - `struct core::SongStep { ChordShape shape; QString name; int section; std::vector<std::pair<int, int>> places; }` (places: chart line index, chord index on that line).
  - `struct core::SongMap { std::vector<SongStep> steps; std::vector<int> sectionStarts; bool followable() const; }` (`sectionStarts` has one entry per `chartSections()` section, -1 when it has no chord; `followable()` = at least 2 steps).
  - `core::SongMap core::buildSongMap(const Chart& chart)`.

- [ ] **Step 1: Write the failing test**

`tests/core/tst_song_map.cpp`:

```cpp
// A chart as the chords a player goes through (chord follow).
#include "gigchain/core/Chart.h"
#include "gigchain/core/SongMap.h"

#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

QStringList names(const SongMap& map)
{
    QStringList out;
    for (const SongStep& step : map.steps) out << step.name;
    return out;
}

std::vector<int> sectionsOf(const SongMap& map)
{
    std::vector<int> out;
    for (const SongStep& step : map.steps) out.push_back(step.section);
    return out;
}

} // namespace

class TestSongMap : public QObject
{
    Q_OBJECT

private slots:
    void everyChordInOrderWithItsSection()
    {
        const SongMap map = buildSongMap(parseChordPro(
            u"{sov: Verse 1}\n[Am]One [F]two\n[C]three [G]four\n{eov}\n{soc: Chorus}\n[F]Chorus [C]line\n{eoc}\n"_s));
        QCOMPARE(names(map), (QStringList{u"Am"_s, u"F"_s, u"C"_s, u"G"_s, u"F"_s, u"C"_s}));
        QCOMPARE(sectionsOf(map), (std::vector<int>{0, 0, 0, 0, 1, 1}));
        QCOMPARE(map.sectionStarts, (std::vector<int>{0, 4}));
        QCOMPARE(map.steps.at(0).places, (std::vector<std::pair<int, int>>{{1, 0}}));
        QCOMPARE(map.steps.at(3).places, (std::vector<std::pair<int, int>>{{2, 1}}));
        QVERIFY(map.followable());
    }

    void repeatsArePlayedAgain()
    {
        // A line of chords "(x2)", and a section "Chorus (x2)".
        const SongMap line = buildSongMap(parseChordPro(u"[Am]  [G]  (x2)\n"_s));
        QCOMPARE(names(line), (QStringList{u"Am"_s, u"G"_s, u"Am"_s, u"G"_s}));
        const SongMap section = buildSongMap(parseChordPro(u"{soc: Chorus (x2)}\n[F]a [C]b\n{eoc}\n"_s));
        QCOMPARE(names(section), (QStringList{u"F"_s, u"C"_s, u"F"_s, u"C"_s}));
        QCOMPARE(section.sectionStarts, (std::vector<int>{0}));
    }

    void theSameChordTwiceIsOneStep()
    {
        const SongMap map = buildSongMap(parseChordPro(u"[C]Hello [C]world [G]now [C]then [C/E]bass moves\n"_s));
        QCOMPARE(names(map), (QStringList{u"C"_s, u"G"_s, u"C"_s, u"C/E"_s}));
        QCOMPARE(map.steps.at(0).places, (std::vector<std::pair<int, int>>{{0, 0}, {0, 1}})); // lit in both places
    }

    void aSectionsFirstChordIsNeverMergedIntoThePrevious()
    {
        const SongMap map = buildSongMap(parseChordPro(u"{sov: Verse}\n[Am]a [G]b\n{eov}\n{soc: Chorus}\n[G]c [C]d\n{eoc}\n"_s));
        QCOMPARE(names(map), (QStringList{u"Am"_s, u"G"_s, u"G"_s, u"C"_s}));
        QCOMPARE(map.sectionStarts, (std::vector<int>{0, 2}));
    }

    void unreadableChordsAreNotSteps()
    {
        const SongMap map = buildSongMap(parseChordPro(u"[C]a [N.C.]b [G]c\n"_s));
        QCOMPARE(names(map), (QStringList{u"C"_s, u"G"_s}));
        QCOMPARE(map.steps.at(1).places, (std::vector<std::pair<int, int>>{{0, 2}})); // still where it is written
    }

    void chordsBeforeTheFirstSectionBelongToNone()
    {
        const SongMap map = buildSongMap(parseChordPro(u"[D]intro\n{c: Verse}\n[A]words\n"_s));
        QCOMPARE(sectionsOf(map), (std::vector<int>{-1, 0}));
        QCOMPARE(map.sectionStarts, (std::vector<int>{1}));
    }

    void aSectionWithoutChords()
    {
        const SongMap map = buildSongMap(parseChordPro(u"{c: Intro}\nspoken words\n{c: Verse}\n[A]x [D]y\n"_s));
        QCOMPARE(map.sectionStarts, (std::vector<int>{-1, 0}));
        QVERIFY(!buildSongMap(parseChordPro(u"[C]only one"_s)).followable());
        QVERIFY(!buildSongMap(parseChordPro(QString())).followable());
    }
};

QTEST_GUILESS_MAIN(TestSongMap)
#include "tst_song_map.moc"
```

- [ ] **Step 2: Run it to see it fail**

Run: `tools\build.ps1 -Target tst_song_map -Filter tst_song_map`
Expected: build error, `gigchain/core/SongMap.h` not found.

- [ ] **Step 3: Repeat counts from Chart**

In `src/core/include/gigchain/core/Chart.h`, after `chartSections`:

```cpp
// How many times a line or a section is played: 2 for a line whose words
// are only "x2" or "(x2)" (a line of chords played twice), or a title like
// "Chorus (x2)"; 1 without a repeat mark.
[[nodiscard]] int lineRepeats(const ChartLine& line);
[[nodiscard]] int sectionRepeats(const ChartSection& section);
```

In `src/core/internal/Chart.cpp`, after `chartSections` (outside the anonymous namespace):

```cpp
int lineRepeats(const ChartLine& line)
{
    return repeatCount(kRepeatMark().match(line.lyrics().trimmed()));
}

int sectionRepeats(const ChartSection& section)
{
    return repeatCount(kRepeatInLabel().match(section.label));
}
```

- [ ] **Step 4: The song map**

`src/core/include/gigchain/core/SongMap.h`:

```cpp
#pragma once

#include "gigchain/core/Chart.h"
#include "gigchain/core/Chords.h"

#include <QString>

#include <utility>
#include <vector>

namespace gigchain::core {

// One chord of a song as a player goes through it.
struct SongStep
{
    ChordShape shape;
    QString name;     // as the chart writes it
    int section = -1; // in chartSections(); -1 = before the first
    // Where it is written: (chart line, chord on that line counting from 0).
    // A chord written twice in a row, or a repeated line, has several.
    std::vector<std::pair<int, int>> places;
};

// A chart's chords in playing order: repeat marks played out, the same
// chord twice in a row (within a section) one step, chords it cannot read
// left out (docs/superpowers/specs/2026-09-29-chord-follow-design.md).
struct SongMap
{
    std::vector<SongStep> steps;
    std::vector<int> sectionStarts; // per chartSections() section: its first step; -1 = it has none

    // Enough chords to follow.
    [[nodiscard]] bool followable() const { return steps.size() >= 2; }
};

[[nodiscard]] SongMap buildSongMap(const Chart& chart);

} // namespace gigchain::core
```

`src/core/internal/SongMap.cpp`:

```cpp
#include "gigchain/core/SongMap.h"

#include <algorithm>

namespace gigchain::core {

SongMap buildSongMap(const Chart& chart)
{
    const std::vector<ChartSection> sections = chartSections(chart);
    SongMap map;
    map.sectionStarts.assign(sections.size(), -1);

    std::vector<SongStep> played; // every chord as played, before twins are merged
    std::vector<SongStep> part;   // the current section's chords, played once
    int section = -1;
    std::size_t nextSection = 0;
    const auto finishPart = [&] {
        const int times = section >= 0 ? sectionRepeats(sections.at(static_cast<std::size_t>(section))) : 1;
        for (int t = 0; t < times; ++t) played.insert(played.end(), part.begin(), part.end());
        part.clear();
    };
    for (std::size_t i = 0; i < chart.lines.size(); ++i) {
        if (nextSection < sections.size() && sections.at(nextSection).line == static_cast<int>(i)) {
            finishPart();
            section = static_cast<int>(nextSection++);
            continue;
        }
        const ChartLine& line = chart.lines.at(i);
        if (line.kind != ChartLine::Kind::Lyrics) continue;
        std::vector<SongStep> once;
        int chordIndex = 0;
        for (const ChartSegment& segment : line.segments) {
            if (segment.chord.isEmpty()) continue;
            const int at = chordIndex++;
            if (const auto shape = parseChordName(segment.chord)) {
                once.push_back(SongStep{.shape = *shape, .name = segment.chord, .section = section,
                                        .places = {{static_cast<int>(i), at}}});
            }
        }
        for (int t = 0; t < lineRepeats(line); ++t) part.insert(part.end(), once.begin(), once.end());
    }
    finishPart();

    // The same chord twice in a row (in one section) is one step, lit in every place.
    for (SongStep& step : played) {
        if (!map.steps.empty() && map.steps.back().section == step.section && map.steps.back().shape == step.shape) {
            auto& places = map.steps.back().places;
            for (const auto& place : step.places) {
                if (std::ranges::find(places, place) == places.end()) places.push_back(place);
            }
            continue;
        }
        map.steps.push_back(std::move(step));
    }
    for (std::size_t i = map.steps.size(); i-- > 0;) {
        const int s = map.steps.at(i).section;
        if (s >= 0) map.sectionStarts.at(static_cast<std::size_t>(s)) = static_cast<int>(i);
    }
    return map;
}

} // namespace gigchain::core
```

Add to `src/core/CMakeLists.txt`:

```cmake
    include/gigchain/core/SongMap.h
    internal/SongMap.cpp
```

- [ ] **Step 5: Run the tests to see them pass**

Run: `tools\build.ps1 -Target tst_song_map -Filter "tst_song_map|tst_chart"`
Expected: PASS (tst_chart unchanged).

- [ ] **Step 6: Commit**

```bash
git add src/core tests/core/tst_song_map.cpp tests/core/CMakeLists.txt
git commit -m "feat: a chart as the chords a player goes through (song map)"
```

---

### Task 3: The follower (engine::ChordFollower)

**Files:**
- Modify: `src/engine/include/gigchain/engine/EngineTypes.h` (add `ChordFollowStep`, `ChordFollowMap`, `ChordFollowPosition`, `followStepOf`; include `gigchain/core/Chords.h`)
- Modify: `src/engine/internal/RenderGraph.h` (`SectionGate` gets `handover`)
- Create: `src/engine/internal/ChordFollower.h`
- Create: `src/engine/internal/ChordFollower.cpp`
- Modify: `src/engine/CMakeLists.txt`
- Create: `tests/engine/tst_chord_follower.cpp`
- Modify: `tests/engine/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::ChordShape`, `core::parseChordName` (Task 1); `SectionGate` (existing, `RenderGraph.h`).
- Produces:
  - `struct engine::ChordFollowStep { int section; uint16_t family; int root; int bass; int third; int otherThird; int colour; }` (pitch classes, -1 = none).
  - `struct engine::ChordFollowMap { std::vector<ChordFollowStep> steps; std::vector<int> sectionStarts; int resumeAt = -1; }`
  - `struct engine::ChordFollowPosition { bool active; bool started; int step; int section; }`
  - `engine::ChordFollowStep engine::followStepOf(const core::ChordShape& shape, int section)`
  - `SectionGate::handover` (`std::span<const MidiEvent>`, default empty).
  - `class engine::ChordFollower` with `SectionGate process(const ChordFollowMap* map, uint64_t generation, std::span<const MidiEvent> events, int frames, double sampleRate) noexcept`, `std::span<const MidiEvent> handover() const noexcept`, `void jumpToSection(int section) noexcept`, `void reset() noexcept`, `ChordFollowPosition position() const noexcept`.

- [ ] **Step 1: The shared types**

In `src/engine/include/gigchain/engine/EngineTypes.h`, add `#include "gigchain/core/Chords.h"` with the other core includes, and after `struct SongPosition`:

```cpp
// ---- Chord follow: the chart follows what is played

// One chord of the song as it is heard. Pitch classes 0-11 (C = 0).
struct ChordFollowStep
{
    int section = -1;    // in the song's sections (setSongSections); -1 = before the first
    uint16_t family = 0; // its notes: bit n = pitch class n
    int root = 0;
    int bass = -1;       // a slash bass; -1 = none
    int third = -1;      // its third; -1 = none (sus, 5)
    int otherThird = -1; // the third it is not (the major third of a minor chord); -1 = none
    int colour = -1;     // what stands in for a missing third (a sus note, a 5 chord's fifth); -1 = none

    bool operator==(const ChordFollowStep&) const = default;
};

// A song's chords in playing order (see IEngine::setChordFollow).
struct ChordFollowMap
{
    std::vector<ChordFollowStep> steps;
    std::vector<int> sectionStarts; // per section: its first step; -1 = it has none
    int resumeAt = -1;              // a chart edited while following carries on from this step

    bool operator==(const ChordFollowMap&) const = default;
};

// Where following is.
struct ChordFollowPosition
{
    bool active = false;  // a map is being followed
    bool started = false; // its first chord was heard (or a section chosen)
    int step = -1;        // the chord being played; -1 = not started
    int section = -1;     // in force: the first chord's section before the start

    bool operator==(const ChordFollowPosition&) const = default;
};

// The step a chord name makes in `section`.
[[nodiscard]] inline ChordFollowStep followStepOf(const core::ChordShape& shape, int section)
{
    const auto at = [&shape](int interval) { return interval < 0 ? -1 : (shape.root + interval) % 12; };
    const int other = shape.third == 3 ? 4 : shape.third == 4 ? 3 : -1;
    return ChordFollowStep{.section = section,
                           .family = shape.family,
                           .root = shape.root,
                           .bass = shape.bass,
                           .third = at(shape.third),
                           .otherThird = at(other),
                           .colour = at(shape.colour)};
}
```

In `src/engine/internal/RenderGraph.h`, `SectionGate` becomes:

```cpp
struct SectionGate
{
    int before = -1;
    int after = -1;
    int switchAt = 0;
    // Chord follow entering a section: keys already held go to the strips
    // coming in (note-ons), and the chord's first keys leave the strips going
    // out (note-offs), all at switchAt. Owned by the ChordFollower.
    std::span<const MidiEvent> handover = {};
};
```

- [ ] **Step 2: Write the failing test**

`tests/engine/tst_chord_follower.cpp` (1 sample = 1 ms: the rate is 1000):

```cpp
// The five rules of chord follow, played as note sequences (no audio).
#include "ChordFollower.h"

#include "gigchain/core/Chords.h"

#include <QtTest>

#include <initializer_list>
#include <vector>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// Keys: middle C is 60.
constexpr int E2 = 40, A2 = 45, C3 = 48, D3 = 50, E3 = 52, F3 = 53, G3 = 55, Gs3 = 56, A3 = 57, B3 = 59;
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
    return map;
}

// A player at the keyboard: each call is one 10 ms block.
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
        for (const int key : keys) events.push_back(MidiEvent{.status = 0x90, .data1 = static_cast<uint8_t>(key), .data2 = 100, .sampleOffset = offset});
        block();
    }
    void release(std::initializer_list<int> keys)
    {
        for (const int key : keys) events.push_back(MidiEvent{.status = 0x80, .data1 = static_cast<uint8_t>(key), .data2 = 0, .sampleOffset = 0});
        block();
    }
    void pedal(bool down)
    {
        events.push_back(MidiEvent{.status = 0xB0, .data1 = 64, .data2 = static_cast<uint8_t>(down ? 127 : 0), .sampleOffset = 0});
        block();
    }
    void wait(int ms) { block(ms); }
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
        p.press({C4, G4}); // not Am
        QVERIFY(!p.started());
        p.press({A3});      // A + C: the root and one more
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
        p.press({C4, E4});
        QCOMPARE(p.step(), 0);
        p.release({C4, E4});
        p.wait(600); // C is forgotten
        for (const int key : keys) p.press({key});
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
        p.press({C3, E3});
        QCOMPARE(p.step(), 0);
        p.release({C3, E3});
        p.wait(600);
        for (const int key : keys) p.press({key});
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
        p.press({C4, E4, G4}); // G is in C: but G's root alone is not "the root and one more"
        for (int i = 0; i < 50; ++i) p.wait(20);
        QCOMPARE(p.step(), 0);
    }

    void aSlashChordNeedsItsBass()
    {
        Player p(mapOf({{"C", 0}, {"C/E", 0}}));
        p.press({C4, E4, G4});
        QCOMPARE(p.step(), 0);
        p.release({C4, E4, G4});
        p.wait(600);
        p.press({C3, E4, G4}); // C at the bottom: still C
        QCOMPARE(p.step(), 0);
        p.release({C3, E4, G4});
        p.wait(600);
        p.press({E2, C4, G4}); // E at the bottom
        QCOMPARE(p.step(), 1);
    }

    void oneStrayChordNeverJumps()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 0}, {"G", 0}, {"D", 1}, {"Bm", 1}}));
        p.press({A3, C4, E4});
        p.release({A3, C4, E4});
        p.wait(600);
        p.press({D4, Fs4, A4}); // the chorus's first chord, alone
        QCOMPARE(p.step(), 0);
        QCOMPARE(p.section(), 0);
        p.release({D4, Fs4, A4});
        p.wait(600);
        p.press({G3, B3, D4}); // then something else
        QCOMPARE(p.step(), 0);
    }

    void twoChordsOfASectionJumpThere()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"C", 0}, {"G", 0}, {"D", 1}, {"Bm", 1}}));
        p.press({A3, C4, E4});
        p.release({A3, C4, E4});
        p.wait(600);
        p.press({D4, Fs4, A4});
        p.release({D4, Fs4, A4});
        p.wait(600);
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
        const auto playChord = [&p](std::initializer_list<int> keys) {
            for (const int key : keys) p.press({key});
            p.events.clear();
            for (const int key : keys) p.events.push_back(MidiEvent{.status = 0x80, .data1 = static_cast<uint8_t>(key), .data2 = 0, .sampleOffset = 0});
            p.block();
            p.wait(600);
        };
        // From the chorus, a verse's opening goes to Verse 2 (the next one)...
        p.follower.jumpToSection(1);
        p.block();
        playChord({C4, E4, G4});
        playChord({G3, B3, D4});
        QCOMPARE(p.section(), 2);
        QCOMPARE(p.step(), 5);
        // ... and from the bridge, the song's top comes round again: Verse 1.
        p.follower.jumpToSection(3);
        p.block();
        playChord({C4, E4, G4});
        playChord({G3, B3, D4});
        QCOMPARE(p.section(), 0);
        QCOMPARE(p.step(), 1);
    }

    // G-B-D holds Bm's root and third (B, D) but is G: it never completes a
    // jump to a section starting D, Bm.
    void aChordSharingTwoNotesIsNotClear()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"D", 1}, {"Bm", 1}}));
        p.press({A3, C4, E4});
        p.release({A3, C4, E4});
        p.wait(600);
        p.press({D4, Fs4, A4}); // D: the chorus's first chord, remembered
        p.release({D4, Fs4, A4});
        p.wait(600);
        p.press({B3, D4, G4}); // G in first inversion: B, D and G
        QCOMPARE(p.section(), 0);
    }

    void aSectionChosenByHand()
    {
        Player p(mapOf({{"Am", 0}, {"F", 0}, {"D", 1}, {"Bm", 1}}));
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
        p.press({A3, C4, E4});
        p.release({A3, C4, E4});
        p.wait(600);
        p.press({G3, B3, D4});
        p.release({G3, B3, D4});
        p.wait(600);
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
        QVERIFY(std::ranges::none_of(p.handover, [](const MidiEvent& e) { return e.status == 0x90 && e.data1 != F3 && e.data1 != A4; }));
    }

    void withoutSectionsItStillFollows()
    {
        Player p(mapOf({{"C", -1}, {"G", -1}}));
        p.press({C4, E4});
        p.release({C4, E4});
        p.wait(600);
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
        p.events.push_back(MidiEvent{.status = 0xB0, .data1 = 123, .data2 = 0, .sampleOffset = 0});
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

    void noMapIsNotFollowing()
    {
        ChordFollower follower;
        const std::array<MidiEvent, 1> events{MidiEvent{.status = 0x90, .data1 = 60, .data2 = 100, .sampleOffset = 0}};
        const SectionGate gate = follower.process(nullptr, 0, events, 10, 1000.0);
        QVERIFY(!follower.position().active);
        QCOMPARE(gate.before, -1);
    }
};

QTEST_GUILESS_MAIN(TestChordFollower)
#include "tst_chord_follower.moc"
```

In `tests/engine/CMakeLists.txt`:

```cmake
gigchain_add_test(tst_chord_follower SOURCES tst_chord_follower.cpp LIBS gigchain::engine)
target_include_directories(tst_chord_follower PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/internal)
```

Note on `enteringASectionHandsTheChordOver`: C3 then F3. F's family is F A C, so C3 already held plus F3 (the root) is "the root plus one more" and enters the chorus on F3's key-down. C3 was pressed before the switch and since the last chord (G): handed over (on to the chorus, off from the verse). If a test's expectation differs from the implementation, the spec's rules decide which is wrong.

The includes the test also needs: `<algorithm>`, `<array>`, `<stdexcept>`.

- [ ] **Step 3: Run it to see it fail**

Run: `tools\build.ps1 -Target tst_chord_follower -Filter tst_chord_follower`
Expected: build error, `ChordFollower.h` not found.

- [ ] **Step 4: The follower**

`src/engine/internal/ChordFollower.h`:

```cpp
#pragma once

#include "MidiEvent.h"
#include "RenderGraph.h"

#include "gigchain/engine/EngineTypes.h"

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <span>

namespace gigchain::engine {

// Follows a song's chords as they are played, by the five rules of
// docs/superpowers/specs/2026-09-29-chord-follow-design.md: which chord of
// the song is being played, and the section gate that switches instruments
// on the note that entered a section.
//
// process() runs on the audio thread (no allocation, no locks);
// jumpToSection(), reset() and position() on any thread.
class ChordFollower
{
public:
    // Audio thread, each block. `map` null: not following. A map with another
    // `generation` than the last starts fresh: at its resumeAt, else waiting
    // for its first chord. The gate: the section in force at the block's
    // start (`before`) and from `switchAt` on (`after`).
    SectionGate process(const ChordFollowMap* map, uint64_t generation, std::span<const MidiEvent> events, int frames,
                        double sampleRate) noexcept;
    // The notes moved at this block's switch (see SectionGate::handover).
    [[nodiscard]] std::span<const MidiEvent> handover() const noexcept { return {m_handover.data(), m_handoverCount}; }

    // Any thread.
    void jumpToSection(int section) noexcept { m_jumpAsked.store(section, std::memory_order_release); }
    void reset() noexcept { m_resetAsked.store(true, std::memory_order_release); }
    [[nodiscard]] ChordFollowPosition position() const noexcept;

    // How long a key let go still counts (broken chords, arpeggios).
    static constexpr double kMemorySeconds = 0.5;

private:
    void clear() noexcept;
    [[nodiscard]] int sectionInForce(const ChordFollowMap& map) const noexcept;
    // `key` went down at `now`: whether the chart moved (rules 1 to 3).
    bool hear(const ChordFollowMap& map, int key, int64_t now, int64_t memory) noexcept;
    void fillHandover(int64_t switchTime, int offset) noexcept;
    void publish(const ChordFollowMap* map) noexcept;

    std::array<uint8_t, 128> m_velocity{};   // down: its velocity; 0 = up
    std::array<uint8_t, 128> m_status{};     // the note-on's status (its MIDI channel)
    std::array<int64_t, 128> m_pressedAt{};  // when it went down (samples)
    std::array<int64_t, 128> m_releasedAt{}; // when it was let go; -1 = not recently
    std::array<bool, 128> m_sustained{};     // let go while the pedal was down
    std::bitset<128> m_sinceChord;           // pressed since the last chord heard
    bool m_pedal = false;
    int64_t m_now = 0;
    int m_step = -1;      // the chord being played; -1 = not started
    int m_candidate = -1; // a section whose first chord was just heard clearly (rule 3)
    uint64_t m_generation = 0;
    std::array<MidiEvent, kMaxEventsPerBlock> m_handover{};
    std::size_t m_handoverCount = 0;

    std::atomic<int> m_jumpAsked{-1};
    std::atomic<bool> m_resetAsked{false};
    std::atomic<bool> m_outActive{false};
    std::atomic<bool> m_outStarted{false};
    std::atomic<int> m_outStep{-1};
    std::atomic<int> m_outSection{-1};
};

} // namespace gigchain::engine
```

`src/engine/internal/ChordFollower.cpp`:

```cpp
#include "ChordFollower.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace gigchain::engine {
namespace {

constexpr uint16_t pitchBit(int pitchClass)
{
    return static_cast<uint16_t>(1U << pitchClass);
}

constexpr bool has(uint16_t notes, int pitchClass)
{
    return pitchClass >= 0 && (notes & pitchBit(pitchClass)) != 0;
}

// Rule 2, generous: the step's root plus any one other note of it, and a
// slash chord's bass at the bottom.
bool heardLoosely(const ChordFollowStep& step, uint16_t notes, int lowest)
{
    if (!has(notes, step.root)) return false;
    if ((notes & step.family & static_cast<uint16_t>(~pitchBit(step.root))) == 0) return false;
    return step.bass < 0 || lowest == step.bass;
}

// Rule 3, strict: the root and its third (a sus chord's sus note, a 5
// chord's fifth), not the other third, and at least three of its notes
// (both of a two-note chord): G-B-D holds Bm's B and D but is not Bm.
bool heardClearly(const ChordFollowStep& step, uint16_t notes)
{
    if (!has(notes, step.root)) return false;
    const bool third = step.third >= 0 ? has(notes, step.third) && !has(notes, step.otherThird) : has(notes, step.colour);
    const int needed = std::min(3, std::popcount(step.family));
    return third && std::popcount(static_cast<uint16_t>(notes & step.family)) >= needed;
}

bool isNoteOn(const MidiEvent& e)
{
    return (e.status & 0xF0) == 0x90 && e.data2 > 0;
}

bool isNoteOff(const MidiEvent& e)
{
    return (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0);
}

} // namespace

void ChordFollower::clear() noexcept
{
    m_velocity.fill(0);
    m_releasedAt.fill(-1);
    m_sustained.fill(false);
    m_sinceChord.reset();
    m_pedal = false;
    m_step = -1;
    m_candidate = -1;
}

int ChordFollower::sectionInForce(const ChordFollowMap& map) const noexcept
{
    const auto step = static_cast<std::size_t>(std::max(m_step, 0));
    return step < map.steps.size() ? map.steps.at(step).section : -1;
}

SectionGate ChordFollower::process(const ChordFollowMap* map, uint64_t generation, std::span<const MidiEvent> events,
                                   int frames, double sampleRate) noexcept
{
    m_handoverCount = 0;
    if (map == nullptr || map->steps.empty()) {
        m_generation = 0;
        m_now += frames;
        publish(nullptr);
        return {};
    }
    if (generation != m_generation) {
        clear();
        m_generation = generation;
        if (map->resumeAt >= 0 && std::cmp_less(map->resumeAt, map->steps.size())) m_step = map->resumeAt;
    }
    if (m_resetAsked.exchange(false, std::memory_order_acq_rel)) {
        clear();
    }
    SectionGate gate;
    gate.before = sectionInForce(*map);
    gate.after = gate.before;
    // A section chosen by hand (the pedal, a click): from the block's start.
    if (const int asked = m_jumpAsked.exchange(-1, std::memory_order_acq_rel);
        asked >= 0 && std::cmp_less(asked, map->sectionStarts.size()) && map->sectionStarts.at(static_cast<std::size_t>(asked)) >= 0) {
        m_step = map->sectionStarts.at(static_cast<std::size_t>(asked));
        m_candidate = -1;
        m_sinceChord.reset();
        gate.before = sectionInForce(*map);
        gate.after = gate.before;
    }
    const auto memory = static_cast<int64_t>(kMemorySeconds * sampleRate);
    for (const MidiEvent& e : events) {
        const auto key = static_cast<std::size_t>(e.data1 & 0x7F);
        const int64_t at = m_now + e.sampleOffset;
        if (isNoteOn(e)) {
            m_velocity.at(key) = e.data2;
            m_status.at(key) = e.status;
            m_pressedAt.at(key) = at;
            m_releasedAt.at(key) = -1;
            m_sustained.at(key) = false;
            m_sinceChord.set(key);
            if (hear(*map, static_cast<int>(key), at, memory)) {
                const int section = sectionInForce(*map);
                if (section != gate.after) {
                    gate.after = section;
                    gate.switchAt = e.sampleOffset;
                    fillHandover(at, e.sampleOffset);
                }
                m_sinceChord.reset();
            }
        } else if (isNoteOff(e)) {
            if (m_velocity.at(key) != 0) {
                m_velocity.at(key) = 0;
                m_releasedAt.at(key) = at;
                m_sustained.at(key) = m_pedal;
            }
        } else if ((e.status & 0xF0) == 0xB0 && e.data1 == 64) {
            m_pedal = e.data2 >= 64;
            if (!m_pedal) m_sustained.fill(false);
        } else if ((e.status & 0xF0) == 0xB0 && (e.data1 == 120 || e.data1 == 123)) {
            // All sound / all notes off: nothing is held any more.
            m_velocity.fill(0);
            m_sustained.fill(false);
            m_releasedAt.fill(-1);
            m_sinceChord.reset();
        }
    }
    m_now += frames;
    publish(map);
    return gate;
}

bool ChordFollower::hear(const ChordFollowMap& map, int key, int64_t now, int64_t memory) noexcept
{
    const auto count = static_cast<int>(map.steps.size());
    // A remembered section opening is forgotten when a key is played that
    // belongs to neither of its first two chords: the player went elsewhere.
    if (m_candidate >= 0) {
        const int first = map.sectionStarts.at(static_cast<std::size_t>(m_candidate));
        uint16_t both = map.steps.at(static_cast<std::size_t>(first)).family;
        if (first + 1 < count) both |= map.steps.at(static_cast<std::size_t>(first + 1)).family;
        if (!has(both, key % 12)) m_candidate = -1;
    }
    // What is played: keys down, kept by the pedal, or let go a moment ago.
    uint16_t notes = 0;
    int lowest = -1;
    for (std::size_t key = 0; key < m_velocity.size(); ++key) {
        const bool held = m_velocity.at(key) != 0 || m_sustained.at(key) ||
                          (m_releasedAt.at(key) >= 0 && now - m_releasedAt.at(key) <= memory);
        if (!held) continue;
        notes |= pitchBit(static_cast<int>(key % 12));
        if (lowest < 0) lowest = static_cast<int>(key % 12);
    }
    // Rules 1 and 2: the next chord (the first when not started).
    const int next = m_step + 1;
    if (next < count && heardLoosely(map.steps.at(static_cast<std::size_t>(next)), notes, lowest)) {
        m_step = next;
        m_candidate = -1;
        return true;
    }
    // Rule 3: a section's first two chords, clearly.
    const auto sections = static_cast<int>(map.sectionStarts.size());
    if (m_candidate >= 0) {
        const int second = map.sectionStarts.at(static_cast<std::size_t>(m_candidate)) + 1;
        if (second < count && second != next && heardClearly(map.steps.at(static_cast<std::size_t>(second)), notes)) {
            m_step = second;
            m_candidate = -1;
            return true;
        }
    }
    // A section's first chord, clearly: remembered (the sections after the
    // current one first, then from the top). Nothing clear: it stays as it was.
    const int current = m_step >= 0 ? map.steps.at(static_cast<std::size_t>(m_step)).section : -1;
    for (int i = 0; i < sections; ++i) {
        const int s = (current + 1 + i) % sections;
        const int first = map.sectionStarts.at(static_cast<std::size_t>(s));
        if (first < 0 || first == next) continue;
        if (heardClearly(map.steps.at(static_cast<std::size_t>(first)), notes)) {
            m_candidate = s;
            break;
        }
    }
    return false;
}

void ChordFollower::fillHandover(int64_t switchTime, int offset) noexcept
{
    m_handoverCount = 0;
    for (std::size_t key = 0; key < m_velocity.size() && m_handoverCount + 2 <= m_handover.size(); ++key) {
        // Only keys down (one let go under the pedal would never get its
        // note-off there), pressed before the switch (those at it already
        // reach the strips coming in).
        if (m_velocity.at(key) == 0 || m_pressedAt.at(key) >= switchTime) continue;
        const auto channel = static_cast<uint8_t>(m_status.at(key) & 0x0F);
        const auto note = static_cast<uint8_t>(key);
        m_handover.at(m_handoverCount++) =
            MidiEvent{.status = static_cast<uint8_t>(0x90 | channel), .data1 = note, .data2 = m_velocity.at(key), .sampleOffset = offset};
        if (m_sinceChord.test(key)) {
            m_handover.at(m_handoverCount++) =
                MidiEvent{.status = static_cast<uint8_t>(0x80 | channel), .data1 = note, .data2 = 0, .sampleOffset = offset};
        }
    }
}

void ChordFollower::publish(const ChordFollowMap* map) noexcept
{
    const bool active = map != nullptr;
    m_outActive.store(active, std::memory_order_relaxed);
    m_outStarted.store(active && m_step >= 0, std::memory_order_relaxed);
    m_outStep.store(active ? m_step : -1, std::memory_order_relaxed);
    m_outSection.store(active ? sectionInForce(*map) : -1, std::memory_order_relaxed);
}

ChordFollowPosition ChordFollower::position() const noexcept
{
    return ChordFollowPosition{.active = m_outActive.load(std::memory_order_relaxed),
                               .started = m_outStarted.load(std::memory_order_relaxed),
                               .step = m_outStep.load(std::memory_order_relaxed),
                               .section = m_outSection.load(std::memory_order_relaxed)};
}

} // namespace gigchain::engine
```

Add to `src/engine/CMakeLists.txt` sources: `internal/ChordFollower.h` and `internal/ChordFollower.cpp`.

- [ ] **Step 5: Run the tests to see them pass**

Run: `tools\build.ps1 -Target tst_chord_follower -Filter "tst_chord_follower|tst_render_graph"`
Expected: PASS. A failing row names the rule it breaks: fix the follower (not the test) unless the test contradicts the spec's five rules.

- [ ] **Step 6: Commit**

```bash
git add src/engine tests/engine/tst_chord_follower.cpp tests/engine/CMakeLists.txt
git commit -m "feat: the chord follower (the five rules, on the audio thread)"
```

---

### Task 4: Handing the chord over at a section switch (ChannelStrip)

**Files:**
- Modify: `src/engine/internal/RenderGraph.cpp:155-196` (`ChannelStrip::render`)
- Test: `tests/engine/tst_render_graph.cpp`

**Interfaces:**
- Consumes: `SectionGate::handover` (Task 3).
- Produces: strips deliver handover note-ons only when they play the section after and not the one before, and handover note-offs only when they play the one before and not the one after; merged in sample order.

- [ ] **Step 1: Write the failing test**

In `tests/engine/tst_render_graph.cpp`, after `aSectionGateSendsNewNotesToItsStrips`:

```cpp
    // Chord follow entering the chorus: the chord's keys pressed a moment
    // before (they reached the verse) move to the chorus at the switch.
    void aHandoverMovesTheHeldChord()
    {
        auto verse = std::make_shared<HeldNoteNode>(0.25F);
        auto chorus = std::make_shared<HeldNoteNode>(0.5F);
        auto both = std::make_shared<HeldNoteNode>(0.125F);
        for (auto* node : {verse.get(), chorus.get(), both.get()}) node->received.reserve(16);
        std::vector<StripSpec> specs;
        specs.push_back(strip(verse));
        specs.push_back(strip(chorus));
        specs.push_back(strip(both));
        RenderGraph graph(std::move(specs), 48000.0, kFrames);
        graph.strip(0)->setSections(0b01);
        graph.strip(1)->setSections(0b10);
        graph.strip(2)->setSections(0b11); // plays in both: nothing to move

        MidiEvent trigger = noteOn(53); // the key that completed the chord, at the switch
        trigger.sampleOffset = 20;
        MidiEvent handOn = noteOn(48);
        handOn.sampleOffset = 20;
        MidiEvent handOff = cc(0x80, 48, 0);
        handOff.sampleOffset = 20;
        const std::array events{trigger};
        const std::array handover{handOn, handOff};
        Output out;
        graph.render(events, out.block(), 1.0F, {}, {},
                     SectionGate{.before = 0, .after = 1, .switchAt = 20, .handover = handover});

        // The verse lets go of 48 and gets nothing new.
        QCOMPARE(verse->received.size(), std::size_t{1});
        QCOMPARE(verse->received.at(0).status, uint8_t{0x80});
        QCOMPARE(verse->received.at(0).data1, uint8_t{48});
        // The chorus gets 48 (handed over) and 53 (the trigger).
        QCOMPARE(chorus->received.size(), std::size_t{2});
        QVERIFY(std::ranges::all_of(chorus->received, [](const MidiEvent& e) { return (e.status & 0xF0) == 0x90; }));
        // A strip in both sections already had 48: only the trigger.
        QCOMPARE(both->received.size(), std::size_t{1});
        QCOMPARE(both->received.at(0).data1, uint8_t{53});
    }
```

- [ ] **Step 2: Run it to see it fail**

Run: `tools\build.ps1 -Target tst_render_graph -Filter tst_render_graph`
Expected: FAIL in `aHandoverMovesTheHeldChord` (the verse gets no note-off; the chorus gets only the trigger).

- [ ] **Step 3: Implement**

In `ChannelStrip::render` (`src/engine/internal/RenderGraph.cpp`), replace the `plays` lambda with a section test and use it:

```cpp
    const uint64_t mask = m_sections.load(std::memory_order_relaxed);
    // Whether this strip plays in `section` (none, or out of range: every strip does).
    const auto inSection = [mask](int section) {
        return section < 0 || section >= 64 || ((mask >> section) & 1U) != 0;
    };
    // Whether a new note at `offset` is for this strip: its section is in force.
    const auto plays = [&gate, &inSection](int offset) { return inSection(offset < gate.switchAt ? gate.before : gate.after); };
```

and, after the event loop and before `produce(...)`:

```cpp
    // Chord follow entering a section: the chord's keys pressed a moment
    // before move over (held keys on to the strips coming in, the chord's
    // first keys off the strips going out), in time order with the rest.
    if (!gate.handover.empty() && gate.before != gate.after) {
        const bool wasIn = inSection(gate.before);
        const bool isIn = inSection(gate.after);
        for (const MidiEvent& event : gate.handover) {
            const bool on = (event.status & 0xF0) == 0x90 && event.data2 > 0;
            if (on ? (!isIn || wasIn) : (!wasIn || isIn)) continue;
            if (routedCount == m_routed.size()) break;
            if (const auto routed = routeEvent(event, m_route)) m_routed.at(routedCount++) = *routed; // room checked above
        }
        const auto first = m_routed.begin();
        std::sort(first, first + static_cast<std::ptrdiff_t>(routedCount),
                  [](const MidiEvent& a, const MidiEvent& b) { return a.sampleOffset < b.sampleOffset; });
    }
```

(`std::sort` does not allocate; the routed array is the strip's fixed buffer.)

- [ ] **Step 4: Run the tests to see them pass**

Run: `tools\build.ps1 -Target tst_render_graph -Filter tst_render_graph`
Expected: PASS, including `render never allocates` (the existing allocation test).

- [ ] **Step 5: Commit**

```bash
git add src/engine/internal/RenderGraph.cpp tests/engine/tst_render_graph.cpp
git commit -m "feat: a section switch hands the held chord to the new instruments"
```

---

### Task 5: The engine follows (IEngine::setChordFollow, RealEngine, fakes)

**Files:**
- Modify: `src/engine/include/gigchain/engine/IEngine.h` (after `songPosition()`)
- Modify: `src/core/include/gigchain/core/Limits.h` (`kMaxFollowSteps`)
- Modify: `src/engine/internal/RealEngine.h`, `src/engine/internal/RealEngine.cpp`
- Modify: `src/engine/internal/FakeEngine.h`
- Modify: `tests/common/SpyEngine.h`
- Test: `tests/engine/tst_real_engine.cpp`

**Interfaces:**
- Consumes: `ChordFollower`, `ChordFollowMap`, `ChordFollowPosition` (Task 3); handover strips (Task 4).
- Produces:
  - `virtual void IEngine::setChordFollow(const ChordFollowMap& map) = 0;` (fewer than 2 steps = not following)
  - `[[nodiscard]] virtual ChordFollowPosition IEngine::chordFollow() const = 0;`
  - While following, `songPosition()` reports `{.playing = started, .section = the follower's}`; `jumpToSection()` moves the follower too; `panic()` resets it.
  - SpyEngine: `engine::ChordFollowMap follow; int followSets = 0; engine::ChordFollowPosition followPosition;`

- [ ] **Step 1: Write the failing test**

In `tests/engine/tst_real_engine.cpp`, after `songSectionsSendNotesToTheirChannels`:

```cpp
    // Chord follow: playing the chorus's chord enters the chorus, and its
    // piano sounds the chord; the verse's piano gets nothing new.
    void playingTheChorusChordEntersTheChorus()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto created = createQuietEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY(created.has_value());
        IEngine& engine = **created;
        engine.setMasterVolume(-90.0); // inaudible, measurable
        const core::SongId song = core::SongId::generate();
        core::Patch patch = pianoPatch();
        patch.channels.push_back(pianoPatch().channels.front());
        const core::ChannelId verse = patch.channels.at(0).id;
        const core::ChannelId chorus = patch.channels.at(1).id;
        engine.applyPatch(song, patch);
        QVERIFY(engine.poll().empty());
        engine.setSongSections(SongSections{.patch = patch.id,
                                            .sections = {{.bars = 4, .live = {verse}}, {.bars = 4, .live = {chorus}}},
                                            .switchEarly = false});
        ChordFollowMap map;
        for (const auto& [name, section] : {std::pair{"Am", 0}, {"G", 0}, {"F", 1}, {"C", 1}}) {
            map.steps.push_back(followStepOf(*core::parseChordName(QString::fromLatin1(name)), section));
        }
        map.sectionStarts = {0, 2};
        engine.setChordFollow(map);
        pump(engine, 50);
        QVERIFY(engine.chordFollow().active);
        QVERIFY(!engine.chordFollow().started);
        const auto chord = [&engine](std::initializer_list<int> keys, int velocity) {
            for (const int key : keys) engine.injectNote(1, key, velocity);
            pump(engine, 100);
        };
        chord({57, 60, 64}, 100); // Am
        QCOMPARE(engine.chordFollow().step, 0);
        chord({57, 60, 64}, 0);
        pump(engine, 600);
        chord({55, 59, 62}, 100); // G
        QCOMPARE(engine.chordFollow().step, 1);
        chord({55, 59, 62}, 0);
        pump(engine, 3000); // the verse's piano fades away
        (void)engine.channelLevel(verse);
        (void)engine.channelLevel(chorus);

        chord({53, 57, 60}, 100); // F: the chorus
        pump(engine, 300);
        QCOMPARE(engine.chordFollow().step, 2);
        QCOMPARE(engine.songPosition().section, 1);
        QVERIFY2(engine.channelLevel(chorus).peak > 0.0F, "the chorus's piano did not sound the chorus's chord");
        QCOMPARE(engine.channelLevel(verse).peak, 0.0F);
        chord({53, 57, 60}, 0);

        // The pedal's "next section" and Panic.
        engine.jumpToSection(0);
        pump(engine, 50);
        QCOMPARE(engine.chordFollow().step, 0);
        engine.panic();
        pump(engine, 50);
        QVERIFY(!engine.chordFollow().started);
        engine.setChordFollow({}); // off: the tempo leads again
        pump(engine, 50);
        QVERIFY(!engine.chordFollow().active);
    }
```

Add `#include "gigchain/core/Chords.h"` to the test's includes.

- [ ] **Step 2: Run it to see it fail**

Run: `tools\build.ps1 -Target tst_real_engine -Filter tst_real_engine`
Expected: build error, `setChordFollow` is not a member of `IEngine`.

- [ ] **Step 3: The interface and the fakes**

`src/engine/include/gigchain/engine/IEngine.h`, after `songPosition()`:

```cpp
    // ---- Chord follow (the chart follows what is played)
    // The current song's chords in playing order, their sections those of
    // setSongSections in the same order. While a map is set, what is played
    // decides the section in force (see ChordFollowMap); fewer than two
    // steps = not following (the song's sections follow the tempo).
    virtual void setChordFollow(const ChordFollowMap& map) = 0;
    [[nodiscard]] virtual ChordFollowPosition chordFollow() const = 0;
```

`src/core/include/gigchain/core/Limits.h`, after `kMaxSectionOccurrence`:

```cpp
inline constexpr int kMaxFollowSteps = 4096; // the chords of a song, repeats played out
```

`src/engine/internal/FakeEngine.h`, after `songPosition()`:

```cpp
    void setChordFollow(const ChordFollowMap& map) override
    {
        const bool active = map.steps.size() >= 2;
        m_follow = ChordFollowPosition{.active = active, .started = false, .step = -1,
                                       .section = active ? map.steps.front().section : -1};
    }
    [[nodiscard]] ChordFollowPosition chordFollow() const override { return m_follow; }
```

and the member `ChordFollowPosition m_follow;` next to `m_position`.

`tests/common/SpyEngine.h`, after `songPosition()`:

```cpp
    void setChordFollow(const engine::ChordFollowMap& chosen) override
    {
        follow = chosen;
        ++followSets;
    }
    [[nodiscard]] engine::ChordFollowPosition chordFollow() const override { return followPosition; }
```

and the members, next to `sections`:

```cpp
    engine::ChordFollowMap follow;
    int followSets = 0;
    engine::ChordFollowPosition followPosition;
```

- [ ] **Step 4: RealEngine**

`src/engine/internal/RealEngine.h`: `#include "ChordFollower.h"`; declare the overrides (next to `songPosition`):

```cpp
    void setChordFollow(const ChordFollowMap& map) override;
    [[nodiscard]] ChordFollowPosition chordFollow() const override { return m_follower.position(); }
```

replace the inline `songPosition()` with a declaration:

```cpp
    [[nodiscard]] SongPosition songPosition() const override;
```

and add the members after `m_timeline`:

```cpp
    // Chord follow: the map the audio thread follows (a new generation starts
    // it fresh), and the follower. m_following: a map is set (main thread).
    struct FollowSnapshot
    {
        ChordFollowMap map;
        uint64_t generation = 0;
    };
    HazardExchange<FollowSnapshot> m_follow;
    uint64_t m_followGeneration = 0;
    bool m_following = false;
    ChordFollower m_follower;
```

`src/engine/internal/RealEngine.cpp`, after `jumpToSection`:

```cpp
void RealEngine::setChordFollow(const ChordFollowMap& map)
{
    GC_ONLY_MAIN_THREAD();
    if (map.steps.size() > static_cast<std::size_t>(core::limits::kMaxFollowSteps)) {
        qCWarning(lcEngine) << "Chord follow ignored:" << map.steps.size() << "chords, at most" << core::limits::kMaxFollowSteps;
        return;
    }
    if (map.steps.size() < 2) {
        m_follow.publish(nullptr);
        m_following = false;
        return;
    }
    m_follow.publish(std::make_shared<FollowSnapshot>(FollowSnapshot{.map = map, .generation = ++m_followGeneration}));
    m_following = true;
}

SongPosition RealEngine::songPosition() const
{
    // Following chords: where the playing is, not the bar counter.
    if (m_following) {
        const ChordFollowPosition follow = m_follower.position();
        if (follow.active) return SongPosition{.playing = follow.started, .countingIn = false, .section = follow.section, .bar = 0, .bars = 0};
    }
    return m_transport.position();
}
```

In `RealEngine::jumpToSection`, after `m_transport.jump(section);`:

```cpp
    m_follower.jumpToSection(section); // (ignored when not following)
```

In `RealEngine::panic`, after `m_keyboard.clear();`:

```cpp
    m_follower.reset(); // following waits for the song's first chord again
```

In `RealEngine::render`, replace the graph call's gate: after `m_timeline.release();` and before building `time`:

```cpp
    // Chord follow: where the song is, from what is played; its gate when the
    // song's sections are this patch's (a map without sections only lights
    // the chart).
    SectionGate gate = song.gate;
    {
        const FollowSnapshot* follow = m_follow.acquire();
        const SectionGate followed = m_follower.process(follow != nullptr ? &follow->map : nullptr,
                                                        follow != nullptr ? follow->generation : 0,
                                                        std::span<const MidiEvent>(m_events.data(), count), out.frames, rate);
        const bool sections = song.gate.before >= 0 || song.gate.after >= 0;
        if (follow != nullptr && sections) {
            gate = followed;
            gate.handover = m_follower.handover();
        }
        m_follow.release();
    }
```

and pass `gate` instead of `song.gate` to `graph->render(...)`. Where the destructor and `poll()` call `m_exchange.collectGarbage()`, add `m_follow.collectGarbage();` beside it (and in the destructor `m_follow.publish(nullptr);` before it).

- [ ] **Step 5: Run the tests to see them pass**

Run: `tools\build.ps1 -Target tst_real_engine -Filter "tst_real_engine|tst_fake_engine|tst_chord_follower"`
Expected: PASS (the new test skips without Piano V2 or an audio device; on this machine it runs).

- [ ] **Step 6: Commit**

```bash
git add src tests
git commit -m "feat: the engine follows the song's chords (sections switch on the note)"
```

---

### Task 6: Per song, chords or tempo (model, file, editing)

**Files:**
- Modify: `src/core/include/gigchain/core/Model.h` (`Song::followChords`)
- Modify: `src/core/internal/SetlistJson.cpp` (read near `switchEarly` at line ~319; write at line ~433)
- Modify: `src/core/include/gigchain/core/Editing.h`, `src/core/internal/Editing.cpp`
- Test: `tests/core/tst_setlist_json.cpp`, `tests/core/tst_editing.cpp`

**Interfaces:**
- Produces: `bool core::Song::followChords = true;`, `core::Result<void> core::setSongFollowChords(Setlist& setlist, int songIndex, bool follow)`; JSON key `"followChords"` (absent = true).

- [ ] **Step 1: Write the failing tests**

`tests/core/tst_setlist_json.cpp`, a new slot:

```cpp
    void aSongFollowsItsChordsUnlessToldOtherwise()
    {
        Setlist setlist;
        setlist.songs.push_back(makeSong(u"By tempo"_s));
        setlist.songs.front().followChords = false;
        const auto read = fromJson(toJson(setlist));
        QVERIFY2(read.has_value(), read ? "" : qPrintable(read.error().message));
        QVERIFY(!read->songs.front().followChords);
        // Files from before chord follow: on.
        QJsonObject root = QJsonDocument::fromJson(toJson(setlist)).object();
        QJsonArray songs = root.value(u"songs"_s).toArray();
        QJsonObject song = songs.at(0).toObject();
        song.remove(u"followChords"_s);
        songs.replace(0, song);
        root.insert(u"songs"_s, songs);
        const auto older = fromJson(QJsonDocument(root).toJson());
        QVERIFY2(older.has_value(), older ? "" : qPrintable(older.error().message));
        QVERIFY(older->songs.front().followChords);
    }
```

`tests/core/tst_editing.cpp`, a new slot:

```cpp
    void followingChordsIsSetPerSong()
    {
        Setlist s = twoSongs();
        QVERIFY(s.songs.at(0).followChords);
        QVERIFY(setSongFollowChords(s, 0, false).has_value());
        QVERIFY(!s.songs.at(0).followChords);
        QVERIFY(s.songs.at(1).followChords);
        QVERIFY(setSongFollowChords(s, 5, false).error().code == ErrorCode::OutOfRange);
    }
```

(Use the file's existing fixture for a two-song setlist; if it has none, build one with `makeSong` twice as the other tests in the file do.)

- [ ] **Step 2: Run them to see them fail**

Run: `tools\build.ps1 -Target tst_setlist_json -Filter "tst_setlist_json|tst_editing"`
Expected: build error, `followChords` is not a member of `Song`.

- [ ] **Step 3: Implement**

`Model.h`, in `struct Song` after `switchEarly`:

```cpp
    // The sections follow the chords played (true), or the tempo (Play counts bars).
    bool followChords = true;
```

`SetlistJson.cpp`, reading (after the `switchEarly` line):

```cpp
    song.followChords = !obj.contains("followChords"_L1) || r.boolean(obj, "followChords"_L1, path);
```

writing (after `{u"switchEarly"_s, song.switchEarly},`):

```cpp
                       {u"followChords"_s, song.followChords},
```

`Editing.h` (next to `setSongSwitchEarly`):

```cpp
// Whether the song's sections follow the chords played or its tempo.
Result<void> setSongFollowChords(Setlist& setlist, int songIndex, bool follow);
```

`Editing.cpp` (after `setSongSwitchEarly`):

```cpp
Result<void> setSongFollowChords(Setlist& setlist, int songIndex, bool follow)
{
    if (!inRange(songIndex, setlist.songs.size())) return missing(u"Song %1"_s.arg(songIndex + 1));
    setlist.songs.at(toIndex(songIndex)).followChords = follow;
    return {};
}
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `tools\build.ps1 -Target tst_setlist_json -Filter "tst_setlist_json|tst_editing|tst_validation"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core tests/core
git commit -m "feat: each song follows its chords or its tempo (saved with the setlist)"
```

---

### Task 7: The document follows (DocumentController)

**Files:**
- Modify: `src/ui/cpp/DocumentController.h`, `src/ui/cpp/DocumentController.cpp`
- Test: `tests/ui/tst_document_controller.cpp`

**Interfaces:**
- Consumes: `core::buildSongMap`, `core::SongMap` (Task 2); `engine::followStepOf`, `IEngine::setChordFollow`, `IEngine::chordFollow` (Tasks 3, 5); `core::setSongFollowChords` (Task 6).
- Produces (QML-visible):
  - `Q_PROPERTY(bool songFollowChords READ songFollowChords NOTIFY songChanged)`, `Q_INVOKABLE bool setSongFollowChords(int song, bool follow)`
  - `Q_PROPERTY(bool following READ following NOTIFY sectionsChanged)` (this song follows chords now)
  - `Q_PROPERTY(QString followFirstChord READ followFirstChord NOTIFY sectionsChanged)`
  - `Q_INVOKABLE QString followLabel(int step) const` ("Chorus · chord 2 of 8")
  - `Q_INVOKABLE int followLine(int step) const` (the index in `chartLines(currentChart)` of the step's line; -1 = none)
  - `chartLines()` segments gain `"steps"` (list of step indices) and `"understood"` (bool).

- [ ] **Step 1: Write the failing tests**

In `tests/ui/tst_document_controller.cpp`:

```cpp
    void aSongsChordsAreFollowed()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->setSongChart(0, u"{sov: Verse 1}\n[Am]One [F]two\n{eov}\n{soc: Chorus}\n[C]three [G]four\n{eoc}\n"_s));
        QCOMPARE(m_engine->follow.steps.size(), std::size_t{4});
        QCOMPARE(m_engine->follow.sectionStarts, (std::vector<int>{0, 2}));
        QVERIFY(m_doc->following());
        QCOMPARE(m_doc->followFirstChord(), u"Am"_s);
        QCOMPARE(m_doc->followLabel(0), u"Verse 1 · chord 1 of 2"_s);
        QCOMPARE(m_doc->followLabel(3), u"Chorus · chord 2 of 2"_s);
        // chartLines: {sov}, the verse line, {soc}, the chorus line.
        QCOMPARE(m_doc->followLine(2), 3);
        const QVariantList lines = m_doc->chartLines(m_doc->currentChart());
        const QVariantList chorus = lines.at(3).toMap().value(u"segments"_s).toList();
        QCOMPARE(chorus.at(0).toMap().value(u"steps"_s).toList(), (QVariantList{2}));

        // By tempo instead: not followed.
        QVERIFY(m_doc->setSongFollowChords(0, false));
        QVERIFY(!m_doc->songFollowChords());
        QVERIFY(m_engine->follow.steps.empty());
        QVERIFY(!m_doc->following());
        QVERIFY(m_doc->followFirstChord().isEmpty());
    }

    void aChordTheAppCannotReadIsMarked()
    {
        const QVariantList lines = m_doc->chartLines(u"[Am]One [Xq7]two\n"_s);
        const QVariantList segments = lines.at(0).toMap().value(u"segments"_s).toList();
        QVERIFY(segments.at(0).toMap().value(u"understood"_s).toBool());
        QVERIFY(!segments.at(1).toMap().value(u"understood"_s).toBool());
        QVERIFY(segments.at(1).toMap().value(u"steps"_s).toList().isEmpty());
    }

    void anEditedChartKeepsItsPlace()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->setSongChart(0, u"[Am]One [F]two [C]three [G]four\n"_s));
        m_engine->followPosition = engine::ChordFollowPosition{.active = true, .started = true, .step = 2, .section = -1};
        QVERIFY(m_doc->setSongChart(0, u"[Am]One [F]two [C]tres [G]four\n"_s)); // a word fixed while playing C
        QCOMPARE(m_engine->follow.resumeAt, 2);
    }
```

- [ ] **Step 2: Run them to see them fail**

Run: `tools\build.ps1 -Target tst_document_controller -Filter tst_document_controller`
Expected: build error, no member `following`.

- [ ] **Step 3: Implement**

`DocumentController.h`: `#include "gigchain/core/SongMap.h"`; the properties next to `songSwitchEarly`:

```cpp
    Q_PROPERTY(bool songFollowChords READ songFollowChords NOTIFY songChanged)
    Q_PROPERTY(bool following READ following NOTIFY sectionsChanged)
    Q_PROPERTY(QString followFirstChord READ followFirstChord NOTIFY sectionsChanged)
```

the methods next to `songSwitchEarly()`/`setSongSwitchEarly`:

```cpp
    [[nodiscard]] bool songFollowChords() const;
    Q_INVOKABLE bool setSongFollowChords(int song, bool follow);
    // Chord follow: whether this song follows its chords now, its first chord,
    // "Chorus · chord 2 of 8" for a step, and the chartLines() index of its line.
    [[nodiscard]] bool following() const { return m_songMap.followable(); }
    [[nodiscard]] QString followFirstChord() const;
    Q_INVOKABLE QString followLabel(int step) const;
    Q_INVOKABLE int followLine(int step) const;
```

and the member `core::SongMap m_songMap; // the current song's chords, when it follows them`.

`DocumentController.cpp`: `#include "gigchain/core/SongMap.h"`. In `applySectionsToEngine`, after `m_engine.setSongSections(sections);`:

```cpp
    // Chord follow: the song's chords, when its sections follow them. An
    // edit while playing carries on from the chord at the same place.
    std::optional<std::pair<int, int>> place;
    const int playing = newSong ? -1 : m_engine.chordFollow().step;
    if (playing >= 0 && std::cmp_less(playing, m_songMap.steps.size())) place = m_songMap.steps.at(static_cast<std::size_t>(playing)).places.front();
    m_songMap = song != nullptr && song->followChords ? core::buildSongMap(core::parseChordPro(song->chart)) : core::SongMap{};
    if (!m_songMap.followable()) m_songMap = {};
    engine::ChordFollowMap follow;
    follow.sectionStarts = m_songMap.sectionStarts;
    for (std::size_t i = 0; i < m_songMap.steps.size(); ++i) {
        const core::SongStep& step = m_songMap.steps.at(i);
        follow.steps.push_back(engine::followStepOf(step.shape, step.section));
        const bool here = place && std::ranges::find(step.places, *place) != step.places.end();
        // The same step if it is still there (a repeated line has the place several times).
        if (here && (follow.resumeAt < 0 || std::cmp_equal(i, playing))) follow.resumeAt = static_cast<int>(i);
    }
    m_engine.setChordFollow(follow);
```

The new functions:

```cpp
bool DocumentController::songFollowChords() const
{
    const core::Song* song = currentSong();
    return song == nullptr || song->followChords;
}

bool DocumentController::setSongFollowChords(int song, bool follow)
{
    if (auto r = core::setSongFollowChords(m_setlist, song, follow); !r) return report(r.error());
    setDirty(true);
    if (song == m_cursor.song) applySectionsToEngine();
    emit songChanged();
    return true;
}

QString DocumentController::followFirstChord() const
{
    return m_songMap.steps.empty() ? QString() : m_songMap.steps.front().name;
}

QString DocumentController::followLabel(int step) const
{
    if (step < 0 || std::cmp_greater_equal(step, m_songMap.steps.size())) return {};
    const auto at = [this](int i) { return m_songMap.steps.at(static_cast<std::size_t>(i)).section; };
    const int section = at(step);
    int first = step;
    while (first > 0 && at(first - 1) == section) --first;
    int last = step;
    while (std::cmp_less(last + 1, m_songMap.steps.size()) && at(last + 1) == section) ++last;
    const QString where = tr("chord %1 of %2").arg(step - first + 1).arg(last - first + 1);
    const core::Song* song = currentSong();
    const std::vector<core::ChartSection> sections =
        song != nullptr ? core::chartSections(core::parseChordPro(song->chart)) : std::vector<core::ChartSection>{};
    if (section < 0 || std::cmp_greater_equal(section, sections.size())) return where;
    return tr("%1 · %2").arg(sections.at(static_cast<std::size_t>(section)).name, where);
}

int DocumentController::followLine(int step) const
{
    const core::Song* song = currentSong();
    if (song == nullptr || step < 0 || std::cmp_greater_equal(step, m_songMap.steps.size())) return -1;
    const int line = m_songMap.steps.at(static_cast<std::size_t>(step)).places.front().first;
    // chartLines() leaves out directives and section ends.
    const core::Chart chart = core::parseChordPro(song->chart);
    int shown = 0;
    for (int i = 0; i < line && std::cmp_less(i, chart.lines.size()); ++i) {
        const auto kind = chart.lines.at(static_cast<std::size_t>(i)).kind;
        if (kind != core::ChartLine::Kind::Meta && kind != core::ChartLine::Kind::SectionEnd) ++shown;
    }
    return shown;
}
```

In `chartLines`, before the line loop:

```cpp
    // Chord follow: which steps each chord is (lit when played).
    const core::SongMap map = core::buildSongMap(chart);
    std::map<std::pair<int, int>, QVariantList> stepsAt;
    for (std::size_t s = 0; s < map.steps.size(); ++s) {
        for (const auto& place : map.steps.at(s).places) stepsAt[place] << static_cast<int>(s);
    }
```

and replace the segments loop with:

```cpp
        QVariantList segments;
        int chordIndex = 0;
        for (const core::ChartSegment& segment : line.segments) {
            QVariantList steps;
            bool understood = true;
            if (!segment.chord.isEmpty()) {
                const auto found = stepsAt.find({static_cast<int>(i), chordIndex++});
                if (found != stepsAt.end()) steps = found->second;
                understood = core::parseChordName(segment.chord).has_value();
            }
            segments << QVariantMap{{u"chord"_s, segment.chord}, {u"text"_s, segment.text},
                                    {u"steps"_s, steps}, {u"understood"_s, understood}};
        }
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `tools\build.ps1 -Target tst_document_controller -Filter "tst_document_controller|tst_settings"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ui/cpp tests/ui/tst_document_controller.cpp
git commit -m "feat: the current song's chords go to the engine to be followed"
```

---

### Task 8: On the screen (EngineStatus, chart, toolbar, song settings)

**Files:**
- Modify: `src/ui/cpp/EngineStatus.h`, `src/ui/cpp/EngineStatus.cpp` (`pollTransport`)
- Modify: `src/ui/ChartSegment.qml`, `src/ui/ChartView.qml`, `src/ui/PerformView.qml`, `src/ui/ChartPanel.qml`, `src/ui/Toolbar.qml`, `src/ui/SetlistView.qml`
- Test: `tests/ui/tst_qml_smoke.cpp`

**Interfaces:**
- Consumes: `IEngine::chordFollow()` (Task 5); `DocumentController.following`, `followFirstChord`, `followLabel()`, `followLine()`, `songFollowChords`, `setSongFollowChords()`, `chartLines()` segment `steps`/`understood` (Task 7).
- Produces: `EngineStatus.chordFollowing`, `chordStarted`, `chordStep` (NOTIFY `chordFollowChanged`); `ChartView.currentStep`, `followStarted`, `followY`; object names `followStartLine`, `chartChordCurrent`, `chartChordNext`, `songFollowChordsBox`.

- [ ] **Step 1: Write the failing test**

`tests/ui/tst_qml_smoke.cpp` (the fake engine is not started, so the first chord is shown and outlined):

```cpp
    void aChartWaitsForItsFirstChord()
    {
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Piano"_s));
        QVERIFY(doc.setSongChart(0, u"{c: Verse}\n[Am]words [F]more\n{c: Chorus}\n[C]la [G]la\n"_s));
        auto* tabs = w->findChild<QObject*>(u"mainTabs"_s);
        QVERIFY(tabs != nullptr);
        QVERIFY(tabs->setProperty("currentIndex", 0));
        settle();
        auto* chart = w->findChild<QQuickItem*>(u"chartView"_s);
        QVERIFY(chart != nullptr);
        QQuickItem* start = findItem(chart, u"followStartLine"_s);
        QVERIFY(start != nullptr);
        QTRY_VERIFY(start->isVisible());
        QCOMPARE(start->property("text").toString(), u"Play Am to start"_s);
        const QList<QQuickItem*> next = findAll(chart, u"chartChordNext"_s);
        QVERIFY(!next.isEmpty()); // the first chord, outlined
        QVERIFY(findAll(chart, u"chartChordCurrent"_s).isEmpty()); // nothing lit before the start
        shoot(u"follow-waiting"_s);

        // Following by tempo: no start line.
        QVERIFY(doc.setSongFollowChords(0, false));
        QTRY_VERIFY(!start->isVisible());
    }
```

- [ ] **Step 2: Run it to see it fail**

Run: `tools\build.ps1 -Target tst_qml_smoke -Filter tst_qml_smoke`
Expected: FAIL, `followStartLine` not found.

- [ ] **Step 3: EngineStatus**

`EngineStatus.h`, next to the song position properties:

```cpp
    // Chord follow: a song's chords are followed, the first one was heard,
    // and which chord (of DocumentController's map) is being played.
    Q_PROPERTY(bool chordFollowing READ chordFollowing NOTIFY chordFollowChanged)
    Q_PROPERTY(bool chordStarted READ chordStarted NOTIFY chordFollowChanged)
    Q_PROPERTY(int chordStep READ chordStep NOTIFY chordFollowChanged)
```

getters `[[nodiscard]] bool chordFollowing() const { return m_follow.active; }`, `chordStarted() { return m_follow.started; }`, `chordStep() { return m_follow.step; }`, the signal `void chordFollowChanged();`, and the member `engine::ChordFollowPosition m_follow;`.

`EngineStatus.cpp`, at the end of `pollTransport()`:

```cpp
    if (const engine::ChordFollowPosition follow = m_engine.chordFollow(); follow != m_follow) {
        m_follow = follow;
        emit chordFollowChanged();
    }
```

- [ ] **Step 4: The chart**

`ChartSegment.qml` becomes:

```qml
import QtQuick

// One piece of a chart line: its chord above the words it changes on.
// `modelData` is a segment from DocumentController.chartLines(). With chord
// follow, the chord being played is lit and the next one outlined; a chord
// the app cannot read is dimmed and struck through (fix it in the editor).
Column {
    id: segment

    required property var modelData
    property real size: 1.0
    property int currentStep: -1
    property bool started: false
    property bool measuring: false // the hidden copy that only measures a line
    readonly property var steps: segment.modelData.steps !== undefined ? segment.modelData.steps : []
    readonly property bool current: segment.started && segment.steps.indexOf(segment.currentStep) >= 0
    readonly property bool next: !segment.current && segment.steps.indexOf(segment.started ? segment.currentStep + 1 : 0) >= 0
    readonly property bool understood: segment.modelData.understood !== false

    objectName: segment.measuring ? "" : segment.current ? "chartChordCurrent" : segment.next ? "chartChordNext" : ""

    Text {
        id: chordText
        text: segment.modelData.chord !== "" ? segment.modelData.chord : " "
        color: !segment.understood ? Theme.textDim : segment.current ? Theme.accent : Theme.chord
        font.pixelSize: (Theme.fontSize + 5) * segment.size
        font.bold: true
        font.strikeout: !segment.understood
        // Chords with no words under them (an intro, a turnaround) keep a
        // gap between them.
        rightPadding: segment.modelData.text.trim() === "" ? 18 * segment.size : 0
        Rectangle {
            // The next chord: outlined, so the eye finds it.
            visible: segment.next && segment.modelData.chord !== ""
            x: -3 * segment.size
            y: -1 * segment.size
            width: chordText.contentWidth + 6 * segment.size
            height: chordText.contentHeight + 2 * segment.size
            color: "transparent"
            border.color: Theme.accent
            border.width: 1
            radius: Theme.radiusSmall
        }
    }
    Text {
        text: segment.modelData.text !== "" ? segment.modelData.text : " "
        color: Theme.text
        font.pixelSize: (Theme.fontSize + 7) * segment.size
    }
}
```

`ChartView.qml`: add after `property int bar: 0`:

```qml
    // Chord follow (from EngineStatus): the chord being played, lit.
    property int currentStep: -1
    property bool followStarted: false
    // Where the line being played is (-1: not following), for the view
    // around the chart to scroll to.
    readonly property real followY: {
        const line = chart.doc !== null && chart.followStarted ? chart.doc.followLine(chart.currentStep) : -1
        const item = line >= 0 ? lineRepeater.itemAt(line) : null
        return item !== null ? item.y : -1
    }

    Text {
        objectName: "followStartLine"
        width: chart.width
        visible: chart.doc !== null && chart.doc.following && !chart.followStarted
        text: chart.doc !== null ? qsTr("Play %1 to start").arg(chart.doc.followFirstChord) : ""
        horizontalAlignment: Text.AlignHCenter
        color: Theme.textDim
        font.pixelSize: (Theme.fontSize + 2) * chart.size
        bottomPadding: 6 * chart.size
    }
```

give the line Repeater `id: lineRepeater`, and in both `ChartSegment` delegates of the lyric line add:

```qml
                                currentStep: chart.currentStep
                                started: chart.followStarted
```

with `measuring: true` on the one inside the hidden `natural` Row.

- [ ] **Step 5: Scrolling, the toolbar and the song setting**

`PerformView.qml`, on the `stageChart` ChartView:

```qml
                        currentStep: perform.engineStatus.chordStep
                        followStarted: perform.engineStatus.chordStarted
```

and inside the `performChart` Flickable:

```qml
                    // Chord follow: the line being played stays in the upper third.
                    NumberAnimation {
                        id: followScroll
                        target: performChart
                        property: "contentY"
                        duration: 250
                        easing.type: Easing.OutCubic
                    }
                    Connections {
                        target: stageChart
                        function onFollowYChanged() {
                            if (stageChart.followY < 0) return
                            followScroll.to = Math.max(0, Math.min(stageChart.followY - performChart.height / 3,
                                                                   performChart.contentHeight - performChart.height))
                            followScroll.restart()
                        }
                    }
```

`ChartPanel.qml`: give the reading `ScrollView` (line ~116) `id: chartScroll` and its `ChartView` `id: panelChart`, set `currentStep: panel.engineStatus.chordStep` and `followStarted: panel.engineStatus.chordStarted`, and add inside the ScrollView the same `NumberAnimation` (target `chartScroll.contentItem`) and `Connections` (target `panelChart`, height `chartScroll.height`, content height `chartScroll.contentItem.contentHeight`).

`Toolbar.qml`, `songPlayButton`: `visible: !bar.engineStatus.chordFollowing` (Play counts bars; while following chords there is nothing to start). The `songWhere` Text's `text` becomes:

```qml
                    text: bar.engineStatus.chordFollowing
                          ? (bar.engineStatus.chordStarted ? qsTr("Following: %1").arg(bar.doc.followLabel(bar.engineStatus.chordStep))
                                                           : qsTr("Play %1 to start").arg(bar.doc.followFirstChord))
                          : parent.section === null ? ""
                          : bar.engineStatus.songCountingIn ? qsTr("Count-in…")
                          : bar.engineStatus.songPlaying ? qsTr("%1 · %2/%3").arg(parent.section.name).arg(bar.engineStatus.songBar).arg(bar.engineStatus.songBars)
                          : parent.section.name
```

and the `songTransport` Row's `visible` becomes `bar.doc.currentSections.length > 0 || bar.doc.following`.

`SetlistView.qml`, in `tempoPopup`: in `onAboutToShow` add `followBox.checked = view.doc.songFollowChords`; after `earlyBox`:

```qml
            CheckBox {
                id: followBox
                objectName: "songFollowChordsBox"
                Layout.leftMargin: Theme.spacing
                text: qsTr("Sections follow the chords I play (off: they follow the tempo)")
                focusPolicy: Qt.NoFocus
            }
```

and under it the hint for a song whose chart has too few readable chords (spec §5):

```qml
            Label {
                objectName: "songFollowHint"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                visible: followBox.checked && !view.doc.following
                text: qsTr("Add chords to the chart to follow them (until then the tempo leads).")
                color: Theme.textDim
                wrapMode: Text.Wrap
            }
```

and in the Set button's `onClicked`, after `setSongSwitchEarly`: `view.doc.setSongFollowChords(tempoPopup.song, followBox.checked)`.

Add to the smoke test (Step 1), after the start line checks: set a one-chord chart with `doc.setSongChart(0, u"[C]only one"_s)` and verify `doc.following()` is false (the hint's condition).

- [ ] **Step 6: Run the tests to see them pass**

Run: `tools\build.ps1 -Target tst_qml_smoke -Filter "tst_qml_smoke|tst_document_controller"`
Expected: PASS; `build\debug\...\follow-waiting.png` shows "Play Am to start" and Am outlined.

- [ ] **Step 7: Commit**

```bash
git add src/ui tests/ui/tst_qml_smoke.cpp
git commit -m "feat: the chart lights the chord being played and scrolls with it"
```

---

### Task 9: Whole-branch check

**Files:**
- Modify: `docs/ROADMAP.md` (section 5, "Charts that follow the song": what version 1 does)

- [ ] **Step 1: The gate**

Run: `powershell -File tools\verify.ps1`
Expected: `Verification gate PASSED` (build, clang-tidy, cppcheck, qmllint, every test).

- [ ] **Step 2: Sanitizer and soak**

Run: `tools\build.ps1 -Preset asan` then `tools\soak.ps1 -Minutes 20`
Expected: every test passes under ASan; `SOAK PASSED` (no memory, handle or thread growth, no dropouts).

- [ ] **Step 3: Roadmap**

In `docs/ROADMAP.md` section 5, replace its body with what shipped: chord follow version 1 (the five rules, per-song chords/tempo), and what is next (following the tempo from the playing, a backing track kept in time, recognising the song, audio input).

- [ ] **Step 4: Commit**

```bash
git add docs/ROADMAP.md
git commit -m "docs: chord follow in the roadmap"
```
