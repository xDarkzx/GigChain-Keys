# Song sections: instruments per verse, chorus, bridge

Status: design, awaiting review (2026-09-27)

## What the player gets

A song's chart already shows its sections (Intro, Verse 1, Chorus,
Bridge...), usually straight from a pasted chord sheet or tab. Each section
title gets an assign row: which of the patch's instruments play in that
section. Live, the song's bars are counted at its tempo and the sound
changes by itself, exactly on the first beat of each section.

No typing, no codes, no chart edit mode: the chart's text editor stays for
words and chords only. Assigning is clicking, in both Edit and Perform.

## 1. Sections

- **Where they come from:** the song's chart. A section is:
  - a ChordPro section (`{start_of_chorus}`, `{sov: Verse 2}`...), or
  - a comment line whose text is a section name, as pasted sheets give
    (`[Verse 1]` becomes `{comment: Verse 1}`): Intro, Verse, Pre-Chorus,
    Chorus, Post-Chorus, Bridge, Solo, Instrumental, Interlude, Break,
    Breakdown, Build, Drop, Hook, Refrain, Tag, Outro, Coda, Ending, each
    optionally numbered or followed by a note (`Chorus 2`, `Verse (x2)`).
- **Identity:** a section is its label plus which occurrence of that label
  it is (the second "Chorus" is Chorus #2). Verse 1 and Verse 2 are
  separate sections. Editing the words of the chart keeps assignments; a
  section whose label disappears from the chart loses its assignment (kept
  in the file until the song is saved without it, so an undo brings it back).
- **Length in bars:** shown on the title row, editable (click, type, Enter).
  Until set by hand it is a guess: one bar per chord in the section, and a
  repeat mark (`x2`, `(x3)`) on the title or a line multiplies what it
  covers. A section with no chords guesses 4 bars. Manual lengths are saved;
  guesses are not (they follow chart edits).

## 2. Assigning instruments

On every section title, in the chart view (Edit and Perform):

```
                          CHORUS
          [ Piano ✕ ] [ Strings ✕ ] [ + ]   · 8 bars
```

- **Section titles are centred and larger** than the lyrics (about 1.5x the
  chart's text size, bold, spaced out), so they can be read at a glance
  from the keyboard; the assign row sits centred just under the title.
  The lyrics and chords stay left-aligned as now.

- **Default:** every section plays the patch's first instrument channel.
- **[ + ]** opens a menu of the patch's other channels (as the effect slot's
  menu does); picking one adds it. Several = layered.
- **✕** removes one. A section with none is silent (a break).
- Assignments are per song and patch channel (by channel id), so renaming or
  reordering channels keeps them; a channel deleted from the patch drops out.
- Undoable like every other edit.

**The rule while a song with sections plays:** a channel sounds only in the
sections it is assigned to; every other channel is muted.

**No sections** (no chart, or no section titles in it): every instrument
in the patch plays at once, exactly as today.

## 3. Tempo and time signature

- Songs already have a tempo (right-click a song). Pasting a chart reads a
  tempo from the text (`Tempo: 96`, `96 BPM`, `BPM: 96`, `{tempo: 96}`) and
  a time signature (`{time: 6/8}`, `Time: 3/4`). When the song has none
  set, the found values are used, with a notice saying so. A song's own
  setting is never overwritten.
- Songs get a time signature: 4/4 unless set.
- The song's tempo and time signature drive the count (and the click and
  plugins, as today).

## 4. Playing live

- **Play / Stop** for the song in Perform (and the toolbar), with an
  optional one-bar count-in on the click. Starting the song's backing track
  starts the count with it, and stopping stops it.
- It starts at the first section, or at the section selected in the chart
  (click a section title to select it).
- The engine counts bars on the audio thread. The section change happens on
  the sample of the section's first beat: channels leaving let their held
  notes and reverb tails ring out (as on a patch change); channels arriving
  start from the next note, never mid-note.
- **One beat early** (per song, off by default): switch a beat before the
  section, for pads that swell in.
- The chart highlights the section playing and shows "Chorus · bar 3 of 8";
  after the last section the count stops and the last sound stays.
- **Next section pedal/button** (learnable like the other pedal actions):
  jumps to the next section at once and carries on counting from its first
  bar. For when the band repeats a chorus or drifts off the count.
  (Included as a live safety net; drop it here if unwanted.)

## 5. Stored

In the setlist file (format 4; format 3 files load with no sections
assigned, i.e. defaults):

- Song: `timeSignature` (e.g. "4/4"), `switchEarly` (bool),
  `sections`: `[{label, occurrence, bars (0 = guessed), channels: [channel id...], assigned: bool}]`
  where `assigned: false` means the default (first instrument).

## 6. Not in this version (roadmap)

- Following the player by ear (chords/notes against the chart).
- Tempo or time signature changes within a song.
- Fading between sections (switches are instant, on the beat).
- Sections changing volumes or effects (only which instruments play).

## 7. Tests (end to end, measured)

- Core: section detection from ChordPro and from pasted sheets (labels,
  numbering, notes, occurrences); bar guesses with repeats; tempo and time
  read from pasted text; setlist JSON v4 round trip and v3 defaults.
- Document: default assignment, add/remove, undo, channel delete, chart
  edit keeping assignments, no-sections = all play.
- Engine: bar counting at 120 BPM 4/4 and 6/8 switches on the exact sample
  of each section's first beat (and one beat early when set); leaving
  channels get note-offs and ring out; arriving channels ignore notes held
  before the switch; next-section jump; backing track start/stop drives the
  count; no allocation on the audio thread.
- UI smoke: assign row on section titles, the [ + ] menu lists the patch's
  channels, bar length edit, Play/Stop, current-section highlight.
