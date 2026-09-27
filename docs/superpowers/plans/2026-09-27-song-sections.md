# Song Sections Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sections of a song's chart (Intro, Verse 1, Chorus...) each choose which of the patch's instruments play; live, the song's bars are counted at its tempo and the instruments switch by themselves on each section's first beat.

**Architecture:** Core finds the sections in the chart (ChordPro sections and pasted `[Verse 1]` comments), guesses their bar lengths, stores per-section assignments on the song (file format 4) and resolves them against the current patch. The engine gates *new notes* per channel strip: a strip that is not in the current section drops note-ons (note-offs, sustain and controllers still pass), so leaving instruments ring out and arriving ones start on the next note. The switch point is sample-exact because each note-on is compared with the section boundary's offset inside the block. The engine owns a song timeline (sections' start beats) and a transport (play from a section with optional count-in, stop, jump), resetting the beat clock to bar 1 on Play like a DAW. The UI shows centred, larger section titles with an instrument chip row, a [+] menu of the patch's instruments, an editable bar count, the playing section highlighted, and a song Play/Stop.

**Tech Stack:** C++20, Qt 6.10 (QML, QtTest), VST3 host engine, lock-free HazardExchange hand-off.

**Spec:** `docs/superpowers/specs/2026-09-27-song-sections-design.md`

## Global Constraints

- Never swallow errors: every failure returned with its cause and logged (notifications in the UI).
- No timers or delays as workarounds; no synthetic clicks in the user's running app.
- Never hardcode plugin names or keyboard mappings.
- Audio thread: no allocation, locks or logging.
- Edit source with the Edit tool only (no sed/Python rewrites).
- Everything verified by `tools\verify.ps1` (build /W4 /WX, clang-tidy, cppcheck, qmllint, all ctest) at the end, plus a real-app check.
- Setlist files are untrusted: every new field bounded and validated.

## Review Focus

1. A song whose chart has no sections must behave exactly as today (everything plays), including when its patch changes.
2. A note played a hair before the section's downbeat should land in the new section (players anticipate): the default lead is a sixteenth note (a quarter of a quarter note); "one beat early" makes it one beat.
3. Switching songs or patches must never leave every instrument silent for a moment (a plan whose channel ids match none of the new graph's strips = everything plays).
4. Duplicating a song keeps its section assignments (channel ids remapped); deleting a channel from a patch drops it from sections without errors.
5. The backing track and the count stay in step: Play with count-in starts the track exactly on bar 1; a jump moves both.

## Rulings made while planning

- Ruling: default lead = a sixteenth note before the downbeat, not "exactly on the beat" — the user asked for "instantly or right before it", and a chord struck a few ms early must not go to the old sound — cost if wrong: one constant.
- Ruling: count-in plays when the click is on — no extra per-song setting — cost if wrong: add a toggle later.
- Ruling: clicking a section title while the song plays jumps there (same path as the pedal); stopped, it selects where Play starts — cost if wrong: remove one call.
- Ruling: sections resolve against whichever patch of the song is up; assigned channels not in that patch fall back to the patch's first instrument (an explicitly emptied section stays silent) — cost if wrong: multi-patch songs with sections behave as "first instrument".
- Ruling: at most 64 sections per song (a 64-bit mask per strip) and 999 bars per section.

---

### Task 1: Sections in charts (core)

**Files:**
- Modify: `src/core/include/gigchain/core/Chart.h`, `src/core/internal/Chart.cpp`
- Test: `tests/core/tst_chart.cpp`

**Interfaces — Produces:**
```cpp
// Chart gains:  int timeNumerator = 0; int timeDenominator = 0; // 0 = not given ({time: 6/8})
// ImportedSheet gains: int timeNumerator = 0; int timeDenominator = 0;
struct ChartSection {
    QString name;      // "Verse 1", "Chorus": the label without a trailing note; identity with `occurrence`
    QString label;     // as written, e.g. "Chorus (x2)"
    int occurrence = 1;// 2 = the second section with this name (case-insensitive)
    int guessedBars = 4;
    int line = 0;      // index into Chart::lines
};
[[nodiscard]] bool isSectionName(const QString& label); // Intro, Verse 2, Pre-Chorus, Chorus (x2)...
[[nodiscard]] std::vector<ChartSection> chartSections(const Chart& chart);
```
Rules: a `Section` line is always a section; a `Comment` line is one when `isSectionName(label)`. Name = the section word(s) plus an optional number (`Verse 1`), trimmed, in the case written. Guessed bars = chords on the section's lyric lines up to the next section (a line whose words are a repeat mark `x2`/`(x3)`/`2x` multiplies that line's chords; a repeat mark in the label multiplies the whole section); no chords → 4; clamped 1..999.
Tempo in pasted sheets also from `96 BPM` / `96bpm` lines; time from `Time: 3/4`, `Time signature: 6/8` header lines and `{time: 6/8}`.

