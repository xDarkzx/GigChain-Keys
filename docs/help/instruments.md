# Instruments and effects

GigChain Keys plays **VST3 plugins**: pianos, organs, synths, strings, and
effects such as reverbs and delays. It finds the plugins installed on your
computer by itself (on Windows in *C:\Program Files\Common Files\VST3*).
**Settings > Plugins** shows how many were found.

## No instruments yet?

Free ones cover everything a keys player needs to start. **Help > Get free
instruments** (and the Instruments tab, when it is empty) lists them, each
with a **Get it** button to its download page:

- **Splice INSTRUMENT (LABS):** pianos, pads, strings and more (free with a
  Splice account).
- **Surge XT:** a big synth with hundreds of pads, leads, basses and keys.
- **Vital:** a modern synth with lush pads and leads (the free version).
- **Dexed:** classic 80s electric pianos, bells and basses.
- **Salamander Grand Piano:** a sampled concert grand, played in the free
  sfizz player.

Install them as their makers say (choose **VST3** when asked), then close and
reopen GigChain Keys: it finds new instruments when it starts.

## Load an instrument

Each instrument plays on its own **channel** in the mixer at the bottom of the
Edit view. To add one, any of these works:

- Open the **Instruments** tab of the left panel and **double-click** an
  instrument.
- **Drag** it from the list onto the mixer.
- Click the empty **Instrument** slot after the last channel and pick one
  from the menu (grouped by maker).

The instrument's own window opens in the **Instrument** tab of the main area,
so you can choose its preset and tweak it. Its settings are saved with the
song.

### Finding instruments

- **Search instruments** at the top of the list finds one by name or maker.
- Right-click one to **Add to favourites** (kept at the top), **Rate it**, or
  **Hide from list** (bring hidden ones back in **Settings > Plugins**).
- **Details** shows the maker, version, where it is installed and its website.

## The same instrument in several songs

A piano you use all night needs loading only once. Click the empty
**Instrument** slot after the last channel, choose **Same as in another
song**, and pick it: this song now plays that very instrument, with its
preset and settings. **Duplicate** a song and the copy shares its
instruments the same way.

A shared instrument uses its memory once however many songs play it, which
matters for big sampled pianos and orchestras. Above its window the app says
**Shared by 3 songs: changes apply to all**: a change you make to it is heard
in every one of those songs. To change it for this song alone, click **Own
copy for this song**; the song then gets a copy of its own (it loads a
second one, so it uses more memory). **Undo** shares it again.

## Two instruments at once

Add a second channel and both play together: a piano with strings under it,
say. To give each its own part of the keyboard (a split) or have them answer
to how hard you play (a layer), see [Splits, layers and knobs](splits-layers-knobs.md).

## The channel strip

Each channel strip, top to bottom:

- **Colour tag and name** (hover for **✕**, which removes the channel).
- **The instrument slot:** click to open its window.
- **Effect slots:** click the empty slot to **Add Effect** (a reverb, a
  delay, an EQ…). Effects run top to bottom. Click an effect to open its own
  window; right-click it to **Bypass**, **Replace With** or **Remove Effect**.
- **Where it plays**, when not the whole keyboard (click to change).
- **Pan**, the **volume fader** and the **meter**.
- **Mute** and **Solo**.

**Right-click anywhere on the strip** for the channel menu: **Keyboard Zone…**,
**Knobs…**, **Play Audio Input**, **Replace Instrument**, **Remove Channel**.

## The master strip

At the right end of the mixer, the **Master** strip is everything together.
Its effect slots (**Add an effect on everything**: an EQ, a compressor…) are
kept with your rig, not with the setlist, so they are the same in every song.
The **LIM** light shows the safety limiter at work (see
[Sound and your keyboard](audio-and-midi.md)).

## A microphone or a guitar

Click **Audio input** after the last channel and pick the input. The channel
plays what comes in, through its effects: a vocal with reverb, a guitar
through an amp plugin. Choose the input device first in **Settings > Audio**.

## Show and hide

The **Mixer** and **Keys** buttons in the top bar show or hide the mixer and
the on-screen keyboard. The on-screen keyboard lights the keys you play and
can be played with the mouse.
