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

### A microphone or a guitar

Under **Audio inputs**, choose the device your mic or guitar is plugged into.
Then in the mixer click **Audio input** (after the last channel) to add a
channel that plays it, with its own effects. See
[Instruments and effects](instruments.md).

## MIDI: where the notes come from

Open **Settings > MIDI**. Your keyboard is listed under **MIDI inputs** as
soon as it is plugged in. Set it to **Enabled**.

- **Enable only the port your keys play on.** Many keyboards show a second
  port (for their own control software); leave that one off.
- **Channel:** *All channels* suits almost everyone. Choose one channel only
  if your keyboard sends different zones on different channels.
- The **MIDI** light in the top bar flashes as notes arrive: a quick way to
  check your keyboard is heard.

### Pedals and pads

Under **Pedals and pads** you can change songs with your feet: click
**Learn** beside an action (next song, previous song, panic, tap tempo…),
then press the pedal, pad or button. Done. See [Perform mode](perform.md).

### MIDI clock

**Send clock to** sends the song's tempo to a drum machine or another app.
**Follow the tempo of a MIDI clock coming in** lets a drum machine or DAW
lead instead. See [Sections, tempo and backing tracks](sections-and-tempo.md).

## Check it works

Load an instrument (see [Instruments and effects](instruments.md)) and
play. You should see the **MIDI** light flash, the channel's meter move and
hear the sound. If not, see [Troubleshooting](troubleshooting.md).
