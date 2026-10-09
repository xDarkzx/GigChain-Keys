# Splits, layers and knobs

When a song has more than one instrument you decide **which keys** each one
plays and **how hard** you must play to hear it. And the knobs and sliders on
your keyboard can move any setting of any plugin.

## All together, or one at a time

Each sound has a **play mode**, switched with the **layers** button in the top
bar (next to **Keys** and **Mixer**; hover over it to see which mode is on). It
shows three layers for all together, one layer, lit, for one at a time:

- **All together** (the usual): every instrument plays on every note. A piano,
  a pad and a synth stack into one big sound. Mute or solo strips to choose,
  and let the song's sections change it per part (piano alone in the verse,
  the synth in the chorus: see [Sections, tempo and backing tracks](sections-and-tempo.md)).
- **One at a time:** only the **selected** strip plays. Click another strip
  to switch: the change is instant, and notes you are holding ring on.

A strip that would be silent if you played now is **dimmed**. Hover over it
to see why: *Muted*, *Another channel is soloed*, *Not in Chorus*, or
*One at a time*.

## Keyboard zones (splits)

Right-click a channel strip and choose **Keyboard Zone…**.

- **Lowest key** and **Highest key:** the keys this instrument plays. Bass on
  the left hand and piano on the right: set the bass to *C1–B2* and the piano
  to *C3–C7*.
- **Transpose:** shift it up or down in semitones (+12 is an octave up). Handy
  to play a bass line an octave higher than it sounds.
- **MIDI: Listens to:** *All channels*, or one channel only when your
  keyboard sends its zones on different channels.
- **Whole keyboard** resets it.

The strip then shows where it plays, for example *C2–B3 +12*.

## Chords and arpeggios from one key

In a channel's **Keyboard Zone**, under **MIDI effects**:

- **One key plays:** a major, minor, power (fifth and octave), sus2, sus4 or
  seventh chord, or octaves, built on the key you press. Big stabs and
  pads from one finger.
- **Arpeggiator:** hold keys and they play one after another, **Up**,
  **Down**, **Up and down** or **As played**, every **1/4**, **1/8**,
  **1/8 triplet** or **1/16** at the song's tempo, over **1 to 3 octaves**.
  The first note plays as you press; the rest land on the beat. Let go and it
  stops.

Both together arpeggiate the chord. They are kept per sound, like the zone.

## Velocity layers

Two channels on the **same keys** with different **How hard (velocity
layer)** ranges make a layer: play softly and you hear one, play hard and you
hear the other. For example a soft pad at velocity *1–80* and a brass stab at
*81–127*.

## Knobs, faders and wheels

Right-click a channel strip and choose **Learn a keyboard knob for a setting
of …**: move the setting in the plugin's own window, then the knob on your
keyboard. Done. Or choose **Knobs…** to pick the setting from a list:

1. Click **Learn a knob**.
2. Pick the setting it should move: choose the plugin, then search its
   settings (*Cutoff*, *Reverb mix*, *Drawbar 1*…). Or click **Learn** and
   move the setting in the plugin's own window.
3. Move a knob or slider on your keyboard. Done.

**Mapped knobs** lists what each knob moves; **✕** removes one. Knobs are kept
per song, so the same knob can be a filter in one song and a leslie speed in
the next.

Each knob there also has:

- **A curve:** **Straight**, **Gentle start** (fine control at the bottom,
  the way an expression pedal should feel) or **Quick start**.
- **Pickup** (on unless you turn it off): when you change song, the knob on
  your keyboard is rarely where the setting is. With pickup, turning it
  changes nothing until it reaches the setting, then it takes over, so the
  sound never jumps. The mixer's learned fader and pan knobs pick up too.

A channel's **fader** and **pan**, and the **master**, learn a knob from
their own right-click menu (see [Sound and your keyboard](audio-and-midi.md)).

## The pitch wheel, mod wheel, sustain and expression

These go to every instrument that is playing, as do breath, foot pedal and
expression (CC 2, 4 and 11). The on-screen keyboard (the
**Keys** button) shows **Pitch**, **Mod** and **Sus** as they move.

To keep one away from an instrument, open its **Keyboard Zone** (the
channel's menu) and untick it under **Takes**: **Sustain pedal**,
**Expression pedal**, **Mod wheel**, **Pitch bend** or **Aftertouch**. The
classic case is a piano and strings layered together: untick the sustain
pedal for the strings, and the piano holds while the strings stop when you
lift your hands. Or let only the lead synth take the pitch bend.

See also: [Instruments and effects](instruments.md) and
[Sections, tempo and backing tracks](sections-and-tempo.md), which change
the instruments as the song goes.
