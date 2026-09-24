# OpenStage

An open-source, Windows-first live-performance host for keyboard players, in
the spirit of MainStage. Load the VST3 instruments and effects already
installed on your machine, build a setlist of songs and patches, and switch
sounds instantly on stage.

> **Status: early development.** It plays, but file formats and features
> will still change.

## What works today

- **Setlists:** songs and patches, with rename, reorder, duplicate and
  delete. Space / arrow keys switch patches; Tab enters full-screen
  **Perform** mode.
- **VST3 hosting:** each installed instrument's own window fills the main
  area. Plugins that support it resize or zoom to fit; Arturia plugins are
  reloaded at their own window size when the window is maximized or restored.
- **Mixer:** Logic-style channel strips along the bottom, each with an
  instrument slot, effect slots (bypass, replace, remove), pan, volume, meters,
  mute and solo. Right-click a strip for everything else.
- **Instruments browser:** your installed VST3 instruments, shown with each
  maker's own artwork where the plugin provides it.
- **Settings** (Ctrl+,):
  - **Audio:** Windows Audio (WASAPI) or ASIO, device, sample rate and buffer
    size. Plugins are re-prepared, not reloaded, when these change.
  - **MIDI:** each input with its own mode and channel. By default only the
    first port of a keyboard plays; plugged-in keyboards are picked up
    automatically.

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
.\tools\run.ps1                     # start OpenStage
```

Other presets: `release`, and `asan` (AddressSanitizer). To build and test
one target, pass `-Target <name> -Filter <test>`.

## Layout

| Folder | What lives there |
|---|---|
| `src/core` | Setlist model, JSON files, navigation and editing, logging |
| `src/engine` | Audio and MIDI devices, the render graph, VST3 hosting; the only code that touches the SDKs |
| `src/ui` | The QML interface and the C++ models behind it |
| `src/app` | Startup: wires the engine, settings and UI together |
| `tests` | Qt Test suites for every module |
| `docs/superpowers` | Design specs and implementation plans |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: see
[SECURITY.md](SECURITY.md).

## License

OpenStage is licensed under the **GNU General Public License v3.0** — see
[LICENSE](LICENSE). It builds on Qt (LGPL-3.0), the Steinberg VST3 SDK (MIT),
the Steinberg ASIO SDK (GPL-3.0), RtAudio and RtMidi (MIT).

VST is a registered trademark of Steinberg Media Technologies GmbH. ASIO is a
trademark of Steinberg Media Technologies GmbH.
