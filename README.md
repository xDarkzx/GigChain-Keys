# OpenStage (working name, not final)

Pre-design notes only — nothing below is a decision, just context carried over
from the conversation that started this project. Open a fresh Claude Code
session in this folder and run the brainstorming process properly from here.

## The idea, roughly

An open-source, Windows-first live-performance host for keyboard/piano
players. Boot fast, skip DAW project setup, host VST instruments and sample
libraries already installed on the machine, assign patches per song in a
setlist, auto-switch patches at song sections (verse -> chorus -> verse),
route straight to an audio interface for live use.

## Why "open source" instead of just building another closed one

Existing tools in this space (Cantabile, Gig Performer, MainStage, Camelot
Pro) are mature and closed-source. General-purpose open-source plugin hosts
exist (Carla, Pedalboard2, Light Host, Kushview Element) but none specialize
in the gig-focused setlist/scene-switching workflow those paid tools own.

## Known close competitor — check this before assuming a gap

**KeyStage** (iOS/iPadOS, commercial, in-app purchases) already does almost
exactly this for keyboard players specifically: MIDI controller mapping,
setlists with quick song-to-song switching, AND automatic instrument-preset
matching from a database when it detects a new MIDI connection. It is not
open source and not on Windows. That's the actual gap, not the core concept.

## Possible differentiator under consideration

Smart plugin/preset discovery and ranking — reusing plugin-detection/ranking
work already built in the sibling Reaper-MCP project (`../Reaper-MCP`) rather
than starting that logic from scratch.

## Real architecture question, not yet resolved

Qt (via the already-installed Audacity4 dev build environment) gives
cross-platform UI + build tooling, but not VST hosting or a real-time audio
engine (no ASIO/WASAPI abstraction, no VST3/AU plugin loading) — that's what
JUCE specializes in. Whether to use JUCE outright, or Qt for UI with a
separate audio-hosting layer, is unresolved and should be one of the first
things the brainstorming process nails down.

## Constraints worth keeping in view

- Sole author (with AI pairing), not a team — scope accordingly.
- Prefers demand-driven scope over building ahead of real need.
- This is a much bigger technical lift than anything in Reaper-MCP: real-time
  audio/DSP, a new framework (JUCE and/or Qt), likely a new language (C++).
- Nothing has been scaffolded yet. No stack, no name, no design is final.
