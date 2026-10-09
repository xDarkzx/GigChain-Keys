# Sound and your keyboard

Before anything plays, GigChain Keys needs to know two things: **where the
sound goes** (your audio interface or speakers) and **where the notes come
from** (your MIDI keyboard). Both are in **Settings** (the button at the top
right, or **Ctrl+,**).

## Audio: where the sound goes

Open **Settings > Audio**.

1. **Driver.** On Windows the system driver (WASAPI) works with every sound
   card and is the safe choice. If your audio interface came with an
   **ASIO** driver, choose ASIO for the lowest delay between key and sound.
2. **Device.** Pick your audio interface or speakers.
3. **Buffer size.** This sets the delay (shown as **Latency** in
   milliseconds). Smaller feels tighter under your fingers; bigger is safer
   against clicks and crackles. Start at **256 samples**; go down to 128 or
   64 if your computer copes, up to 512 if you hear crackles.
4. **Sample rate.** 48000 Hz is a good choice; use what your interface is set to.

**Running now** at the bottom shows what is really open, so you can check
your choice took.

### The safety limiter

**Never let the output go past the ceiling** is on by default. It is the last
thing before your speakers: if a patch is too loud or a synth runs away, the
limiter catches the peak so the PA (and the audience's ears) are safe. The
**LIM** light on the master strip lights when it is working; if it is often
lit, turn something down.

### More outputs: the click in your ears, a channel to the desk

With an audio interface that has more than two outputs, everything you hear
still comes out of outputs **1-2** (the mix). Two things can go elsewhere:

- **The click:** **Settings > Audio > Outputs > Click to**, choose a pair
  (say **Outputs 3-4**) and plug your in-ear monitors there. The click counts
  you in and keeps time in your ears; the audience never hears it.
- **A channel:** right-click it in the mixer, **Output**, and pick a pair: a
  guide track or a pad to its own channel on the sound desk. A channel sent
  there plays with its own fader, not through the master. The strip shows
  where it goes (**Out 3-4**).

Every pair has the safety limiter too. A pair the interface does not have
(another interface plugged in) plays in the mix until that one is back.

### A microphone or a guitar

Under **Audio inputs**, choose the device your mic or guitar is plugged into.
Then in the mixer click **Audio input** (after the last channel) to add a
channel that plays it, with its own effects. See
[Instruments and effects](instruments.md).

## MIDI: where the notes come from

Open **Settings > MIDI**. Your keyboard is listed under **MIDI inputs** as
soon as it is plugged in. Each input **Plays**, gives its **Buttons and
knobs** only, or is **Off**.

- **Only the port your keys play on Plays.** Many keyboards show a second
  port for DAW control (*MIDIIN2 (…)*, *… DAW In*): it is set to **Buttons
  and knobs** by itself, so its transport buttons work and it never plays a
  note twice. A controller without keys (a Korg nanoKONTROL, say) can be set
  to Buttons and knobs too.
- **Channel:** *All channels* suits almost everyone. Choose one channel only
  if your keyboard sends different zones on different channels.
- The **MIDI** light in the top bar flashes as notes arrive: a quick way to
  check your keyboard is heard.

### Your keyboard's buttons: what works by itself

Keyboards speak a few common languages for their buttons, and the app
understands them all without any setup:

| The keyboard sends | Play | Stop | ◀◀ | ▶▶ | Loop / Cycle | Click | Others |
|---|---|---|---|---|---|---|---|
| **MIDI Start / Stop** | from the top | stop | | | | | Continue: from where it is |
| **MMC** (MIDI Machine Control) | from the top | stop | previous part | next part | | | |
| **Mackie Control** (the *DAW mode* of Korg, M-Audio, Arturia, Novation, Akai and others) | play | stop | previous part (with Shift: previous song) | next part (with Shift: next song) | loop the part | click on/off | Bank ◀ ▶: songs; Channel ◀ ▶: sounds |

Many keyboards send their own numbers instead (a Nektar Impact sends
controllers 66-69 and 98-100; a nanoKONTROL 41-46). Learn those once, below.

### Pedals, pads and buttons

Under **Pedals and pads** you can teach the app any button, pad or pedal:
click **Learn** beside an action (**Play the song**, **Stop the song**,
**Next part**, **Previous part**, **Hold this part**, **Click on / off**,
next song, previous song, panic, tap tempo…), then press it. Done. What you
learn is never heard by the instruments. See [Perform mode](perform.md).

### Faders and knobs for the mixer

**Right-click a channel's fader**, its **pan knob** or the **master fader**
and choose **Learn a keyboard knob…**, then move a knob or fader on your
keyboard. It now moves that control, in every song: the first strip's fader
follows it whatever sound is playing, as a controller's eight faders sit
over eight strips. Right-click again to **Forget** it. For a plugin's own
settings (a filter, a leslie speed) see [Splits, layers and knobs](splits-layers-knobs.md).

### Expression, dynamics and the other sliders

The mod wheel (CC 1), breath (CC 2), foot pedal (CC 4), volume (CC 7), pan
(CC 10), expression (CC 11) and sustain (CC 64) go straight to the
instruments, which use them their own way (an orchestral library's
dynamics on the mod wheel, expression on CC 11). Nothing to set up.

### MIDI clock

**Send clock to** sends the song's tempo to a drum machine or another app.
**Follow the tempo of a MIDI clock coming in** lets a drum machine or DAW
lead instead. See [Sections, tempo and backing tracks](sections-and-tempo.md).

### Starting the song

- **My keyboard's Play and Stop buttons start and stop the song** (on): the
  transport buttons in the table above. Switch it off if your keyboard
  sends Start for its own arpeggiator.
- **Pressing the sustain pedal twice quickly starts and stops the song**
  (off): two presses within 0.4 s. Each press still sustains.

Pedals and pads can also learn **Next part of the song**, **Repeat this part
once more** and **Hold this part**.

## Check it works

Load an instrument (see [Instruments and effects](instruments.md)) and
play. You should see the **MIDI** light flash, the channel's meter move and
hear the sound. If not, see [Troubleshooting](troubleshooting.md).
