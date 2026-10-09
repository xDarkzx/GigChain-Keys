# Sections, tempo and backing tracks

A song rarely uses the same sound all the way through: maybe just a piano in
the verse, strings joining in the chorus, a synth lead in the solo. In
GigChain Keys the song's **chart sections** decide this, and they can change
**by themselves** as the song plays.

## What each section plays

In the **Chart** tab, under each section's title, there is a chip for every
instrument that plays in it. A section you have not touched plays what the
sound's **play mode** says (the **layers** button in the top bar): **All together**, every
instrument at once (piano, pad and synth on every chord), or **One at a
time**, the selected one. So to have the piano alone in the verse and the
synth join in the chorus: take the pad and synth out of the verse (**✕**),
and leave the chorus as it is.

- **✕** on a chip takes that instrument out of the section.
- **+** adds another instrument of the song (one that is already in the mixer).
- No chips means the section is **Silent** (a break).
- The number of **bars** is next to them. It is guessed from the chords;
  click it to type the real length.

**Click a section's title** to switch to it now: its instruments come in and
the others fade out. Held notes and reverb tails ring on naturally.

## Let the song change by itself

The song runs at its **tempo** along its **flow** (the Flow bar's order: a
chorus played twice is played twice), like the playback rigs bands use with a
click (MainStage, MultiTracks Playback, Prime). Set the song's **beats per
minute** and **time signature** (right-click the song in the setlist >
**Tempo…**) and each section's **bars** (click the number under its title).
Then start it:

- **Play** (the big ▶ in Perform, ▶ in the top bar) or **Space**;
- your keyboard's own **Play / Stop** buttons (on by default: **Settings >
  MIDI > Starting the song**);
- a pedal or pad learned to **Song / backing track: play / stop**;
- or, if you switch it on, **press the sustain pedal twice quickly** (each
  press still sustains).

At each part the next one's instruments come in, the current chord lights in
the chart and the chart scrolls with you. Perform's tile for the part playing
shows how far through it you are. The display shows *Chorus · 3/8*.

**Live controls** change the song as the band plays it. They happen on the
**next bar line** (or when the part ends), so nobody falls out of time; press
again to take it back. What is coming shows beside the display (*→ Bridge*,
*Repeat*, *Hold*).

| Control | Perform | Keys | Pedal or pad |
|---|---|---|---|
| **Next part** (cut this one short) | **Next part**, or tap a part's tile to go to that part | **N** | Next part of the song |
| **Repeat this part** once more | | **Shift+N** (twice: two more) | Repeat this part once more |
| **Loop this part** (it plays again and again until you press again) | **Loop part** | **H** | Hold this part |
| **Stop at the end of this part** | | **Shift+Space** | — |

Stopped, **Next part** and the tiles choose where **Play** starts.

**Change sections a beat early** helps pads that swell in slowly.

A chart **without section titles** plays too: the whole song is one part, a
bar per chord (add titles like *Verse* and *Chorus* to change sounds at each
part, and set their bars).

### A song without a steady tempo

Leave it stopped and move it on yourself, the way MainStage players do: a
pedal learned to **Next part of the song** (**Settings > MIDI > Pedals and
pads**), **N**, or a tap on the part's tile in Perform. Each part's
instruments come in as you get there. (The app does not listen to what you
play to guess where you are: a verse and a chorus often share their chords,
so a guess would jump to the wrong part.)

## Tempo, tap and click

- The **BPM** display in the top bar shows the tempo. Plugins with tempo-synced
  delays or arpeggiators follow it.
- **Tap** sets the tempo from your taps (a pedal can tap too: see
  [Sound and your keyboard](audio-and-midi.md)).
- The **metronome** button switches the click on and off.
- A song's tempo (right-click the song > **Tempo…**) is set whenever the song
  is chosen. *0* means "leave the tempo as it is".

## Backing tracks

Right-click a song and choose **Backing Track…** to give it an audio file
(*WAV, MP3, FLAC, M4A, AAC, OGG, AIFF*). The top bar then shows its play
button and where it is. A song with sections starts the backing track with its
own **Play**, so the sound changes stay in time with it.

**Remove Backing Track** in the same menu takes it off.

### Stems: more tracks, each to its own outputs

A song can play up to eight more tracks with its backing track: a click, a
guide vocal, the drums on their own. Right-click the song, **Stems…**, and
**Add stems…**. They play together from the same place, and each has its own
level, mute (**M**) and outputs: send the click and the guide to outputs 3-4
for your in-ears and leave them out of the main mix. Stems on outputs of their
own skip the master fader, so turning the band down never takes the click
away. A level, mute or output change is heard at once.

The track and stems are all held in memory, up to 45 minutes of audio per song
in all (eight stems of a five-minute song). Past that, the app says which files
were left out.

### Markers

The flag button next to the track's time lists the song's markers: choose one
to jump there, playing or not. **Add Marker at…** marks where the track is now
(name it *Chorus 2*, *Outro*…); **Remove Marker** takes one off. Markers are
saved with the song.

See also: [Chord charts](charts.md), [Perform mode](perform.md).
