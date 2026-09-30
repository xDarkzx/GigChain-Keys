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
rely on come before the new ideas below. First audited against the code on
2026-09-27; updated the same day after the gaps below were closed:

| What players need | Status | Where it stands |
|---|---|---|
| Never crashes, never drops out | **Ahead** | Plugins that crash while loading are switched off next start; the plugin scan runs in its own process; plugin state calls cannot end the app; a limiter guards the output. A plugin crashing *while playing* still ends the app (as in both rivals). |
| Instant patch changes | **Done** | Every sound in the setlist is loaded up front; switching swaps the patch in one audio block. |
| Held notes and tails carry over a patch change | **Done** | An instrument not in the new patch rings on: held notes until their keys (and sustain) are let go, reverbs until they fade. |
| Splits and layers | **Done** | Keyboard Zone (channel menu): key range, transpose, velocity range (layers), MIDI channel. |
| Keyboard knobs and faders control plugin settings | **Done** | Knobs (channel menu): learn a knob for any setting of the channel's instrument or effects, with a range; per patch. The plugin's own window follows. |
| Songs and patches switched from the keyboard | **Done** | Learnable buttons/pedals for next/previous song and patch, panic, tap tempo and backing track; Program Change picks the patch. |
| Tempo | **Done** | Plugins follow the tempo (and the beat and bar); each song can set one; tap tempo in the toolbar or on a pedal. |
| Backing tracks and click | **Done** | A backing track per song (WAV, MP3, FLAC...), played from the toolbar or a pedal; a click on every beat. |
| Audio inputs (vocal or guitar through effects) | **Done** | Choose the input device in Settings > Audio, then add an audio input channel in the mixer. |
| MIDI clock | **Done** | Sent to a chosen MIDI output; the tempo can follow a clock coming in (Settings > MIDI). |
| Undo | **Done** | Every edit to the setlist (Ctrl+Z, Ctrl+Shift+Z); a fader drag is one step. |
| Sounds out of the box | **Partly** | The app scans a `plugins` folder next to itself; an installer has to fill it (see below). |
| Plugin formats | **Partly** | VST3 only. Gig Performer also hosts VST2 (many older Windows plugins); an open-source VST2 host needs care, since Steinberg no longer licenses the VST2 SDK. |
| Charts (chords and lyrics on screen) | **Ahead** | Built in; neither rival has them. |
| Master effects, meters, limiter | **Done** | |

**Still open:**
- **Bundled sounds:** which open-source instruments ship (e.g. Surge XT and
  Dexed, both GPL like the app, and a free sampled piano), built and
  placed in `plugins/` by the installer (6). Their licences and credits go
  with them.
- **VST2**, only if it can be done within the licences.

---

## 1. Instrument toggles and song sections

**First version built (2026-09-27)** — see
`docs/superpowers/specs/2026-09-27-song-sections-design.md`. The chart's
sections (pasted `[Verse 1]`, `[Chorus]`...) show centred and large, each
with the patch's instruments it plays (click [+] / ✕), and its length in
bars. Play counts the bars at the song's tempo and time signature (with a
count-in on the click) and switches the instruments by themselves a
sixteenth before each section (or a beat early), sample-exactly; the
backing track plays along; a pedal moves on to the next section. Songs
without sections play everything, as before. Still open below: following
the player by ear, tempo changes inside a song, fades between sections.

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

**First version built (2026-09-28)**, see
`docs/superpowers/specs/2026-09-28-loop-station-design.md`: a looper strip
above the mixer (● record, ⟳ loop with a progress ring and bar count), one
audio loop per channel with up to 8 layers and undo, synced to the bars or
free (the first loop can set the tempo), loops playing on through sections
and patches and cleared with the song, a Loops menu, a toolbar pill while
the strip is out of sight, and keyboard buttons, pads and an instrument
knob learned per setlist (Record + Loop held = clear). **Next: plan 2,
headphone monitoring** (headphone output or second device, Live/Monitor,
cue per channel).

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
shows the whole song.

**Done (chord follow, version 1):** the chart follows the chords played on
the keyboard. The song waits for its first chord ("Play Am to start"); the
root and any one other note of the next chord move on (any voicing, any
bass, sus and power chords count); the first two chords of another section,
played clearly, jump there; the pedal or a click picks a section. The chord
being played is lit, the next one outlined, and the chart scrolls with it.
Sections switch instruments on the note that entered them, with the held
chord handed over. Each song follows its chords (the default) or its tempo
(Play counts bars), in the song's settings.

**Next:** following the tempo from the playing, a backing track kept in
time with it, recognising which song is being played, and following from
an audio input (guitar, voice) as well as MIDI.

## 6. Installer

Build the app as an .exe plus module DLLs and package it into a Windows
installer, as Audacity 4 does. The core, engine and ui modules are kept
separate for this. With 7: a macOS app bundle and Linux packages.

## 7. Windows, macOS and Linux

**The goal:** the same app on all three, then a public, free, open-source
beta. Four pieces (design: `docs/superpowers/specs/2026-09-30-cross-platform-linux-design.md`):

1. **Done: the platform layer and Linux.** Everything that differs by system
   lives in per-system files (`src/platform`, and a few engine files):
   crash reports (a signal handler and backtrace on Linux), one app at a
   time (a lock file and a per-user socket), plugin folders (`~/.vst3`,
   `/usr/lib/vst3`, `/usr/local/lib/vst3`), plugin windows (X11, with the
   host running the plugin's timers and events, as Reaper does), audio
   drivers (PulseAudio, JACK, ALSA), the scanner process, memory readings,
   timers. A test fails if system code appears anywhere else. Linux builds
   with GCC 13 and passes every test that is not about Windows, with Surge XT
   as its test instrument; ASan, the fuzzers and the soak run on Linux too.
   Windows works exactly as before.
2. **Next: macOS.** The `_mac` files (plugin windows in an NSView, Core Audio
   and Core MIDI, `~/Library/Audio/Plug-Ins/VST3`), a build for Apple Silicon
   on GitHub's Mac machines, an ad-hoc-signed `.dmg`; tested on a friend's
   M5 MacBook.
3. **Automatic builds and packages:** one GitHub workflow building and
   testing all three on every change, and making the Windows installer, the
   `.dmg` and a Linux AppImage (free for public repositories).
4. **Going public:** the pre-publication check of the repository and its
   history, licence notices, a landing page with the downloads and a
   donate button, the first public beta.

**Open:**
- Audio Units on macOS: many Mac players have plugins as AU; is VST3 enough
  there?
- Real USB MIDI keyboards on Linux are untested in WSL (it has none;
  usbipd could pass one through); a Linux desktop tester would check them.
- Plugin artwork (`PlugIn.ico`) is a Windows folder icon; Linux and macOS
  plugins carry theirs differently or not at all.
