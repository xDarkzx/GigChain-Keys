# Cross-platform, piece 1: the platform layer and Linux

GigChain Keys is Windows-only today. This is the first of four pieces that
make it the same app on Windows, macOS and Linux before it goes public as
free, open-source software:

1. **The platform layer and Linux** (this document).
2. macOS: the Mac files of the platform layer, a build on GitHub's Mac
   machines for Apple Silicon, an ad-hoc-signed `.dmg`.
3. Automatic builds and packages for all three (a GitHub workflow; the
   Windows installer, the `.dmg`, a Linux AppImage).
4. Going public: the pre-publication check, licence notices, the landing
   page, the first public beta.

Each piece gets its own design, plan and build.

## The goal

- **Windows stays exactly as it works today.** Its code is moved, not
  rewritten, and every existing test passes at every step.
- **Linux works the way Reaper does on Linux**: plugin windows are X11
  windows (on Wayland through its X11 layer), the host runs the plugins'
  timers and events, the standard VST3 folders, and the choice of
  PulseAudio, JACK and ALSA.
- **Done** means: the Linux build passes its tests, and the app runs in WSL
  (WSLg), loads a free Linux VST3 instrument (Surge XT), opens its window,
  resizes it as the plugin asks, and plays it (on-screen keyboard and
  simulated notes: USB keyboards do not reach WSL; real keyboards are
  tested on Windows, and on a friend's Mac in piece 2).
- The same features on every system: VST3 only (Audio Units on the Mac are
  a later question).

## The platform layer

A new library, `src/platform/` (namespace `gigchain::platform`), holds every
piece of code that differs by system. Each piece is one small interface with
one file per system, chosen by CMake:

- `<piece>.h` — the interface, shared.
- `<piece>_win.cpp` — Windows: the code as it is today, moved here.
- `<piece>_posix.cpp` — Linux and macOS, where they work alike.
- `<piece>_linux.cpp` / `<piece>_mac.mm` — where they differ (Mac files are
  piece 2; piece 1 leaves room for them).

Code outside `src/platform/` has no `#ifdef _WIN32` and no system headers
(`windows.h`, `unistd.h`, ...); a check in the build enforces it. This is the
pattern of Steinberg's VST3 SDK (`module_win32` / `module_linux` /
`module_mac`) and of Audacity.

| Piece | Windows (moved) | Linux | Mac (piece 2) |
|---|---|---|---|
| Crash reports | crash dump (MiniDump) and the note of what the app was doing | crash signals (SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL) caught on an alternate stack; the crash note and a backtrace written to the same crash folder; the next start says so, as on Windows | the POSIX file |
| App already running | named mutex and pipe (the pass-4 permissions) | a lock file decides who is first; the setlist is handed over through a Qt local socket that only this user can open | the POSIX file |
| Process hardening | DLL search path cut to the app's folder | nothing: plugins load by full path, and "only from the plugin folders" is shared code already | nothing |
| No error pop-ups when a plugin fails to load | error-mode guard | nothing (Linux shows none) | nothing |
| The plugin scanner | no console window; a crash ends it quietly | a crash signal ends it quietly with an exit code the app already understands | the POSIX file |
| Standard plugin folders | `C:\Program Files\Common Files\VST3` | `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3` | `~/Library/Audio/Plug-Ins/VST3`, `/Library/Audio/Plug-Ins/VST3` |
| RAM in the status bar | process memory counters | `VmRSS` from `/proc/self/status` | task info |
| Precise MIDI clock timer | 1 ms timer resolution | nothing (Linux timers are precise) | nothing |
| Bring the window forward (a second start) | the topmost trick | Qt's raise and activate (the window manager may decline; normal on Linux) | Qt's raise |
| Loading a plugin's module | Steinberg's `module_win32.cpp` | Steinberg's `module_linux.cpp` | `module_mac.mm` |
| Compiler safety flags | MSVC hardening, as now | GCC/Clang: stack protector, `_FORTIFY_SOURCE=3`, PIE, full RELRO, no executable stack | Clang equivalents |

Every failure in a platform piece is returned with its precise cause and
logged, as everywhere else (never silent).

## Plugin windows on Linux

