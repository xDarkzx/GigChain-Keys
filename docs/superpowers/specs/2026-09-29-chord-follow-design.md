# Chord follow: the chart follows what you play

Status: design, awaiting review (2026-09-29)

## What the player gets

You pick a song and play. When you play its first chord, the chart starts
following you: the chord you are on lights up, the chart scrolls with you,
and when you reach the chorus its instruments take over on the very note
that starts it. You set the pace: slow down, hold a chord, vamp. Repeat a
chorus, go back to a verse or skip to the bridge, and the chart finds you
at the start of that section. Your "Next section" pedal still moves it by
hand.

It is made for how keyboard players really play, learners included: a
bass note, octave or fifth in the left hand, the chord in the right, any
inversion, broken chords, the sustain pedal, sus and power chords for a
minor seventh, the odd wrong note.

No click and no backing track are needed. Neither live host (MainStage,
Gig Performer) follows a chart this way; arranger keyboards (Yamaha Genos,
Korg Pa) recognise chords the same way, inside the sound engine.

## The five rules (version 1)

1. **Start.** With the song chosen, playing its first chord starts
   following at the top. (Playing the first two chords of another section
   starts there instead, by rule 3.)
2. **Move on.** Playing the **root of the next chord plus any one other
   note of that chord** moves the chart to it and lights it up.
3. **Jump.** Playing chords that are not the next one but are **the first
   two chords of another section** jumps to that section (its second
   chord lit). One chord alone never jumps.
4. **By hand.** The "Next section" pedal (and a click on a section title)
   moves to the start of that section.
5. **On the note.** When following enters a section, its instruments take
   over on the note that made the chord count, not a block later.

## 1. The song map

Built on the main thread from the song's chart whenever the chart, its
sections or the patch change, and handed to the audio thread as the
sections are now (swapped whole, never edited in place).

- **Steps:** every chord in the chart, in order, is a step. A step knows
  its section, where it is in the chart (line and chord on the line, for
  the highlight), its **root**, its **family** (all the notes the chord
  name gives: third, fifth, sevenths, extensions, sus notes) and its
  **slash bass** if written (`D/E`).
- **Repeats** already in the chart ("Chorus x2", a repeated line) expand
  into repeated steps, as the section lengths already count them.
- **The same chord twice in a row is one step** (`[C]Hello [C]world` is one
  C; `C` then `C/E` are two, the bass moves): holding or re-playing a chord
  never skips ahead. The highlight covers
  all the places it is written.
- **Chords the app cannot read** (a typo, "N.C.") are not steps; they
  are shown unlit.
- **Chord names understood:** a root A to G with `#` or `b`, then any of
  `m`/`min`/`-`, `maj`/`M`/`Δ`, `dim`/`°`, `aug`/`+`, `sus2`, `sus4`/`sus`,
  `5`, `6`, `7`, `9`, `11`, `13`, `add9`, `add11`, `m7b5`/`ø`, altered
  `b5 #5 b9 #9 #11 b13`, and `/bass`. Anything after that the app does not
  know is ignored if the root is clear (`Cmaj7(no3)` is still a C chord).
- **A section's first two chords** (for rule 3): its first two steps; a
  section with one chord uses its chord and the step after it.

## 2. Hearing a chord

What counts as "what you play" is every note you are holding, from both
hands:

- keys down;
- keys let go while the sustain pedal is down (pedalled voicings);
- keys let go in the last half second (broken chords and arpeggios: the
  notes of an oom-pah or a rolled chord add up).

Hands, octaves and how many notes do not matter: a left hand of one note,
an octave, root and fifth, or three notes all count the same. The
**lowest note** matters only for a slash chord: the chart's `C/E` is
confirmed by an E at the bottom; for a chord without a slash it is just
another note.

**Rule 2 (the next chord), generous:** the next step's root is held, plus
at least one other note of its family. So for **G#m7**, G#+B, G#+D# (a
power chord), G#+C# (sus4), G#+F#, or G# with a wrong third all move on.
Extra notes (a melody, an added ninth) never stop it.

