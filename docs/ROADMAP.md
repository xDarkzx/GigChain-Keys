# Roadmap

Plans for GigChain Keys beyond what works today (see the README). None of
this is built yet. Each item lists what is decided and what is still open;
the open questions get answered when the work starts.

The order is a suggestion. Items marked **shared** need the same building
block, so they are best built in that order.

---

## 0. Parity first

Players will not move their live rig to an app that does less than
MainStage or Gig Performer, however good the extras. So the basics they
rely on come before the new ideas below. Audited against the code on
2026-09-27:

| What players need | Status | Where it stands |
|---|---|---|
| Never crashes, never drops out | **Ahead** | Plugins that crash while loading are switched off next start; the plugin scan runs in its own process; plugin state calls cannot end the app; a limiter guards the output. A plugin crashing *while playing* still ends the app (as in both rivals). |
| Instant patch changes | **Done** | Every sound in the setlist is loaded up front; switching swaps the patch in one audio block. |
| Held notes and tails carry over a patch change | **Missing** | An instrument not in the new patch stops at once: held chords and reverb tails are cut. Both rivals let them ring out. |
| Splits and layers | **Partly** | Key range, transpose and MIDI channel per instrument work in the engine and are saved, but no screen sets them. No velocity ranges. |
| Keyboard knobs and faders control plugin settings | **Partly** | Sustain, mod wheel, pitch bend, expression and aftertouch reach every plugin through its own MIDI mapping. Assigning a knob to any plugin setting, per patch, is missing (MainStage's screen controls, Gig Performer's widgets). |
| Songs and patches switched from the keyboard | **Done** | Learnable buttons/pedals for next/previous song and patch and panic; Program Change picks the patch. |
| Tempo | **Missing** | Songs store a tempo, but plugins are always told 120 BPM: synced arps and delays run at the wrong speed. No tap tempo, no MIDI clock. |
| Backing tracks and click | **Missing** | |
| Audio inputs (vocal or guitar through effects) | **Missing** | The audio device opens outputs only. |
| Sounds out of the box | **Missing** | The app plays only plugins the user already owns. |
| Undo | **Partly** | Only the last chart paste can be undone. |
| Plugin formats | **Partly** | VST3 only. Gig Performer also hosts VST2 (many older Windows plugins); an open-source VST2 host needs care, since Steinberg no longer licenses the VST2 SDK. |
| Charts (chords and lyrics on screen) | **Ahead** | Built in; neither rival has them. |
| Master effects, meters, limiter | **Done** | |

**Closing the gaps, most important first:**
1. **Tempo:** the song's tempo reaches plugins, plus tap tempo. Small, and
   synced sounds are wrong until it is done. Also the base for 2 and 3.
2. **Carry-over:** held notes and tails of the old patch ring out after a
   change.
3. **Split and layer editor** for the existing key range and transpose,
   plus velocity ranges.
4. **Knob/fader to plugin setting**, per patch (learn by moving both).
5. **Bundled sounds:** open-source instruments under the same GPL licence,
   e.g. Surge XT (synth) and Dexed (FM), and a free sampled piano, so a new
   user can play the moment it installs.
6. **Audio inputs** as channels (mic, guitar) with effects.
7. **Backing tracks and click.**
8. **Undo** for all editing.
9. **MIDI clock** in and out.
10. **VST2**, if it can be done within the licences.

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
- **Shared:** Mac and Linux versions of the app (7).

## 5. Charts that follow the song

Charts are editable ChordPro with chords placed over the words, and Perform
shows the whole song. Next: follow the place in the song from the chords
being played (MIDI) and the lyrics, and scroll along.

## 6. Installer

Build the app as an .exe plus module DLLs and package it into a Windows
installer, as Audacity 4 does. The core, engine and ui modules are kept
separate for this. With 7: a macOS app bundle and Linux packages.

## 7. Windows, macOS and Linux

**The goal:** the same app on all three. It is Windows-only today, but most
of it is already built on cross-platform parts: Qt, RtAudio, RtMidi and the
VST3 SDK all run on macOS and Linux, and core (setlists, charts) has no
Windows code at all.

What is Windows-only now, and what each needs:
- **Audio:** WASAPI and ASIO. RtAudio also speaks Core Audio (macOS) and
  ALSA, PulseAudio and JACK (Linux); the device settings must offer those.
- **MIDI:** RtMidi's Windows backend; the macOS and Linux backends come with
  it.
- **Plugin windows:** embedded by their Windows handle (HWND). macOS needs
  an NSView and Linux an X11 window, each with its own resizing and focus
  rules.
- **Plugin folders:** the standard VST3 folders differ
  (`/Library/Audio/Plug-Ins/VST3` and `~/Library/...` on macOS;
  `~/.vst3` and `/usr/lib/vst3` on Linux).
- **Plugin artwork:** `PlugIn.ico` is the Windows folder icon; macOS bundles
  carry their icon inside the bundle.
- **Crash safety:** crash reports (minidumps), the no-error-dialog set-up
  and the out-of-process scanner's window flags are Windows code; each needs
  its macOS/Linux counterpart.

**Open:**
- Audio Units on macOS: many Mac players have plugins as AU; is VST3 enough
  there?
- Build and test on real Macs and Linux machines (CI), since the gate's
  real-plugin tests need each system.