- **Which window a plugin gets:** `IPluginEditor::attach` takes the native
  parent and now also says which kind it is (a Windows handle, an X11 window
  id, or later a Mac view), mapped to VST3's `kPlatformTypeHWND`,
  `kPlatformTypeX11EmbedWindowID` and `kPlatformTypeNSView`. A plugin that
  does not support the kind is refused with that reason, as now.
- **X11 even on Wayland:** on Linux the app runs its windows through Qt's
  X11 platform (`xcb`), which works on Wayland desktops and WSLg through
  their X11 layer. Linux plugins draw into X11 windows; this is what Reaper
  and Bitwig do. `tools/run.sh` and the Linux app's start-up set it, unless
  the player sets another on purpose.
- **The host runs the plugin's timers and events** (Steinberg's
  `Linux::IRunLoop`, which a plugin asks the host's `IPlugFrame` for): event
  handlers on file descriptors through Qt's socket notifiers, and timers
  through Qt timers, all on the main thread. Every handler and timer a
  plugin registers is removed when it asks, and all of them when its window
  closes (a plugin that forgets cannot leave a timer calling into a closed
  editor).
- **Size:** the plugin owns its size, as on Windows: we follow its
  `resizeView` requests and scale for the screen only.
- The Windows-only background-erase workaround in `PluginEditorHost` stays
  in the Windows file.

## Audio and MIDI

| | Windows (unchanged) | Linux | Mac (piece 2) |
|---|---|---|---|
| "System audio", the default | WASAPI | PulseAudio (also on PipeWire desktops, and in WSLg) | Core Audio |
| The pro option | ASIO | JACK | — (Core Audio is low-latency already) |
| Also offered | — | ALSA, direct to the hardware | — |
| MIDI | Windows MIDI | ALSA MIDI | Core MIDI |

- `AudioDriver` gains `Jack` and `Alsa`. Settings offers only the drivers
  the system has, in the same layout. A saved "System" means that system's
  default; a saved driver the system lacks (a setlist moved from Windows with
  ASIO) falls back to System, logged and told once.
- JACK follows ASIO's rules: if it stops or resets, the device is reopened,
  or the app falls back to System audio and says so.
- RtAudio and RtMidi are built with those backends on Linux (our ports
  already carry the Windows fixes; the Linux backends come with them).

## Building on Linux

- Built in WSL Ubuntu 22.04 with GCC 13 (22.04's GCC 11 lacks C++20 parts
  the code uses) and a current CMake (the project needs 3.24; 22.04 has
  3.22). Qt 6.10 for Linux (the official binaries), everything else from
  vcpkg (`x64-linux`), with our RtAudio and RtMidi ports.
- New presets `linux-debug`, `linux-release`, `linux-asan`; `tools/run.sh`
  starts the app (X11 mode), like `tools/run.ps1`; `tools/verify.sh` builds
  and runs every test on Linux.
- The VST3 hosting support compiles `module_linux.cpp` on Linux.
- The Windows build, its presets, its gate and its hooks do not change.

## Tests

- Every test that is not about Windows itself runs on Linux too.
- Tests that need a real instrument use one that is installed (Piano V2 on
  Windows as now; Surge XT on Linux) and skip, saying so, when there is none.
- New tests, each written to fail first:
  - a crash (in a child process) leaves its crash note and backtrace, and
    the next start reports it;
  - a second start hands its setlist to the first, and a stale lock file
    left by a crash does not block a new start;
  - the standard plugin folders, and a Linux VST3 bundle found and loaded
    (`.vst3/Contents/x86_64-linux/*.so`);
  - the RAM reading is sane;
  - a plugin's run-loop timers and file handlers fire, and are gone after
    it unregisters them or its window closes;
  - the driver list per system, and a saved ASIO setting on Linux falling
    back to System.
- ASan and the fuzzers run on Linux (GCC/Clang sanitizers), and the soak runs
  on Linux against Surge XT. Linux adds checking for the shared code too.
- The Windows gate (`tools\verify.ps1`, the commit hook) stays green on every
  commit.

## Not in this piece

- The Mac: its files, its build and its `.dmg` (piece 2).
- The AppImage, and builds on GitHub for all three (piece 3).
- Real USB MIDI keyboards on Linux (WSL has none; usbipd could pass one
  through later).
- Other plugin formats (LV2, CLAP, Audio Units).
