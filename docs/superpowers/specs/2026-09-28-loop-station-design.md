# Loop station and headphone monitoring

Status: design, awaiting review (2026-09-28)

## What the player gets

A street performer's loop pedal built into the app. Above the mixer, a
horizontal looper strip gives each instrument channel two buttons: record
a phrase, loop it, and keep building (an arp looping, a pad under it,
piano played live on top). A headphone output lets the player record and
try things privately while the audience hears only what is meant for them.
Every looper action is also a pedal/keyboard button and, later, a tap on
the iPad remote.

## 1. Loops

- **One loop per channel**, recording that channel's own sound (its
  instrument and effects, before its fader), taken inside the app: no
  round trip through the audio interface, so no latency to correct.
  Audio-input channels (a mic, a guitar) loop too.
- **Length**
  - **Sync on** (default, saved per song): recording starts on the next bar
    line (the click counts in if it is on) and ends on the bar line after
    the second press. Loops are whole bars at the song's tempo and time
    signature, so they stay locked to each other, the click, tempo-synced
    plugins, the backing track and the song's sections.
  - **Sync off** (pedal style): the first press records at once, the second
    closes the loop where it is. The first loop sets the length; every
    later loop is rounded to a whole multiple or fraction of it (1/4, 1/2,
    1, 2, 4...). Optional (Loops menu): **take the tempo from the first
    loop** (assuming 1, 2 or 4 bars, whichever lands nearest 60-180 BPM).
- **Layers**: Record on a playing loop records a layer on top of it
  (overdub), until pressed again (synced: to the end of that pass). **Undo
  layer** removes the last one (and the one before, up to 8 deep).
- **Start and stop**: each loop starts and stops on its own (synced: on the
  next bar, so a drop lands in time). A stopped loop keeps its recording.
- **Clear**: on screen, right-click or long-press ⟳ → Clear; on pedals or
  keyboard buttons, hold Record and Loop together (as on loop pedals); the
  Loops menu clears one channel's or all.
- **Life of a loop**: loops belong to the song. They keep playing through
  section changes (even when the section mutes their channel's live
  playing) and patch changes within the song, and after the song's last
  section (an outro) until stopped. They stop when the song's Stop is
  pressed, and they stop and are cleared when another song is chosen.
- **Limits**: a loop is at most 64 bars or 2 minutes; recordings live in
  memory only (not saved with the setlist). Running out of room stops the
  recording and says so.

## 2. Headphones and monitoring

- **Headphone output** (Settings > Audio): "Same as the main output" (no
  separate headphones), another output pair of the same interface
  (outputs 3-4...), or a second device (e.g. the laptop's headphone jack;
  the app keeps the two devices' clocks in step, adding a few ms to the
  headphones only). The Scarlett Solo alone has one mix, so its headphones
  hear what the house hears; a second device gives separate headphones.
- **Live / Monitor** (toolbar switch, also in the Loops menu):
  - Live: the house and the headphones hear the full mix.
  - Monitor: the house hears nothing from the app; the headphones hear
    everything. For soundcheck and trying things between songs.
- **Cue** (🎧 on each mixer channel strip, beside M and S): that channel and
  its loop go to the headphones only, while everything else carries on to
  the house. Cueing off a loop brings it to the house on the next bar
  (synced). This is how a loop is recorded in private and dropped in.
- **Headphone volume**, apart from the master fader (Loops menu and
  Settings).

## 3. On screen

- **Looper strip**: a horizontal strip across the top of the mixer, lined
  up with the channel strips, shown or hidden from the Loops menu (and
  remembered). Above each channel:
  - **●  Record**: red ring = waiting for the bar; solid red = recording;
    orange = recording a layer.
  - **⟳  Loop**: grey = empty; dim = stopped (has a loop); green = playing,
    with a ring round it filling as the loop plays (like a loop pedal's LED
    circle) and the bar count ("2/4").
  - A new pair of icons drawn to match the app's icon set (record,
    loop, headphones; undo, stop-all and clear in the menu).
- **Loops menu** in the toolbar next to File: Sync to tempo, Tempo from
  first loop, Stop all, Clear all, Undo last layer (of the selected
  channel), Show looper strip, Live/Monitor, headphone volume.
- **Toolbar**: a Live/Monitor switch that is impossible to miss when on
  Monitor (the house is silent).
- Perform view: the looper strip shows above its mixer too.
- **With the mixer hidden** (only the setlist and chart showing), a small
  **loops pill** in the toolbar tells what is going on without getting in
  the way. It shows only while a loop exists: "⟳ 2" green = two loops
  playing, a small red dot = something is recording, dim = loops recorded
  but stopped. It changes colour, never blinks or pops up. Clicking it
  opens a small list of the loops (channel name, progress ring,
  play/stop) with Stop all.

## 4. Controls and remote

- Learnable actions (Settings > MIDI): Record, Loop play/stop and Cue for
  the selected channel; Stop all; Undo layer; Live/Monitor. Holding the
  Record and Loop controls together clears.
- All looper actions go through one command interface (in the document
  layer), which the screen, pedals and the future iPad remote use alike.

## 5. Not in this version

- Saving loops with the setlist, exporting them as audio files.
- Several independent loops per channel, reversing, half speed.
- MIDI (note) loops.

## 6. Tests (end to end, measured)

- Engine: synced record starts and ends on the exact bar samples; the loop
  plays back bit-exact what the channel produced; overdub sums; undo
  restores; free mode sets length and rounds later loops; start/stop on the
  bar; loops keep playing across a patch change and a section switch;
  stop on song stop, cleared on song change; memory limit reported; no
  allocation on the audio thread while recording or playing.
- Monitoring: house and headphone buses carry the right mix in Live,
  Monitor and cue; second-device output with drift correction keeps within
  a few ms over minutes (measured).
- UI smoke: strip lines up with the channel strips, icons change state,
  Loops menu items act, Monitor warning shows.