- [ ] Step 1: tests — `sectionsFromPastedSheet` (import "[Intro]\nC G\n[Verse 1]\nC  G\nwords\nAm F\nmore\n[Chorus] (x2)\nF G C\n" → names Intro/Verse 1/Chorus, bars 2/4/6, occurrences 1), `sectionsFromChordPro` ({soc}, {sov: Verse 2}, repeated Chorus → occurrence 2), `commentsThatAreNotSections` ("Capo 2", "play softly" are not), `sectionWithoutChordsGuessesFour`, `lineRepeatMultiplies`, `timeAndBpmFromSheet` (header "Tempo: 96" / "88 BPM" / "Time: 6/8", `{time: 3/4}`).
- [ ] Step 2: run `tools\build.ps1 -Filter chart` — FAIL (not declared).
- [ ] Step 3: implement.
- [ ] Step 4: run — PASS.

### Task 2: Song model, file format 4, editing, resolving (core)

**Files:**
- Modify: `Model.h`, `Model.cpp` (withFreshIds remap), `Limits.h`, `Validation.cpp`, `SetlistJson.cpp` (+ format constant), `Editing.h/.cpp`
- Create: `src/core/include/gigchain/core/Sections.h`, `src/core/internal/Sections.cpp`
- Test: `tst_setlist_json.cpp`, `tst_editing.cpp`, `tst_validation.cpp`, new `tests/core/tst_sections.cpp`

**Interfaces — Produces:**
```cpp
struct SectionSetup { QString name; int occurrence = 1; int bars = 0 /*0 = guessed*/; bool assigned = false;
                      std::vector<ChannelId> channels; friend bool operator==(...) = default; };
// Song gains: int timeNumerator = 4; int timeDenominator = 4; bool switchEarly = false; std::vector<SectionSetup> sections;
// limits: kMaxSectionsPerSong = 64; kMaxSectionBars = 999; time numerator 1..32, denominator 1,2,4,8,16,32.
Result<void> setSectionSetup(Setlist&, int song, const SectionSetup&); // upsert by (name case-insensitive, occurrence)
Result<void> setSongTimeSignature(Setlist&, int song, int numerator, int denominator);
Result<void> setSongSwitchEarly(Setlist&, int song, bool early);
struct ResolvedSection { ChartSection chart; int bars; bool guessed; bool assigned; std::vector<ChannelId> channels; /*as set*/
                         std::vector<ChannelId> live; /*resolved against the patch*/ };
[[nodiscard]] std::vector<ResolvedSection> resolveSections(const Song& song, const Patch& patch);
[[nodiscard]] std::optional<ChannelId> firstInstrument(const Patch& patch);
```
JSON: song fields `timeSignature` ("4/4"), `switchEarly`, `sections` [{name, occurrence, bars, assigned, channels:[ids]}]; format 3 files load with defaults; bad values rejected with the path.

- [ ] Step 1: tests — JSON v4 round trip; v3 defaults; rejects bars 1000, "5/5", 65 sections, empty channel id; editing upsert/replace; bad index; `withFreshIds` remaps section channel ids to the copies; resolve: default = first instrument (skips audio-input channels); assigned subset; foreign ids → default; assigned-empty → silent; no sections → empty list; bars manual beats guess.
- [ ] Step 2: FAIL. Step 3: implement. Step 4: PASS (`-Filter "setlist_json|editing|validation|sections"`).

### Task 3: Note gating per strip (engine)

**Files:** `RenderGraph.h/.cpp`; Test: `tst_render_graph.cpp`

**Interfaces — Produces:**
```cpp
// ChannelStrip: void setSections(uint64_t mask) (main thread; bit s = plays in section s; default all ones)
//               [[nodiscard]] uint64_t sections() const;
struct SectionGate { int before = -1; int after = -1; int switchAt = 0; }; // -1 = no sections: everything plays
// RenderGraph::render(..., const SectionGate& gate = {}) ; ChannelStrip::render(..., const SectionGate&)
```
A strip drops a note-on (0x9n, velocity > 0) when its bit for the section in force at the event's offset (`offset < switchAt ? before : after`) is clear. Everything else passes.

- [ ] Step 1: tests — `gateDropsNotesOutsideSection` (strip A mask 0b01, B 0b10; gate {0,1,switchAt 100}: note at 50 → A only; note at 150 → B only; note-off at 150 reaches A), `noSectionsEverythingPlays`, `gateKeepsControllersAndSustain`.
- [ ] Step 2 FAIL, 3 implement, 4 PASS (`-Filter render_graph`).

### Task 4: Song timeline and transport (engine)

**Files:** `EngineTypes.h`, `IEngine.h`, `RealEngine.h/.cpp`, `Metronome.h` (time signature), `FakeEngine.h/.cpp`, `tests/common/SpyEngine.h`; Test: `tst_real_engine.cpp`, `tst_fake_engine.cpp`

