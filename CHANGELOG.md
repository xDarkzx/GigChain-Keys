# Changelog

What changed in each version of GigChain Keys, newest first. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions
are numbered as described in [docs/RELEASING.md](docs/RELEASING.md).
Downloads for every version are on the
[Releases](https://github.com/xDarkzx/GigChain-Keys/releases) page.

## [Unreleased]

### The song's timeline

- **Songs run on a timeline:** Play (or Space, your keyboard's Play/Stop
  buttons, a pedal, or optionally two quick presses of the sustain pedal)
  runs the song at its tempo along its **flow** (the chorus twice when the
  Flow bar says so). Instruments change at each part, the current chord
  lights in the chart and Perform's tile shows how far through the part you
  are.
- **Live controls on the bar line:** Next part (**N**, a tile), Repeat this
  part (**Shift+N**), Hold this part (**H**), Stop at its end
  (**Shift+Space**); pedals can learn them too. What is queued shows by the
  song display.
- **Chord follow** is now the option for free-time songs (Tempo… > Follow
  the chords I play). Songs from older files with a tempo run on the
  timeline; songs without one still follow their chords.
- On the timeline the same chord twice in a row is two chords, each lit in
  its own place.
- Pedal names: "Next part" / "Previous part" were the sounds: now "Next
  sound" / "Previous sound".

## [0.2.0] - 2026-10-05 (alpha)

### Charts

- **The chart edited as cells.** Click a line: each word has a chord box
  above it (a bar where there is none yet). Type a chord in a box (Tab: the
  next word), click a chord to change it, drag chords between words and
  lines, double-click a word to change it. Chords are always drawn over
  their word.
- **Pasted chords land on their words.** A chord a chord site placed a
  letter or two into a word goes on the word's start.
- **A paste is one undo step** (the chart, the song's name, key, tempo and
  time together). The separate "Undo paste" bar is gone: Ctrl+Z does it.
- The chart scrolls with the mouse wheel again.

### Playing

- **How to play a chord:** tap a chord on stage (or right-click it in the
  editor, or tap its name in Practice): a keyboard with a dot on each key to
  press, ringed blue for the left hand and gold for the right, its notes by
  name, and its inversions. Keep the one you like for the song.
- **The song's flow:** the Flow bar over the chart sets the order the song
  is played in (a chorus three times, a verse after a verse…). Perform's
  part tiles and Practice follow it.
- **Chord follow keeps to the flow:** it only moves forward (the next chord,
  one missed chord caught up, the next part's opening). It no longer jumps
  to another part that opens with the same chords.

### Sounds

- **All together or one at a time:** each sound now plays **every**
  instrument together by default (layers: piano, pad and synth on every
  chord); mute, solo or the song's sections choose per part. Or switch it to
  **One at a time** (the layers button in the top bar): only the selected strip
  plays, and clicking another switches at once, held notes ringing on.
- **Fixed:** in a song with chart sections, only the first instrument played
  in every section not set up by hand. Every instrument plays there now.
- A strip that would be silent if played now is dimmed, and says why on
  hover (muted, another soloed, not in this section, one at a time).

### Keyboard shortcuts

- **Space plays and stops the song** (its count and backing track; in
  Practice, play and pause). Next sound is **→**, as before.
- **↑ / ↓** change songs; **N** next section, **T** tap tempo, **C** click,
  **M** mute everything, **P** panic.
- Loop station: **R** records on the selected channel, **L** plays or stops
  its loop, **Shift+L** stops them all.
- A clicked song: **Delete** removes it, **F2** renames it, **Ctrl+D**
  duplicates it; **Ctrl+Shift+N** adds one. A clicked channel strip:
  **Delete** removes the channel. Never in Perform; Ctrl+Z brings it back.

### Practice

- **Left hand:** the bass note, an octave, root and fifth, or the full chord.
- **Right hand:** smooth, root position, or the inversions you chose.

### Fixed

- A MIDI clock test failed on machines that list a MIDI output they cannot
  open (no sound hardware); it now says so and skips.
- Perform's part tiles lit one too early after a part with no chords (a
  spoken intro).
- Tapping a part tile goes to that very part (the second chorus, or the
  chorus being played again), not the next time that section comes round.
- Renaming a section keeps its place in the flow and its instruments; a flow
  part whose section is gone from the chart is shown dimmed with a "?" and
  dropped at the next change to the flow.

## [0.1.0] - 2026-10-05 (first public alpha)

The first version for players to try and test: Windows 10 and 11 (64-bit),
and Apple Silicon Macs. An alpha: please report what works and what breaks.

### Sounds

- Plays the **VST3 instruments and effects** on your computer, found by
  themselves in the usual VST3 folders, each with its own window.
- **Instruments list** with each maker's artwork, search, favourites,
  ratings and hiding; details for every plugin (maker, version, website).
- **Mixer:** channel strips with an instrument, effects (bypass, replace,
  remove), pan, fader, meters, mute and solo; a **master strip** with its own
  effects; a **safety limiter** before your speakers.
- **Audio inputs:** a mic or a guitar through its own channel and effects.
- **Keyboard zones** (splits, transpose, MIDI channel) and **velocity layers**.
- **Knobs:** learn any knob, fader or pedal on your keyboard to any plugin
  setting, per song.

### Setlists and songs

- A **setlist** holds the night's songs, each with its own sounds, saved as
  one file; the last one can open at start, with every sound ready.
- **Instant song changes:** every sound is loaded up front; held notes and
  reverb tails ring on across a change.
- Change songs from the keyboard, **pedals and pads** (learned in Settings),
  or MIDI Program Change.
- **Undo and redo** for every edit.

### Charts

- **Chord charts:** paste a song from any chord website (cleaned up and
  placed over the words), import a chord sheet (ChordPro, OnSong and text),
  or type it in.
- **Edit where you read:** click above a word to add a chord, drag chords
  onto words, a chord palette, sections you add and rename in place, and the
  raw ChordPro text when you want it.
- **Sections that change the sound:** each part of the song picks which
  instruments play; they change by themselves, counted in bars or **following
  the chords you play**.

### On stage

- **Perform mode:** full screen, the chart big and clear (A−/A+ text size),
  the song's parts as tiles, Panic always at hand.
- **Tempo** per song, tap tempo, a **click**, **MIDI clock** in and out.
- **Backing tracks** (WAV, MP3, FLAC, M4A, AAC, OGG, AIFF) per song, in time
  with the song's sections.
- **Loop station:** a loop per instrument with layers, in time with the song
  or free; a loop shorter than the set length plays over and over to fill it,
  and what rings on past the end carries smoothly over the start. Record,
  play and clear from buttons and pedals on your keyboard.

### Practice

- **Practice mode:** the song's chords fall as glowing notes onto a piano
  keyboard, played the way a pianist would (bass note in the left hand,
  the chord in the right). **Listen**, **Play along** or **Wait for me**;
  slow it down to 25%; loop a section. Keys light up and turn green or red
  as you play.

### Help

- A **user guide** inside the app (**F1**): getting started, every part of
  the app, shortcuts and troubleshooting, with search. The same pages are
  in [`docs/help`](docs/help/README.md).
- **About** shows the version, the licence and the credits.

### Built not to fail

- Plugins are scanned in a separate process, and remembered until they change
  (start-up in moments, not seconds).
- A plugin that crashes while loading is switched off next time, and listed
  in Settings to try again.
- An unplugged MIDI keyboard or audio interface comes back by itself when
  plugged in again.
- A crash saves a report and says so at the next start.
