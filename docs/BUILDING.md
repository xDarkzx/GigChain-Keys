# Building GigChain Keys from source

GigChain Keys is C++20 with Qt 6.10 (QML), the Steinberg VST3 SDK, RtAudio
and RtMidi, built with CMake presets. Windows is the main platform; Linux and
macOS (Apple Silicon) build from the same sources.

- [Windows](#windows)
- [Linux (and WSL)](#linux-and-wsl)
- [macOS (Apple Silicon)](#macos-apple-silicon)
- [The Windows installer](#the-windows-installer)
- [Checks before a commit](#checks-before-a-commit)
- [Name and branding](#name-and-branding)
- [Source layout](#source-layout)

## Windows

Requirements (Windows 10/11, x64):

- Visual Studio 2022 or newer with the C++ workload
- CMake 3.24+ and Ninja (both ship with Visual Studio)
- Qt 6.10 for MSVC 2022 x64 (`msvc2022_64`)
- [vcpkg](https://github.com/microsoft/vcpkg); RtAudio (with ASIO), RtMidi
  and the other libraries are installed from `vcpkg.json`. The Steinberg
  VST3 SDK is fetched by CMake.

```powershell
$env:VCPKG_ROOT  = 'C:\path\to\vcpkg'
$env:QT_ROOT_DIR = 'C:\Qt\6.10.2\msvc2022_64'
.\tools\build.ps1 -Preset debug     # configure, build and run every test
.\tools\build.ps1 -Preset release -NoTest
.\tools\run.ps1 -Preset release     # start GigChain Keys
```

`tools\run.ps1` only starts the app (with Qt's DLLs on the path); build first.
Other presets: `asan` (AddressSanitizer). To build and test one target, pass
`-Target <name> -Filter <test>`.

## Linux (and WSL)

Ubuntu 22.04 or newer, x64; on Windows, WSL 2 with WSLg (it shows Linux
windows and plays their sound). The first time:

```bash
sudo bash tools/setup-linux.sh --system   # packages, GCC 13, Clang 15 and 18, Surge XT (a free test instrument)
bash tools/setup-linux.sh --user          # CMake, Qt 6.10.2, vcpkg
bash tools/setup-linux.sh --check         # what is installed
```

Then:

```bash
bash tools/verify.sh        # configure, build and run every test (linux-debug)
bash tools/run.sh           # start GigChain Keys (linux-release)
bash tools/soak.sh 20       # the 20-minute soak, in a terminal of its own
bash tools/fuzz.sh 300      # the fuzzers, 5 minutes each (Clang)
```

Presets: `linux-debug`, `linux-release`, `linux-clang` (Clang 18, `-Werror`),
`linux-asan` (AddressSanitizer and UndefinedBehaviorSanitizer), `linux-fuzz`.
Builds go to `~/.cache/gigchain/build/<preset>` (the machine's own disk, fast
even when the sources are on a Windows drive). Audio: PulseAudio (also on
PipeWire, and in WSLg), JACK or ALSA; MIDI: ALSA (WSL has no MIDI devices).
Plugin windows are X11 windows, also on Wayland desktops.

What differs by system lives in per-system files (`*_win.cpp`,
`*_posix.cpp`, `*_linux.cpp`, `*_mac.cpp`), mostly in `src/platform`; a test
fails if system code appears anywhere else.

## macOS (Apple Silicon)

Apple Silicon Macs on macOS 13 or newer. The Mac build runs on GitHub's Mac
machines (`.github/workflows/mac.yml`): **Actions → Mac → Run workflow**, or
push a release tag (`v*`). It builds with the `mac-release` preset, runs every
test (with Surge XT from Homebrew as the test instrument), and makes
`GigChain Keys-<version>-arm64.dmg` (the run's artifact): the app with Qt
inside, ad-hoc signed, not notarized. Testers open it once through **System
Settings → Privacy & Security → Open Anyway**
([testing/mac-checklist.md](testing/mac-checklist.md)).

On a Mac of your own: Xcode (or its command-line tools), Qt 6.10.2 for macOS,
vcpkg, CMake and Ninja; then:

```bash
cmake --preset mac-release
cmake --build --preset mac-release
ctest --preset mac-release
bash tools/package-mac.sh build/mac-release <version>   # the .dmg
```

Audio: Core Audio; MIDI: Core MIDI; plugins from
`~/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/VST3`.

Before any Mac build, `linux-clang` (Clang 18, Apple's compiler family)
checks the code at home: `bash tools/setup-linux.sh --system` installs it.

## The Windows installer

```powershell
winget install JRSoftware.InnoSetup   # once
.\tools\package.ps1                   # Release build, every test, then dist\
```

`dist\` then holds `GigChainKeys-<version>-x64-setup.exe` (the Windows
installer: for everyone or just you, upgrades in place, opens `.gigchain`
setlists, uninstalls from Apps & Features) and a portable zip of the same
files. The installer is described in [`installer/setup.iss`](../installer/setup.iss);
its pictures come from `tools\make-installer-art.ps1`. Publishing a version
is described in [RELEASING.md](RELEASING.md).

## Checks before a commit

`tools\verify.ps1` (Windows; `tools/verify.sh` on Linux) is the gate every
commit passes, run by the commit hook (`tools\verify-hook.ps1`):

- a build with every warning an error (`/W4 /WX`);
- clang-tidy and cppcheck on the changed files;
- qmllint on the changed QML;
- the whole test suite (`ctest`).

It remembers its verdict for the same changes; `-Force` runs it again. Tests
that need real hardware or installed plugins (an audio device, a MIDI
keyboard, Arturia Piano V2…) skip themselves when those are missing.

## Name and branding

The product's name, version, executable, settings folder, setlist file
extension and splash picture all come from
**[`branding.cmake`](../branding.cmake)**. To rename, edit that file (and swap
`branding/splash.png`, which has the name in it) and rebuild. A test fails if
a product name is typed anywhere else in the source. Settings saved under
earlier names carry over on first start.

## Source layout

| Folder | What lives there |
|---|---|
| `src/core` | Setlist model, JSON files, charts, navigation and editing, logging |
| `src/platform` | What differs by system (crash reports, one app at a time, plugin folders, windows…): one file per system |
| `src/engine` | Audio and MIDI devices, the render graph, the loop station, VST3 hosting; the only code that touches the SDKs |
| `src/ui` | The QML interface and the C++ models behind it |
| `src/app` | Startup: wires the engine, settings and UI together |
| `tests` | Qt Test suites for every module |
| `docs/help` | The user guide (built into the app and shown on GitHub) |
| `docs/superpowers` | Design specs and implementation plans |
| `installer`, `tools` | The Windows installer, build, check and packaging scripts |
