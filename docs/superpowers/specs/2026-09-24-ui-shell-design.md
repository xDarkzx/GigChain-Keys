# OpenStage — Sub-project 1: UI Shell (Design)

Date: 2026-09-24
Status: Draft for review

## 1. Context

OpenStage is an open-source, Windows-first live-performance host for keyboard
players, modelled on MainStage. v1 is for the author's own gigs. The project is
split into sub-projects, each with its own spec → plan → build cycle:

1. **UI shell on a fake engine** ← this spec
2. Audio/MIDI device setup (RtAudio ASIO/WASAPI, RtMidi)
3. VST3 scanner, plugin browser, embedded plugin editor
4. Live engine: MIDI → instrument → FX chain → audio out, patch switching
5. Instrument ranking (idea from Reaper-MCP, new rules for instruments)

This spec also sets the **engineering standards (§7)** that every later
sub-project must follow.

### Goal of this sub-project

A runnable Qt Quick app with the full Edit/Perform UI, a working setlist
(create, edit, navigate, save/load), and a mixer — all backed by a fake engine
behind a stable interface, so sub-projects 2–4 replace the backend without
rewriting screens.

### Success criteria

- App launches to Edit mode; a setlist can be built, saved, reopened.
- Patch navigation works via buttons and keyboard shortcuts in both modes.
- Mixer strips reflect the current patch; fake meters/CPU/MIDI animate.
- Plugins tab lists fake instruments/effects; drag-to-create channel and
  drag-to-add effect work.
- Malformed or hostile setlist files never crash the app.
- All tests pass under the AddressSanitizer preset with no leaks reported.

## 2. Tech stack

| Concern | Choice |
|---|---|
| Language | C++20 |
| Build | CMake + Ninja, `CMakePresets.json` (debug, release, asan) |
| Compiler | MSVC 2022, x64 |
| UI | Qt 6.10 Qt Quick / QML (`qt_standard_project_setup(REQUIRES 6.8)`, `qt_add_qml_module`) |
| Dependencies | vcpkg manifest mode (`vcpkg.json`); Qt from the local install `C:\Qt\6.10.2\msvc2022_64` |
| Error values | `tl::expected` (vcpkg `tl-expected`) until the project moves to C++23 `std::expected` |
| Tests | Qt Test |
| Later sub-projects | RtAudio, RtMidi, Steinberg VST3 SDK — used only inside their own modules |

The Muse framework is **not** a dependency. Its `vst` module may be read as a
reference for how to do VST3 hosting, but no code is copied from it.

## 3. Module architecture

Each module is a CMake static library with a public interface directory and a
private `internal/` directory. Other modules may include only the public
headers. Dependencies point one way:

```
app ──► ui ──► engine (interface) ──► core
         │                              ▲
         └──────────────────────────────┘
```

| Module | Purpose | Depends on |
|---|---|---|
| `core` | Plain C++ domain model: `Setlist`, `Song`, `Patch`, `Channel`, `PluginSlot`; JSON load/save; navigation logic. No Qt Quick. Uses QtCore only for JSON/file IO. | QtCore |
| `engine` | `IEngine` interface (public) + `FakeEngine` (internal). Later sub-projects add the real engine here or as sibling modules (`audio`, `midi`, `vst`) behind the same interface. | `core` |
| `ui` | Qt list models exposing `core` to QML, controllers for navigation/editing, and all QML files. | `core`, `engine` interface |
| `app` | `main.cpp`: the composition root. Constructs the engine and services, hands them to the UI by constructor/property injection. The only place that knows `FakeEngine` exists. | all |
| `tests` | Unit tests per module. | module under test |

### Directory layout

```
OpenStage/
  CMakeLists.txt  CMakePresets.json  vcpkg.json  .clang-tidy  .clang-format
  cmake/                 CompilerHardening.cmake, Sanitizers.cmake
  src/
    core/     include/openstage/core/*.h   internal/*.cpp  CMakeLists.txt
    engine/   include/openstage/engine/IEngine.h, EngineTypes.h
              internal/FakeEngine.{h,cpp}   CMakeLists.txt
    ui/       include/openstage/ui/*.h     internal/*.cpp
              qml/  Main.qml Theme.qml Toolbar.qml SidePanel.qml
                    SetlistView.qml PluginBrowser.qml PluginArea.qml
                    Mixer.qml ChannelStrip.qml Inspector.qml PerformView.qml
    app/      main.cpp  CMakeLists.txt
  tests/      core/  engine/  ui/
  docs/superpowers/specs/
```

