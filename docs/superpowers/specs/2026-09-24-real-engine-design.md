# OpenStage — Real Engine v1 (Design)

Date: 2026-09-24 · Status: working design (user asked to keep momentum; no separate plan doc)

## Goal

Replace `FakeEngine` behind the existing `IEngine` with an engine that makes
sound: MIDI keyboard → the current patch's channels (VST3 instrument + VST3
effects, key range / transpose / MIDI-channel filter, volume/mute/solo) →
audio device. Proven feasible by `spikes/engine_spike` (Scarlett Solo,
WASAPI 48 kHz/256: 0 underflows, Piano V2 ≤ 26 % of buffer).

## Decisions already made

- Default output: the Windows system device via WASAPI. ASIO drivers are
  listed and selectable. If ASIO fails to start or requests a reset: reopen
  once, else fall back to WASAPI and report why. (user, 2026-09-24)
- VST3 via the Steinberg SDK 3.8 (MIT), fetched with FetchContent; the SDK
  hosting sources that `sdk_hosting` omits (module, module_win32,
  plugprovider, memorystream) build as a C++17 library.
- RtAudio 6 (with ASIO) and RtMidi 6 from vcpkg (manifest feature `audio`).
- Approach studied from Muse (Audacity 4 / MuseScore), not copied:
  plugins load/unload on the main thread; controller synced to component
  state after init; setupProcessing → prepare → activate main buses →
  setActive → setProcessing; reversed teardown after audio stops.

## Units (all inside `src/engine`, internal unless noted)

| Unit | Job | Tested by |
|---|---|---|
| `MidiRouter` | Filter/transform one MIDI event for one channel: MIDI channel filter, key range, transpose (drops notes pushed outside 0–127). | unit tests |
| `INode` | What the graph runs: `prepare(sampleRate, maxBlock)`, `process(events, stereo buffers, frames)`. | — |
| `RenderGraph` | Immutable per patch: channel strips (instrument node, effect nodes, settings). `render()` routes MIDI, runs nodes, applies gain/mute/solo (atomics, changeable live), sums to the output, updates per-channel peak/RMS atomics. No allocation, locks or logging. | unit tests with fake nodes |
| `GraphExchange` | Main thread publishes a new graph; audio thread picks it up at the start of the next block; retired graphs are freed on the main thread only after the audio thread has moved past them. Lock-free. | multi-thread test under ASan |
| `Vst3Node` | `INode` over one VST3 plugin (SDK wrapper; the only code that includes SDK headers). Load errors → `Result`. | integration test, skips when the plugin is not installed |
| `PluginCatalog` | Finds `.vst3` bundles under the standard folder, reads class info (name, vendor, instrument vs effect). | integration test |
| `AudioDevice` | RtAudio wrapper: list WASAPI + ASIO outputs, open default WASAPI, open chosen ASIO, reset/fallback handling, runs a render callback. | integration test (skips without devices) + manual |
| `MidiInput` | RtMidi wrapper: list ports, open all ports, lock-free queue to the audio thread. | manual + queue unit test |
| `RealEngine` (public factory `createRealEngine()`) | Implements `IEngine`: `applyPatch` builds a graph (plugin instances cached per channel id so returning to a patch is instant), publishes it; mixer calls update atomics; levels/CPU/MIDI activity read from atomics. | unit tests with injected fake node factory + manual |

`tools/engine_cli` (kept, not a spike): loads a setlist file, opens the
default device and all MIDI inputs, and switches patches from the computer
keyboard — the way to hear the engine before the UI exists.

## Real-time rules

The audio callback never allocates, locks, logs, throws, or calls Qt.
Everything it touches is either owned by the current graph (immutable while
published) or an atomic. Per-block MIDI events use a fixed-capacity array.

## Out of scope for v1

Plugin editor windows (sub-project 3), background/parallel plugin scanning,
out-of-process plugin sandboxing, sustain/CC → VST3 parameter mapping
(IMidiMapping), audio inputs, preloading a whole setlist, multi-output
plugins beyond the main stereo bus.
