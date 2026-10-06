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

There are two ways, and you pick one per song: right-click the song in the
setlist and choose **Tempo…**.

### Follow the tempo (bars)

Set the song's **beats per minute** and **time signature**. Then press **Play**
(the ▶ next to the section display in the top bar). The song counts its bars,
and at the end of each section the next one's instruments come in. The display
shows where you are (*Chorus · 3/8*). Press it again to stop.

**Change sections a beat early** helps pads that swell in slowly.

### Follow the chords I play

Switch on **Sections follow the chords I play**. Now there is no counting:
the app listens to the chords you play and moves through the song with you.
The display says **Play C to start**, then follows. It needs chords in the
chart.

It follows the song's **flow**: the order the song is played in, set in the
**Flow** bar over the chart (see [Chord charts](charts.md)). It only ever
moves **forward** along it:

- **The next chord** you play moves it on.
- **A missed chord** is caught up: play the chord after the next and it
  follows (within the part you are in).
- **The next part:** play the next part's first two chords clearly (you cut
  the end of a section short) and it goes there.

It never jumps back, and never to another part that happens to open with the
same chords (common: verse, pre-chorus and chorus often start alike). If it
ever loses you, a pedal (**Settings > MIDI > Pedals and pads**) or a tap on a
section's title puts it right: it goes to the next time the flow comes to that
section. A tile in Perform goes to that very part (the second chorus, say).

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

See also: [Chord charts](charts.md), [Perform mode](perform.md).