### `IEngine` (initial surface)

The interface talks in `core` types and plain values only — no Qt Quick, no
SDK types.

- `applyPatch(const core::Patch&)` — make this patch the sounding one.
- `availablePlugins() -> std::vector<PluginInfo>` — name, vendor, kind
  (instrument/effect), id.
- `channelLevel(ChannelId) -> LevelReading` — peak/RMS, polled by the UI.
- `cpuLoad() -> float`, `midiActivity() -> bool`.
- `setChannelVolume / setChannelMute / setChannelSolo`.

`FakeEngine` returns a fixed plugin list and simulated levels/CPU/MIDI driven
by a timer. The UI polls at ~30 Hz; no engine → UI callbacks cross threads in
this sub-project.

## 4. Data model

```
Setlist
 └─ Song[]            name
     └─ Patch[]       name   (song sections are patches, in order)
         └─ Channel[] name, instrument: PluginSlot?, effects: PluginSlot[],
                      volume (dB), mute, solo, keyLow, keyHigh (0–127),
                      transpose (-48..+48), midiChannel (0 = omni, 1–16)
PluginSlot            pluginId, displayName, bypass
```

- `Song`, `Patch` and `Channel` each carry a stable `id` (UUID string,
  persisted), exposed across modules as `SongId` / `PatchId` / `ChannelId`.

- Layers and splits = several channels with overlapping or split key ranges.
- Navigation: next/previous patch walks across song boundaries; next/previous
  song jumps to the first patch of that song. Navigation clamps at the ends
  (no wrap).
- Section changes are always manual (button, shortcut; later footswitch/MIDI).
  No timeline or tempo-driven switching.

### File format

- One setlist per `.openstage.json` file.
- Top-level `formatVersion` (integer, starts at 1). Unknown higher versions are
  refused with a clear error, not guessed at.
- Save uses `QSaveFile` (write-to-temp then atomic rename), so a crash mid-save
  never corrupts the existing file.
- The last opened file path is kept in `QSettings` and reopened on launch; if it
  fails to load, the app starts with an empty setlist and shows the error.

## 5. Screens and behaviour

**Edit mode** (default)

- **Toolbar:** Edit/Perform toggle, CPU and MIDI indicators, Settings button
  (placeholder until sub-project 2), side-panel collapse button.
- **Side panel** (collapsible), two tabs:
  - *Setlist:* tree of Songs → Patches. Add, rename, duplicate, delete,
    drag-reorder. Selecting a patch makes it current everywhere.
  - *Plugins:* searchable list grouped Instruments / Effects (fake data).
    Drag an instrument onto the mixer → new channel. Drag an effect onto a
    channel's effect slot → add effect.
- **Main area:** the selected channel's plugin editor. In this sub-project a
  placeholder box showing the plugin name; sub-project 3 embeds the real VST
  editor here. Slim header shows patch number and name.
- **Mixer** (right): one strip per channel in the current patch — instrument
  slot, effect slots with "+", meter, fader, mute, solo, name. Clicking a strip
  selects that channel.
- **Inspector** (bottom, collapsible): Patch → name. Channel → name, key range,
  transpose, MIDI channel.

**Perform mode**

- Full-screen, dark, high contrast. Current song + patch name very large; next
  patch shown smaller underneath.
- Side panel shows the setlist only (collapsible). Large previous/next buttons,
  compact master volume, CPU/MIDI indicators.
- No editing is possible in Perform mode.

**Shortcuts (both modes):** Space / → next patch · ← previous patch ·
PgDn / PgUp next / previous song · Tab toggle Edit/Perform.
Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S file actions (Edit mode only).
Unsaved changes shown with a dot in the title bar; closing with unsaved
changes asks to save.

**Theming:** all colours, spacing and fonts live in one `Theme.qml` singleton.
Dark theme only for now. Layout and workflow follow MainStage; no Apple assets,
icons or names are used.

