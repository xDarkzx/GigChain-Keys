# Changelog

All notable changes to OpenStage are documented in this file.

## [Unreleased]

### Added

- **Setlists:** songs and patches with navigation (Space / arrows /
  Page Up-Down), rename, reorder, duplicate and delete; JSON setlist files,
  with the last one reopened at startup.
- **Audio engine:** WASAPI by default, ASIO optional, RtMidi input, a
  lock-free render graph and VST3 instrument and effect hosting.
- **Plugin windows** embedded in the main area:
  - Resizable plugins follow the area and zoomable plugins zoom to fit it.
  - Arturia plugins are reloaded at their own window size, with sound and
    settings kept, when the window is maximized, restored or resized.
- **Mixer:** Logic-style strips with instrument and effect slots (bypass,
  replace, remove), pan, volume, meters, mute and solo, plus right-click
  menus. Strips keep a standard height.
- **Instruments browser** showing installed VST3 instruments with each
  maker's own artwork; instruments can be hidden.
- **Settings window** (Audacity 4 layout):
  - **Audio:** driver, device, 44.1–96 kHz and buffer size. Plugins are
    re-prepared in place when the rate or buffer changes.
  - **MIDI:** each input with its own mode and channel. By default only the
    first port is on; the choice is remembered, and keyboards are picked up
    when plugged in or pulled out.
- **Top bar:** CPU and RAM readouts.

### Fixed

- A plugin that ends the test process (for example a fatal error in the
  plugin) is now a test failure, not a pass.
- Plugin windows no longer leave stale images around them when maximized.
