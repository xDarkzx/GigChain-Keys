# Loop Station Implementation Plan (plan 1 of 2: loops)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One tempo-synced (or free) audio loop per mixer channel: record, loop, overdub layers, undo, stop, clear — from a looper strip above the mixer, a Loops menu, a toolbar pill, and pedals.

**Architecture:** The engine gets a `LoopStation` (fixed 32 slots, audio-thread state machine per slot, buffers owned by the main thread and handed over through `HazardExchange`). Each channel strip taps its post-fader, post-pan sound into its slot while recording; the station plays every loop into the mix before the master effects, independent of the current patch. Record/stop points are quantised on the audio thread to the bar grid (synced) or to the first loop's cycle (free). The UI talks to the engine only through a `LoopController` (commands + polled state), which the future iPad remote will reuse. Plan 2 (monitoring: headphone bus, cue, Live/Monitor, second device) builds on this.

**Tech Stack:** C++20, Qt 6.10 QML/QtTest, VST3 host engine.

**Spec:** `docs/superpowers/specs/2026-09-28-loop-station-design.md`

## Global Constraints

- Audio thread: no allocation, locks or logging; buffers are allocated on the main thread.
- Never swallow errors (limits reached, out of memory: said and logged).
- No timers or delays as workarounds; no synthetic clicks in the user's running app.
- Edit source with the Edit tool only.
- Setlist: the song's loop Sync setting is saved (format 4, not yet released: an optional field).
- Verified at the end by `tools\verify.ps1` plus a real-app check.

## Review Focus

1. Synced record must start and end on exactly the bar's sample; the loop must repeat bit-exact and stay aligned with the bar grid over many passes.
2. A patch or section change while loops play must not stop, glitch or double them; a song change clears them.
3. Memory: long loops (2 min stereo) must not be copied on the audio thread; clearing must free memory only after the audio thread let go.
4. Pedals: holding Record and Loop together clears; a single press never also clears.
5. Free mode: later loops stay in step with the first (rounded to its multiples/fractions).

## Rulings

- Ruling: record post-fader and pan (spec said "before its fader") — the loop plays on alone after its channel is gone, so it keeps the level the audience heard — cost if wrong: a per-loop gain later.
- Ruling: free mode = the first loop's cycle is the grid (later loops start/stop on quarter-cycles from its start); "tempo from first loop" additionally sets the song tempo and restarts the bar count on the loop — cost if wrong: grid rules revisited.
- Ruling: overdub layers are allocated on the main thread once the loop's length is known; an overdub asked before they are ready waits (it is quantised to the next pass anyway) — cost if wrong: one poll interval.
- Ruling: pedal actions act on the channel selected in the mixer.

---

### Task 1: Icons and the looper strip (screens first, no sound yet)

**Files:** create `src/ui/icons/loop-record.svg`, `src/ui/icons/loop.svg`, `src/ui/LooperCell.qml`, `src/ui/LoopsPill.qml`; modify `src/ui/Mixer.qml` (a looper band above the strips; each delegate = LooperCell over ChannelStrip), `src/ui/Toolbar.qml` (Loops menu next to File; pill), `src/ui/CMakeLists.txt`; test `tests/ui/tst_qml_smoke.cpp`.

- LooperCell: two round buttons (● record, ⟳ loop with a progress ring and "2/4"), states drawn from a `state` string: "empty", "armed", "recording", "closing", "playing", "overdubArmed", "overdubbing", "stopped", "startArmed", "stopArmed".
- Smoke: cells line up with their strips (same x and width), strip hides/shows from the Loops menu.

### Task 2: LoopStation core (engine, pure, sample-exact)

**Files:** create `src/engine/internal/LoopStation.h/.cpp`; test `tests/engine/tst_loop_station.cpp`.

**Produces:**
```cpp
struct LoopGrid { int64_t origin = 0; double unit = 0.0; };          // samples: grid lines at origin + k*unit
enum class LoopState : int { Empty, Armed, Recording, Closing, Playing, OverdubArmed, Overdubbing,
                             Stopped, StartArmed, StopArmed };
struct LoopReading { LoopState state; int64_t length; int64_t position; int layers; };
class LoopStation {
  // main thread
  void setCapacity(int slot, int64_t frames);            // allocates the base take (before Record)
  bool prepareLayers(int slot);                          // after the base closed: trimmed base + 8 layers
  void post(int slot, LoopCommand);                      // Record, PlayStop, Undo, Stop, Clear
  void stopAll(); void clearAll();
  LoopReading read(int slot) const;
  std::vector<LoopNotice> takeNotices();                 // limits hit
  // audio thread
  void beginBlock(int64_t samplePosition, int frames, const LoopGrid& grid) noexcept; // applies commands at grid lines
  void record(int slot, const float* left, const float* right, int frames) noexcept;   // a strip's post-fader sound
  void play(float* left, float* right, int frames) noexcept;                            // every playing loop, summed
};
```
- [ ] Tests: synced record from the bar after the press to the bar after the second press (exact samples); playback bit-exact over 10 passes; overdub sums; undo removes the last layer; start/stop on the next grid line; stopAll/clearAll; capacity limit stops the recording and reports; free mode: first loop's length is the press-to-press span, the second rounds to a power-of-two multiple/fraction; no allocation in beginBlock/record/play.

### Task 3: Engine wiring

**Files:** `RenderGraph.h/.cpp` (strip `setLoopSlot(int)`; tap in `mixInto`; `render(..., LoopStation*)` mixes loops before master effects), `RealEngine.h/.cpp`, `IEngine.h`, `EngineTypes.h` (`LoopCommand`, `LoopReading` public view, `LoopSetup{bool sync}`), `FakeEngine.h`, `tests/common/SpyEngine.h`; test `tst_real_engine.cpp`.

**Produces (IEngine):**
```cpp
virtual void loopCommand(const core::ChannelId& channel, LoopCommand command) = 0; // allocates on Record
virtual void setLoopSync(bool sync) = 0;
virtual void takeTempoFromFirstLoop(bool take) = 0;
virtual void stopAllLoops() = 0;
virtual void clearAllLoops() = 0;
[[nodiscard]] virtual std::vector<ChannelLoop> loops() const = 0; // {channel, state, progress 0-1, bar, bars, layers}
```
- [ ] Test (real device, silent): a channel's loop records its sound and keeps playing after a patch change to a patch without that channel; clearAll silences it.

### Task 4: LoopController, pedals, song life

**Files:** create `src/ui/cpp/LoopController.h/.cpp` (QML: `record(channel)`, `playStop(channel)`, `undo(channel)`, `clear(channel)`, `stopAll()`, `clearAll()`, `sync`, `tempoFromFirstLoop`, `loopStates` (per current channel), `loopList`, `playingCount`, `recording`, `storedCount`); `DocumentController` (song Stop stops loops; another song clears them; `Song.loopSync` stored); `MidiControl.h` (LoopRecord, LoopPlay, LoopUndo, LoopStopAll; held tracking for clear); `SettingsController` labels; `Session` wiring; tests `tst_document_controller.cpp`, `tst_settings.cpp`, `tst_midi_control.cpp`.

### Task 5: Screens wired + Loops menu + pill

**Files:** `LooperCell.qml`, `LoopsPill.qml`, `Toolbar.qml`, `Mixer.qml`, `PerformView.qml`, `Main.qml`; smoke test drives Record/Loop via clicks against the fake engine's states; pill appears only with loops; menu items act.

### Task 6: Finish

- [ ] Roadmap (loop station: first version); full gate; real app screenshot; commit.