## 6. Error handling

- Functions that can fail for expected reasons (file IO, parsing, validation)
  return `tl::expected<T, Error>`; `Error` carries a code and a user-facing
  message. Exceptions are not used for control flow.
- Any exception thrown by Qt or the standard library at a module boundary is
  caught there and converted to an `Error`. No exception crosses into QML or
  (later) into an audio/plugin callback.
- The UI shows errors in a non-blocking banner; a failed load or save never
  loses the in-memory setlist.

## 7. Engineering standards (apply to all sub-projects)

### 7.1 Ownership and memory

- RAII everywhere. No naked `new`/`delete` in project code.
- Ownership is explicit: `std::unique_ptr` by default; `std::shared_ptr` only
  where ownership is genuinely shared, and justified in a comment.
- Raw pointers and references are non-owning only, and never outlive the
  owner. Non-owning `QObject` references that may outlive their target use
  `QPointer`.
- Qt parent ownership is allowed for `QObject`s created with a parent.
- Signal/slot connections always pass a context object so they disconnect
  automatically when either side is destroyed.
- Containers of domain objects hold values or `unique_ptr`; indices and IDs
  (`ChannelId`, `PatchId`) are used across module boundaries instead of
  pointers.

### 7.2 Boundaries and third-party code

- Every call into an SDK or external library (Qt excluded; later RtAudio,
  RtMidi, VST3 SDK) goes through a wrapper class in the module that owns it.
  No other module includes SDK headers.
- Wrappers check every return code, convert failures to `Error`, and never let
  SDK exceptions or error states leak out.
- Plugins are treated as untrusted code. Crash isolation (e.g. running plugins
  out-of-process) is a decision for sub-project 3 and is recorded there.
- Forward rule for sub-project 4: the audio callback does not allocate, lock,
  log, or call into Qt.

### 7.3 Input validation

- Setlist files are untrusted input: file size capped (8 MB), counts capped
  (songs, patches, channels, effect slots), every numeric field range-checked
  (MIDI notes 0–127, channels 0–16, transpose, volume), strings length-capped.
  Invalid input yields an `Error`, never undefined behaviour or a crash.

### 7.4 Build hardening

- MSVC: `/W4 /WX /permissive- /sdl /guard:cf /utf-8 /Zc:__cplusplus`;
  linker `/guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`. Warnings-as-errors
  applies to project code only, not third-party headers (included as SYSTEM).
- `asan` preset builds with `/fsanitize=address`; tests run under it.
- Debug builds enable the CRT leak checker (`_CrtSetDbgFlag`); the test
  runner fails if leaks are reported at exit.
- `.clang-tidy` enables `cppcoreguidelines-*`, `bugprone-*`,
  `modernize-*`, `performance-*` (with noisy checks disabled);
  `.clang-format` defines the house style.

## 8. Testing

- **core:** model operations, navigation across song boundaries, clamping at
  ends, JSON round-trip, and validation — including malformed, truncated,
  oversized, wrong-version and out-of-range files.
- **engine:** `FakeEngine` honours the `IEngine` contract (plugin list,
  volume/mute/solo state, levels in range).
- **ui:** list models report correct rows/roles and emit the right change
  signals on edits; navigation controller drives current patch correctly.
- **QML smoke test:** loads `Main.qml` with the fake engine and fails on any
  QML warning or error.
- All of the above run in both the `debug` and `asan` presets.

## 9. Out of scope (later list)

Kept so nothing from the MainStage baseline is lost:

- Layout mode (custom on-screen controller surfaces)
- Screen controls mapped to plugin parameters; MIDI learn
- Tuner, metronome, backing-track/audio playback
- Footswitch / MIDI-controller patch navigation (sub-project 4)
- Real audio/MIDI device setup (sub-project 2)
- Real plugin scanning, loading and editor embedding (sub-project 3)
- Real audio engine and FX processing (sub-project 4)
- Instrument ranking (sub-project 5)
- Light theme / custom styling, touch-optimised layout
- Automatic, timeline or tempo-driven section switching
- Cross-platform builds (macOS/Linux)