**Rule 3 (a jump), strict:** a chord counts for a jump only when it is
unmistakable: its root and third are held (for a sus chord its sus note;
for a `5` chord its fifth), no note contradicts the third (no major third
for a minor chord and the other way round), and at least three of its
notes are held (both, for a two-note chord). (G-B-D holds B and D, Bm's
root and third, but not F#: it is not Bm.) Two such chords in a row,
matching a section's first two chords, jump there. The first one is
remembered until a key is played that belongs to neither of those two
chords: the player went somewhere else. A chord matching the next step is
always taken as rule 2 first.

A chord is checked each time a key goes down (and when the pedal goes
down). Nothing is checked while nothing is new, so a held chord never
moves the chart on its own.

## 3. Where it runs

- **`core::SongMap`** (core, plain code): builds the steps from a chart
  and its sections; parses chord names into root, family and bass. No
  audio, no Qt widgets; fully unit-tested.
- **`engine::ChordFollower`** (engine, audio thread, no allocation, no
  locks): fed each block's MIDI events with their sample offsets; keeps
  what is held (with the half-second memory and the pedal); applies the
  rules; answers where following is (step, section) and, when it enters a
  section, the sample offset of the note that did it.
- **The section gate** (as now): during a block where following entered a
  section, the gate switches at that note's sample offset, so the chord's
  last note plays on the new section's instruments. The notes of that
  chord pressed a moment earlier (still in the previous section) are
  handed over too: at the switch, the incoming section's instruments get
  the keys held right then, and the outgoing ones let go of the keys
  pressed since the last recognised chord. Keys held from before keep
  ringing, as tails do now.
- **The screen** reads the position (step, section, whether following
  has started) the way it reads the song position now, about 30 times a
  second, and highlights.

## 4. On the screen

- **The current chord** is lit (the accent colour, bold) on the chart,
  wherever it is written; **the next chord** is outlined, so you can see
  what comes.
- **The chart scrolls** to keep the current line in the upper third
  (Perform and the chart tab), smoothly, never jumping past what you are
  reading.
- **Section titles** light up as now when their section plays.
- **Before the start:** a quiet line above the chart, "Play G#m7 to
  start" (the song's first chord), and the first chord outlined.
- **The toolbar** shows "Following: Chorus · chord 3 of 8" instead of the
  bar counter.

## 5. Per song: chords or tempo

Each song's sections follow **My chords** (new, the default) or **The
tempo** (as now: Play counts bars at the song's BPM, with the count-in and
the backing track). A choice on the song's settings (next to its tempo),
saved with the song. Following needs a chart with at least two readable
chords; a song without is shown "Add chords to the chart to follow them"
and uses the tempo mode as now.

In "My chords" mode:

- Following is armed whenever the song is the current song; nothing to
  press.
- Choosing another song, Panic and a patch change start that song's map
  fresh (waiting for its first chord).
- After the last chord, following stays there until another section starts
  (by rule 3 or 4) or the song changes.
- The backing track keeps its own Play/Stop (not synced to your playing in
  version 1).

## 6. Errors and edge cases

- A chart edited while following: the map is rebuilt; following stays on
  the step whose chord is at the same place in the chart if it still is,
  else waits at the start of its section.
- A patch without the section's instruments (a channel deleted): as now,
  that section plays what it can; nothing crashes, the section row shows
  it.
- No MIDI keyboard: following waits; clicking a section or the pedal still
  moves it.
- A chord the parser does not know in the chart: not a step, and the
  chart shows it dimmed and struck through, so it can be found and fixed
  in the editor.
- Every failure is said and logged, never silent (as everywhere).

## 7. Testing

- **Chord names:** a table of names to root, family and bass (`G#m7`,
  `Bbmaj7/D`, `Csus`, `E5`, `F#m7b5`, `Dadd9`, `C(no3)`, `N.C.`, rubbish).
- **The song map:** steps from real pasted charts (repeats expanded, twins
  merged, unreadable chords skipped, a one-chord section's first two).
- **The follower, with recorded playing** (note sequences with timings,
  no audio): each rule, and each way of playing: inversions; a bass note,
  octave, fifth or three notes in the left hand; a broken chord over half
  a second; the pedal; sus, power chord and wrong third for a m7; a wrong
  note; one stray chord never jumping; two chords of the chorus jumping;
  holding a chord not moving; twins; the pedal's next section; starting
  mid-song at a section.
- **On the note:** the real engine with two instruments in two sections;
  the chord that enters the chorus sounds on the chorus's instrument, and
  the verse's instrument receives nothing new, measured on the channel
  meters.
- **The screen:** the document controller's highlight position; a QML
  smoke test that the lit chord and the "Play ... to start" line show.
- The gate as always: lint on changed lines, every test, and a soak with
  following on.

## Not in version 1

- Working out which song is being played (you pick the song).
- Following the tempo from your playing, and a backing track kept in time
  with you.
- Listening through a microphone (audio chord recognition); MIDI only.
- Jumps into the middle of a section.
