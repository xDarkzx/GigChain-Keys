# Practice tab: falling notes from the song's chart (step 1)

A third tab beside Chart and Instrument: the song's chords as notes falling
onto a keyboard, Synthesia-style. It plays itself through the patch's own
instrument, or waits for the player, at any speed, looping any section.
It works from any chart: pasted, imported or written by hand.

Later steps (not this one): recording or importing a MIDI file for intros
and melodies (step 2), syncing with the song's backing track (step 3), and
a YouTube link beside it (step 4).

## What the player sees

```
┌ Practice ───────────────────────────────────────────────────────────┐
│ ▶ Play  ■  [Listen | Play along | Wait for me]  Speed 75%  Loop: [Chorus ▾] │
│                                                    Now: Gm   Next: F    │
│   Gm                                                                    │
│   █  █   █                       ← notes fall, the chord's name beside  │
│                                                                         │
│      F                                                                  │
│      █ █  █                                                             │
│ ───────────────────────────────────────────── the line they land on    │
│ ┃ ┃█┃ ┃ ┃█┃ ┃ ┃ ┃█┃ ┃  keyboard: the notes to play lit, what is       │
│ ┗━┻━┻━┻━┻━┻━┻━┻━┻━┻━┛  pressed green (right) or red (wrong)             │
└─────────────────────────────────────────────────────────────────────────┘
```

- **Notes fall onto the keys** they belong to and land at the moment to
  play them. Each note's length is its bar. Left hand (bass) and right hand
  (the chord) are two colours. The chord's name sits beside each chord as it
  falls.
- **Now / Next:** the chord to play and the one after, big, top right.
- **The keyboard** shows the notes to play now lit in the hand colours. A
  key pressed on the player's keyboard is lit green when it is one of them,
  red when not.
- **Section marks:** a thin line with the section's name across the
  waterfall where a section starts.

## How a chart becomes notes

- **Order and length:** the chords in playing order (the chord-follow song
  map, with repeats played out). Each section's length in bars, as set in
  the chart or guessed from it, is shared evenly between its chords. Four
  bars and four chords is a bar each; four bars and eight chords is half a
  bar each. Chords before the first section get a bar each.
- **Time:** the song's tempo and time signature (120 BPM and 4/4 when not
  set). A count-in bar comes before the first chord.
- **Voicing:** the left hand plays the bass note (the root, or the slash
  bass of "D/F#") in the octave from C2 to B2. The right hand plays the
  chord's notes (up to four: a fifth is left out first, then the highest
  extension) in the inversion closest to the previous chord's, between C4
  and C6. The first chord starts nearest to middle C. This is how a pianist
  moves between chords: the fewest jumps.
- A chord the app cannot read is skipped (it is dimmed in the chart
  already).

## Modes

- **Listen:** it plays the song through the patch's instrument (as if the
  player's keyboard played it) while the notes fall.
- **Play along:** the notes fall at the tempo and the player plays. Hits
  and misses are shown on the keys.
- **Wait for me:** each chord waits at the line until every one of its
  notes is held, then goes on.
- **Speed:** 25% to 100%, in 5% steps.
- **Loop:** the whole song or one section, round and round.
- **Play / Pause / Stop,** and Stop goes back to the start (of the loop).

## Where it lives

- `core/Practice`: `practiceTimeline(SongMap, bars per section, beats per
  bar)` and `voiceChord(ChordShape, previous)`, both pure and tested.
- `ui/PracticeController`: the timeline for the current song, the clock
  (about 60 updates a second), playing the notes through
  `IEngine::injectNote`, and reading the held keys from
  `IEngine::keyboardActivity`. It is tested with the spy engine.
- `PracticeView.qml`: the waterfall and the keyboard, in the main area's
  third tab. It is tested in the QML smoke test with screenshots.
- Notes it plays are always let go: on pause, stop, a song change, or
  leaving the tab.
