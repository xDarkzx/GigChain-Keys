# Contributing to OpenStage

Thanks for your interest in contributing! Bug fixes, features, tests and docs
are all welcome.

## Development setup

See **Building** in the [README](README.md): Visual Studio 2022+, Qt 6.10
(`msvc2022_64`), vcpkg, then:

```powershell
.\tools\build.ps1 -Preset debug
```

## Running tests

`tools\build.ps1` runs every test after building. To run one suite:

```powershell
.\tools\build.ps1 -Preset debug -Target tst_models -Filter tst_models
```

A test passes only when Qt Test reports its own summary with nothing failed
(`tests/RunQtTest.cmake`). A plugin that ends the process early counts as a
failure, even if the exit code is 0.

Tests that need real hardware or installed plugins (an audio device, a MIDI
keyboard, Arturia Piano V2, …) skip themselves when those are missing. CI runs
without them, so run the full suite on your own machine before opening a PR.

All tests must pass before a PR is merged.

## Code rules

These are enforced in review, and several by the compiler (`/W4 /WX /sdl`):

- **Never swallow errors.** Every failure is returned with its precise cause
  (`core::Result<T>`) **and** logged. No silent fallbacks.
- **No timers or delays to paper over ordering.** React to the real event
  (window state changes, `WM_EXITSIZEMOVE`, `afterAnimating`, signals).
- **SDK code stays behind its module.** Only `src/engine/internal` includes
  the VST3 SDK, RtAudio or RtMidi. The UI talks to `IEngine` alone.
- **Real-time safety.** The audio callback never allocates, locks or logs; it
  counts problems for the main thread to report.
- **Test first.** Write the test, watch it fail, then make it pass.
- Match the surrounding code's naming, comments and idioms. `.clang-format`
  holds the formatting.

## Workflow

1. Branch from `main` (`feat/…`, `fix/…`).
2. Keep commits focused, with messages that say what changed and why.
3. Open a pull request into `main`. CI must pass before merging.
4. Add a line under **Unreleased** in [CHANGELOG.md](CHANGELOG.md) for
   anything a user would notice.
