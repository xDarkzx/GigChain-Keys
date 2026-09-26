# Roadmap

Plans for GigChain Keys beyond what works today (see the README). None of
this is built yet. Each item lists what is decided and what is still open;
the open questions get answered when the work starts.

The order is a suggestion. Items marked **shared** need the same building
block, so they are best built in that order.

---

## 1. Instrument toggles and song sections

**The idea:** within one song, switch sounds for the verse, chorus, drop and
so on without building a separate patch for each, and turn instruments on
and off with one easy toggle.

- A patch already layers several instruments (they all play together). What
  is new is choosing, per instrument:
  - **Always on:** plays whenever the patch is up (today's behaviour).
  - **Toggle:** on or off at the press of one button (on screen, a keyboard
    button or pedal, or the tablet remote).
- **Sections** inside a song (Intro, Verse, Chorus, Drop...): each section
  remembers which instruments are on, and moving to the next section
  switches them in one step, with no gap in the sound (the plugins stay
  loaded; only their volume changes).
- Moving between sections by button, pedal, Program Change or the tablet.

**Open:**
- Do toggles fade in and out (and how fast), or switch instantly?
- Do sections also change volumes and effects, or only which instruments
  play?
- How sections relate to the song chart (a "Chorus" section could follow
  the chart's `{chorus}`).

## 2. Loop station

**The idea:** like a street performer's loop pedal. Record a phrase, let it
loop, and build atmosphere live: an arp looping on one instrument and pads
on another, while playing piano or drums on top.

- A row above each mixer channel strip, hidden until wanted.
- Two buttons per channel: **Record** and **Loop/Play**. **Holding both
  clears** the loop, as on hardware loop pedals.
  - A mouse cannot hold two buttons at once, so the screen needs another
    way to clear (long-press or right-click). Pedals and MIDI buttons can
    hold both.
- Each channel loops on its own.
- Keyboard buttons and pedals can be learned for the looper buttons.

**Open:**
- Record the sound (the plugin's output, like a loop pedal) or the notes
  played (MIDI: the loop follows a patch change to a new sound)?
- Does the first loop set the length for the others, so they stay in time?
- **Shared:** a tempo/clock (with 3).

## 3. Stack presets with built-in sidechain

**The idea:** the lush layered saw stacks on the drops of artists like
Illenium, Slander and Said The Sky, which normally take a lot of setup in a
production DAW. Build a stack once, save it, and play it live.

- A stack is several instruments layered as one sound: detuned supersaws,
  octave layers, sub, noise/air, pads.
- The stack runs through one group with shared effects (multiband
  compression, reverb, width).
- **Sidechain pumping built in:** a ducker that dips the stack's volume in
  time with the beat (the "pump" of EDM drops), synced to the tempo or
  triggered by a kick channel or a MIDI note. No real kick drum needed.
- Saved as a **stack preset** that can be dropped into any song's patch.

**Open:**
- Ship ready-made stacks? Everyone owns different synths, so presets must
  use the player's own plugins (never hardcoded plugin names).
- Where the tempo comes from: tap tempo, MIDI clock or a BPM per song.
- **Shared:** tempo/clock (with 2), channel groups / buses.

## 4. Tablet remote (iPad on the music stand)

**The idea:** leave the computer (PC or Mac) safely out of the way, and put
a tablet on the music stand or piano to run the show. No bulky laptop in
front of the player.

- The tablet shows a simple version of the app, much like the main screen:
  - the setlist, with song and patch switching;
  - instrument toggles and song sections (1);
  - volumes;
  - the loop station (2);
  - the song chart with chords and lyrics, large enough to read on stage.
- The computer does all the sound; the tablet only controls it and shows
  what is happening, over the local network.
- **Live lock:** while performing, the tablet can switch, toggle, set
  volumes and run loops, but cannot add or remove instruments. Loading a
  plugin mid-show can glitch the sound. Adding instruments stays possible
  outside Live mode.

**Open:**
- A web page any tablet opens in its browser (no app store; works on iPad
  and Android), or a real iPad app?
- How the tablet finds and pairs with the computer safely on a venue's
  network.
- Should adding instruments from the tablet be allowed at all (see Live
  lock)?
- **Shared:** a Mac version of the app for "setup on Mac or PC" (the app is
  Windows-only today).

## 5. Charts that follow the song

Charts are editable ChordPro with chords placed over the words, and Perform
shows the whole song. Next: follow the place in the song from the chords
being played (MIDI) and the lyrics, and scroll along.

## 6. Installer

Build the app as an .exe plus module DLLs and package it into a Windows
installer, as Audacity 4 does. The core, engine and ui modules are kept
separate for this.
