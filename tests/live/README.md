# Live checks

Each file here is a fix or feature checked in the real, running app: the
reporter's steps, measured, passing or failing on their own (`expect`). They
drive the dev build through the live test control
([docs/testing/live-test-control.md](../../docs/testing/live-test-control.md)).

They need what the reporter had: a plugin installed, a keyboard plugged in
(each file says what). They are not part of `ctest` or CI.

```powershell
tools\build.ps1 -Preset dev -NoTest
build\dev\gck-ctl run tests\live\issue-53-repeated-note.txt
```

A file stops at the first command or check that fails, and says which.

`tools\live-checks.ps1` runs every file here on Windows. Checks of what only
Linux does are in [linux/](linux), run from the Linux dev build
(`cmake --preset linux-dev`, then `cmake --build --preset linux-dev`):

```bash
~/.cache/gigchain/build/linux-dev/gck-ctl run tests/live/linux/issue-71-realtime-limit.txt
```