**Interfaces — Produces:**
```cpp
struct SongSections { struct Section { int bars = 4; std::vector<core::ChannelId> live; };
                      std::vector<Section> sections; bool switchEarly = false; };
struct SongPosition { bool playing = false; bool countingIn = false; int section = -1; int bar = 0; int bars = 0; };
virtual void setSongSections(const SongSections&) = 0; // empty = everything plays
virtual void setTimeSignature(int numerator, int denominator) = 0;
virtual void playSong(int fromSection, bool countIn) = 0;
virtual void stopSong() = 0;
virtual void jumpToSection(int section) = 0; // playing: jump now; stopped: select
[[nodiscard]] virtual SongPosition songPosition() const = 0;
```
Behaviour: the timeline (section start beats in quarters, from bars × quarters per bar) is published through `HazardExchange`; strip masks set by id (a plan matching none of a graph's strips → all ones); applyPatch sets masks on the new strips before publishing. Play: beat clock := section start − (count-in ? one bar : 0), click counts in; the backing track (if loaded) starts exactly when bar 1 of the section is reached, at the section's time in the track. Lead: a sixteenth (0.25 quarter), or one beat when switchEarly. After the last section: playing stops, the last section stays in force. Stopped: the selected section is in force. Jump: clock and track move to the section.

- [ ] Step 1: tests — at 120 BPM 48 kHz with sections [2 bars: A][2 bars: B]: a note injected at exactly (bar 3 − lead) − 1 sample reaches A only, at (bar 3 − lead) reaches B only (via two recording nodes); switchEarly moves the edge a beat; 6/8 bar length; count-in delays bar 1 by a bar; position reports section/bar; stop keeps section; jump; end of song stops; backing track starts on bar 1 with count-in (first non-zero sample index); no sections → both play; patch change keeps masks; no allocation in render with sections.
- [ ] Step 2 FAIL, 3 implement, 4 PASS (`-Filter "real_engine|fake_engine"`).

### Task 5: Next-section pedal

**Files:** `MidiControl.h` (NextSection, count 8), `SettingsController.cpp` labels, `EngineStatus.cpp` (action), tests `tst_midi_control.cpp`, `tst_settings.cpp`.
- [ ] Test learnable NextSection round trip in settings and that the action reaches `DocumentController::nextSection()`.

### Task 6: Document and status (UI C++)

**Files:** `DocumentController.h/.cpp`, `EngineStatus.h/.cpp`; Test: `tst_document_controller.cpp`

**Interfaces — Produces (QML):**
```cpp
Q_PROPERTY(QVariantList currentSections READ currentSections NOTIFY sectionsChanged)
  // [{index, name, label, bars, guessed, assigned, channels:[{channel, name}]}] (channels = live ones)
Q_PROPERTY(int songTimeNumerator/songTimeDenominator/songSwitchEarly ... NOTIFY songChanged)
Q_INVOKABLE QVariantList sectionChoices(int section) const;   // the patch's instrument channels not yet in it
Q_INVOKABLE bool addSectionChannel(int section, int channel);
Q_INVOKABLE bool removeSectionChannel(int section, int channel);
Q_INVOKABLE bool setSectionBars(int section, int bars);
Q_INVOKABLE bool setSongTimeSignature(int song, int numerator, int denominator);
Q_INVOKABLE bool setSongSwitchEarly(int song, bool early);
Q_INVOKABLE void playSong(); stopSong(); selectSection(int); nextSection();
QVariantList chartLines(...) // each line gains sectionIndex (-1 = none)
// EngineStatus: songPlaying, songCountingIn, songSection, songBar, songBars (NOTIFY songPositionChanged)
```
Sections pushed to the engine (setSongSections + setTimeSignature) before applyPatch on every song/patch change, and after every section/chart/time edit. Paste: a tempo or time taken from the chart raises a notice. Backing-track play/pause on a song with sections = song Play/Stop.
- [ ] Tests: sections listed from pasted chart; default channel; add/remove/undo; bars edit; pushed plan contents (SpyEngine); switching songs pushes before applyPatch; no-sections song pushes empty plan; paste tempo notice; nextSection; duplicate song keeps assignments.

### Task 7: Screens (QML)

**Files:** `ChartView.qml`, `ChartPanel.qml`, `PerformView.qml`, `Toolbar.qml`, `SetlistView.qml` (song dialog: time signature, switch early), new `SectionHeader.qml`; register in `src/ui/CMakeLists.txt`; Test: `tst_qml_smoke.cpp`
- Section title centred, bold, ~1.5× chart text; chip row centred under it; [+] StageMenu of `sectionChoices`; ✕ removes; "· 8 bars" tap-to-edit; playing section highlighted with "bar 3 of 8"; click title = selectSection.
- Toolbar: song Play/Stop when the song has sections.
- [ ] Smoke: header centred (title centre = chart centre ±1), larger than lyrics; chips show default instrument; [+] lists the other channel; add via menu; bars edit; Play shows "bar"; qmllint clean.

### Task 8: Finish

- [ ] Roadmap section 1 marked in progress/done; spec updated with the lead ruling.
- [ ] Full gate `tools\verify.ps1`; real app via `tools\run.ps1 -Setlist`, screenshot; commit.
