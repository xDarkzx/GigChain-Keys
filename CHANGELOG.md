# Changelog

What changed in each version of GigChain Keys, newest first. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions
are numbered as described in [docs/RELEASING.md](docs/RELEASING.md).
Downloads for every version are on the
[Releases](https://github.com/xDarkzx/GigChain-Keys/releases) page.

## [Unreleased]

## [0.1.0] - first public beta (not yet released)

The first version for players to try: Windows 10 and 11 (64-bit), and
Apple Silicon Macs in testing.

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
