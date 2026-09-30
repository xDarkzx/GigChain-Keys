# GigChain Keys

An open-source live-performance host for keyboard players, in the spirit of
MainStage, for Windows and Linux (macOS next). Load the VST3 instruments and effects already
installed on your machine, build a setlist of songs and patches, and switch
sounds instantly on stage.

> **Status: early development.** It plays, but file formats and features
> will still change.

## What works today

- **Setlists:** songs and patches, with rename, reorder, duplicate and
  delete. Space / arrow keys switch patches; Tab enters full-screen
  **Perform** mode.
- **VST3 hosting:** each installed instrument's own window shows in the main
  area, shrunk to fit when the area is smaller and never cut off, keeping
  its own shape.
- **Mixer:** Logic-style channel strips along the bottom, each with an
  instrument slot, effect slots (bypass, replace, remove), pan, volume, meters,
  mute and solo. Right-click a strip for everything else.
- **Instruments browser:** your installed VST3 instruments, shown with the
  artwork each plugin installs in its own folder.
- **Settings** (Ctrl+,):
  - **Audio:** Windows Audio (WASAPI) or ASIO, device, sample rate and buffer
    size. Plugins are re-prepared, not reloaded, when these change.
  - **MIDI:** each input with its own mode and channel. By default only the
    first port of a keyboard plays; plugged-in keyboards are picked up
    automatically.

## What's planned

Instrument toggles and song sections, a loop station, EDM stack presets with
built-in sidechain, a tablet remote for the music stand, and macOS and Linux
versions: see
[docs/ROADMAP.md](docs/ROADMAP.md).

## Building

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
.\tools\run.ps1                     # start GigChain Keys
```

Other presets: `release`, and `asan` (AddressSanitizer). To build and test
one target, pass `-Target <name> -Filter <test>`.

### Linux (and WSL)

Ubuntu 22.04 or newer, x64; on Windows, WSL 2 with WSLg (it shows Linux
windows and plays their sound). The first time:

```bash
sudo bash tools/setup-linux.sh --system   # packages, GCC 13, Clang 15, Surge XT (a free test instrument)
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

Presets: `linux-debug`, `linux-release`, `linux-asan` (AddressSanitizer and
UndefinedBehaviorSanitizer), `linux-fuzz`. Builds go to
`~/.cache/gigchain/build/<preset>` (the machine's own disk, fast even when
the sources are on a Windows drive). Audio: PulseAudio (also on PipeWire, and
in WSLg), JACK or ALSA; MIDI: ALSA (WSL has no MIDI devices). Plugin windows
are X11 windows, also on Wayland desktops.

What differs by system lives in per-system files (`*_win.cpp`,
`*_posix.cpp`, `*_linux.cpp`), mostly in `src/platform`; a test fails if
system code appears anywhere else. macOS is next (docs/ROADMAP.md).

### The installer

```powershell
winget install JRSoftware.InnoSetup   # once
.\tools\package.ps1                   # Release build, every test, then dist\
```

`dist\` then holds `GigChainKeys-<version>-x64-setup.exe` (the Windows
installer: for everyone or just you, upgrades in place, opens `.gigchain`
setlists, uninstalls from Apps & Features) and a portable zip of the same
files. The installer is described in [`installer/setup.iss`](installer/setup.iss);
its pictures come from `tools\make-installer-art.ps1`.

## Name and branding

The product's name, version, executable, settings folder, setlist file
extension and splash picture all come from **[`branding.cmake`](branding.cmake)**.
To rename, edit that file (and swap `branding/splash.png`, which has the name
in it) and rebuild. A test fails if a product name is typed anywhere else in
the source. Settings saved under earlier names carry over on first start.

## Layout

| Folder | What lives there |
|---|---|
| `src/core` | Setlist model, JSON files, navigation and editing, logging |
| `src/platform` | What differs by system (crash reports, one app at a time, plugin folders, windows...): one file per system |
| `src/engine` | Audio and MIDI devices, the render graph, VST3 hosting; the only code that touches the SDKs |
| `src/ui` | The QML interface and the C++ models behind it |
| `src/app` | Startup: wires the engine, settings and UI together |
| `tests` | Qt Test suites for every module |
| `docs/superpowers` | Design specs and implementation plans |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: see
[SECURITY.md](SECURITY.md).

## License

GigChain Keys is licensed under the **GNU General Public License v3.0** — see
[LICENSE](LICENSE). It builds on Qt (LGPL-3.0), the Steinberg VST3 SDK (MIT),
the Steinberg ASIO SDK (GPL-3.0), RtAudio and RtMidi (MIT).

VST is a registered trademark of Steinberg Media Technologies GmbH. ASIO is a
trademark of Steinberg Media Technologies GmbH.
