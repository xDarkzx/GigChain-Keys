# Setlists and songs

A **setlist** is your night: the songs in the order you play them, each with
its own sounds, chart and tempo. It is saved as one file
(*name*.gigchain.json) that you can copy to another computer or keep as a
backup.

## Start, open and save

- **File > New** (**Ctrl+N**) starts an empty setlist.
- **File > Open…** (**Ctrl+O**) opens one; **File > Recent setlists** lists
  the last ones you used (the start screen shows them too).
- **File > Save** (**Ctrl+S**) and **Save As…** (**Ctrl+Shift+S**) keep it.
  The dot before the name in the top bar means there are unsaved changes.
- In **Settings > General**, **Open the last setlist I used** makes the app
  start straight into your setlist with every sound loaded: handy on a gig
  night.

## Songs

The **Setlist** panel on the left lists the songs.

- **Add a song:** click **+ Song** at the bottom of the panel.
- **Choose a song:** click it. Its sounds switch in at once (every sound in
  the setlist is loaded up front, so there is no wait).
- **Rename:** double-click it, type, press **Enter**.
- **Reorder:** drag it up or down.
- **More:** right-click a song for **Tempo…**, **Backing Track…**,
  **Duplicate** and **Delete**.

A quick way to fill a setlist: copy a song's chords and lyrics from a chord
website and paste them into an empty setlist's chart (**Ctrl+V**). See
[Chord charts](charts.md).

## Moving between songs

| Key | Does |
|---|---|
| **↓** / **↑**, or **Page Down** / **Page Up** | Next / previous song |
| **→** | Next sound (then the next song) |
| **←** | Previous sound |
| **Delete** (a song clicked) | Delete it (**Ctrl+Z** brings it back) |
| **F2** / **Ctrl+D** (a song clicked) | Rename it / duplicate it |
| **Ctrl+Shift+N** | A new song |

Pedals and pads can do the same: see [Sound and your keyboard](audio-and-midi.md).

## Hardware synths (external gear)

Playing a hardware synth too (a Nord, a Prophet, a module in the rack)?
Right-click a song, **External Gear…**, and **Add a synth**: choose the MIDI
output it is on, its channel, the **Program** (1-128) and, if the synth wants
one, its **Bank**. When the song comes up, the synth is sent that sound, so
the computer and the hardware change together. A change is sent at once, so
you can hear it while you choose. Up to four synths per song.

If the synth is not plugged in, the app says so and carries on; the sound
goes out next time the song comes up with it there.

### Playing a hardware synth from your keyboard

A channel can play a synth (or a sound module with no keys of its own)
instead of, or as well as, a plugin. Right-click its strip, **Play Hardware
Synth**, and choose the synth's MIDI output, then **On MIDI Channel** if the
synth listens on another channel than 1. The channel's split, transpose,
velocity range, pedal choices, chord trigger and arpeggiator all apply: a
module can take the left hand while a plugin plays the right. To hear the
synth through the app (its effects, its fader, the aux reverb, the
recording), give the same channel its audio input with **Play Audio Input**.
Muting the channel stops new notes; **Panic** also stops the synths.

## Undo

Every change to the setlist can be undone with **Ctrl+Z** and redone with
**Ctrl+Shift+Z** (or the arrows in the top bar). A fader drag counts as one
step.

## What a song holds

- **Its sounds:** the instruments and effects in the mixer while it is
  chosen. See [Instruments and effects](instruments.md).
- **Its chart:** the chords and lyrics. See [Chord charts](charts.md).
- **Its sections:** which instruments play in the verse, the chorus…
  See [Sections, tempo and backing tracks](sections-and-tempo.md).
- **Its tempo, time signature and backing track.**
