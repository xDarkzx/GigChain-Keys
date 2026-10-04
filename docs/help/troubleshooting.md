# Troubleshooting

Something not right? Find it below.

## I hear nothing

1. Is the **MIDI** light in the top bar flashing when you play? If not, your
   keyboard is not heard: open **Settings > MIDI** and set your keyboard's
   port to **Enabled**. See [Sound and your keyboard](help:audio-and-midi).
2. Does the channel's meter move? If not, check the channel is not **muted**
   and its **keyboard zone** covers the keys you play (right-click the strip >
   **Keyboard Zone…** > **Whole keyboard**). In a song with sections, check the
   instrument is in the **section** you are in (see
   [Sections, tempo and backing tracks](help:sections-and-tempo)).
3. Does the master meter move but you still hear nothing? Check **Settings >
   Audio**: the right **Device**, and **Running now** shows it open. Check the
   master fader and that **Mute everything** is off.

## Crackles, clicks or dropouts

- Raise the **Buffer size** in **Settings > Audio** (256 → 512).
- Use your interface's **ASIO** driver if it has one.
- Watch **CPU** in the top bar. If it is high, use fewer or lighter plugins in
  the song: one shared reverb on the master strip costs less than a reverb
  on every channel.
- Close other programs, especially web browsers playing video.

## A note keeps sounding

Press the red **Panic** button in the top bar. It stops every note on every
instrument.

## A plugin will not load

- **Switched off after a crash:** a plugin that crashed while loading is not
  loaded again, so it cannot take the app down. **Settings > Plugins** lists
  them; **Try again** after updating the plugin.
- **Not in the list:** GigChain Keys plays **VST3** plugins only. Check the
  plugin is installed as VST3 (in *C:\Program Files\Common Files\VST3* on
  Windows). Also check **Show hidden instruments**.
- A song whose plugin is missing says so on its channel; the rest of the song
  still plays.

## The setlist opened but a song sounds wrong

Each plugin's settings are saved with the song when you **Save**. If you
changed a sound and did not save, the old sound comes back next time.

## Practice shows nothing

The song has no chords yet. Add them in the **Chart** tab. See
[Chord charts](help:charts) and [Practice mode](help:practice).

## Still stuck?

**Help > About** shows the version and the website, where you can report the
problem. Saying what you did, what you expected and what happened helps a lot.
