# Live test control (developer builds only)

A way to drive the real, running app from a terminal, so a fix is checked
live (the reporter's steps, in the real app, measured) and not only by unit
tests. Never part of a release.

## How to use it

```powershell
tools\build.ps1 -Preset dev -NoTest       # once; then incremental
build\dev\gck-ctl start                   # the dev app, listening (start <setlist> opens one)
build\dev\gck-ctl help                    # every command with its arguments
build\dev\gck-ctl status
build\dev\gck-ctl call document addChannel "C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3" Piano
build\dev\gck-ctl play "on 60 100; wait 400; off 60"
build\dev\gck-ctl level 500
build\dev\gck-ctl shot $env:TEMP\window.png
build\dev\gck-ctl quit
```

`gck-ctl run <file>` runs a file of commands, one a line, and stops at the
first that fails. `expect <field> <op> <value>` checks the answer before it,
so a file passes or fails on its own; `$mark` is the last `mark`, `$tmp` a
scratch folder. The live checks of fixes are in [tests/live](../../tests/live).
Every answer is JSON with `"ok"` (and `"error"` saying why when not) and `"ms"`
(how long it took). Every command reaches the app's log as `Dev control: ...`.

The tool's own commands: `start [setlist]`, `kill`, `kill-at <pattern> [ms]
[command...]` (ends the app the moment its log matches: a kill during a plugin
load), `restart [setlist]`, `sleep <ms>`, `run <file>`.

`tools\run.ps1 -Preset dev -DevControl` starts the app the same way.

## Why

Unit tests and the verification gate prove the code; they do not prove that a
player gets the fix. Checking by hand means a person clicking through every
fix. With this, a developer (or a coding assistant working for one) opens a
song, plays notes into it, opens a dialog, reads the levels and the log and
takes a screenshot, from a script, before and after a fix.

## Goals

- Drive the running app: open a setlist, pick a song and sound, add a
  channel, play notes and MIDI sequences, open dialogs and pages, press
  on-screen controls, switch a blocked plugin back on, quit.
- Read it back: status (song, sound, loaded and blocked plugins, audio
  device), the output and channel levels over a stretch of time, the log
  since a mark, a screenshot of the window.
- Every request answers with what happened or the exact reason it could not
  (never silent).

## Non-goals

- A player feature, remote control from a phone or another computer, OSC.
- An MCP server (may wrap the same commands later, outside this repo).
- Replacing the unit tests or the gate: it adds the live check on top.

## Anti-goals (a failure even if it works)

- Any way in from a shipped build. Release builds, installers and CI
  artifacts must not contain the control code at all.
- Any change to how the app behaves or performs when it is not switched on.
  Nothing on the audio thread, in any build (`record` uses the app's own
  performance recorder).
- Reachable from the network or by another user on the machine.

## Approaches considered

- **A. A control socket inside the app plus a small command-line tool**
  (chosen). Reads the engine's real state, uses the app's own actions, works
  today without installing anything.
- B. A plus an MCP server: nicer for an assistant, but another piece to keep,
  and AI tooling stays out of this repo. Possible later, outside it.
- C. Outside-in UI automation and a virtual MIDI cable: nothing to remove
  for releases, but flaky with Qt Quick and blind to the engine's state.

## How it is switched on: three locks

1. **Compiled in only for the `dev` preset.** A CMake option
   `GIGCHAIN_DEV_CONTROL` (default OFF). A new preset `dev` (Release build
   type, its own folder `build/dev`) sets it ON; `debug`, `release`, `asan`,
   CI and the packaging scripts never do. A separate folder means a release
   configure can never pick the option up from an old cache.
2. **Started only when asked.** Even in a dev build the control listens only
   when the app starts with `--dev-control` (`tools/run.ps1 -DevControl`).
3. **Checked at packaging.** `tools/package.ps1` (and the Linux and Mac
   packaging scripts) pass `-DGIGCHAIN_DEV_CONTROL=OFF` and fail if the
   packaged executable contains the control's marker string.

## The channel

- A local socket (Qt `QLocalServer`; a named pipe on Windows, a Unix socket
  elsewhere), named after the app plus `-dev-control`, open to this user
  only (`UserAccessOption`). No TCP, nothing on the network.
- One JSON object per line each way. Request: `{"cmd": "note", "args": {...}}`.
  Answer: `{"ok": true, ...}` or `{"ok": false, "error": "<why>"}`.
- Runs on the main thread (the app's own actions and engine calls are main
  thread); a long measurement (levels over time) answers when it is done,
  without blocking the window (a timer, not a sleep).

## Commands: everything a live debugging session needs

The core is generic, so every feature the app has, and every one added later,
can be driven and read without writing a new command: anything the screen can
do goes through the same controllers the screen uses. Dedicated commands
cover what the screen cannot reach (raw MIDI, measurements, the app's life,
its files).

### 1. The app's own actions and state (generic)

The screen's controllers by name: `document`, `settings`, `panel`, `practice`,
`warmup`, `loop`, `mixer`, `masterBus`, `sustainPads`, `engineStatus`,
`notifications`, `instrumentWindows`, `effectWindows`, `editor`, `help`,
`community`, `pluginList` (every QObject the QML is given).

| Command | Does | Answers |
| --- | --- | --- |
| `objects` | lists the controllers | names |
| `methods <controller>` / `props <controller>` | what it offers (Q_INVOKABLEs with their arguments, Q_PROPERTYs) | the list |
| `call <controller> <method> [args...]` | calls an action: open a setlist, pick a song or sound, add or remove a channel, set volume / mute / solo / pan / sends, zones, transpose, chords, the song's key, tempo, parts, looper, warm-ups, settings pages, unblock a plugin... | its return value |
| `get <controller> <property>` / `set <controller> <property> <value>` | reads / sets a property | the value |
| `status` | one summary: setlist, song, sound, channels (plugin, keyboard, zone, mute/solo, volume), loaded and blocked plugins, audio device, rate and buffer, MIDI inputs (playing / buttons-and-knobs / off), tempo, playing or not | JSON |

### 2. The screen (generic)

| Command | Does | Answers |
| --- | --- | --- |
| `ui-tree [name]` | the visible items under a named one (or the whole window, popups and dialogs included): objectName, type, text, visible, enabled, geometry | the tree |
| `ui-get <name> <property>` / `ui-set <name> <property> <value>` / `ui-call <name> <method> [args]` | on a named item (open Settings and pick its page, scroll a list) | the result |
| `ui-click <name>` / `ui-hover <name>` / `ui-drag <name> <dx> <dy>` | real mouse events on it (a tooltip shows on hover) | ok, or not found / not visible / disabled |
| `key <keys>` | key presses to the window (`Ctrl+S`, `Escape`, `Space`) | ok |
| `resize <w> <h>` | the window's size (a player's small or HiDPI screen) | the size it took |
| `shot <file> [name]` | a picture of the window, or of one item | the file |
| `plugin-window <channel> [effect]` | opens a plugin's own window | its size; `shot` takes it too |

### 3. MIDI, as a keyboard sends it

| Command | Does |
| --- | --- |
| `note <key> <velocity> [channel] [keyboard]` | a key down (velocity 0: up) |
| `cc <number> <value> [channel] [keyboard]` | a controller: sustain (64), mod wheel, expression... |
| `midi <hex bytes> [keyboard]` | any message: pitch bend, program change, aftertouch, MIDI clock, Start/Stop |
| `play <script>` | a timed sequence: `on 60 100; wait 50; on 60 100; wait 50; off 60` |

`keyboard` names the keyboard it comes from, as if that input sent it (a
channel set to one keyboard, #51); without it, from every keyboard. They enter
where a real keyboard's messages enter (the engine's input queue), not at a
plugin. Needs one engine call added: inject a raw message with its source.

### 4. Measuring

| Command | Answers |
| --- | --- |
| `level <ms> [channel]` | the highest peak and RMS of the output (or a channel) over that long, sampled every 5 ms |
| `record <seconds> <file.wav>` | the mix as the audience hears it (the app's performance recorder), to listen to or compare |
| `cpu <ms>` | the audio load, highest and average; dropouts and late blocks in that time |
| `keys` | the keys and pedal the instruments hear now (held notes left behind show here) |
| `process` | the memory the app uses now (leaks while debugging) |
| (every answer) | `ms`: how long the command took (a song switch, a plugin load) |
| `windows` | the app's open windows by title (a plugin's own: `shot <file> window:<title>`) |

### 5. What the app said

| Command | Answers |
| --- | --- |
| `mark` | a position in the log and the messages shown |
| `log [since mark] [match]` | the log lines written since, optionally only those matching |
| `notices [since mark]` | the messages and warnings the player saw (toasts, banners) |
| `wait <match> <ms>` | waits until a log line or notice matches, or says it never came |

### 6. The app's life

| Command | Does |
| --- | --- |
| `quit` | closes the app the normal way (its saving and closing run) |
| `crash` | crashes it on purpose (the crash reporter's path, #55) |
| `freeze <ms>` | holds the main thread (the freeze watchdog's path) |

The command-line tool also starts the app (`start [setlist]`, with
`--dev-control`), kills it (`kill`, or `kill-at <match>`: the moment a log
line appears, as when a player kills it during a plugin load) and waits for it
to come back (`restart`).

### 7. Its files

| Command | Does |
| --- | --- |
| `paths` | where the settings, logs, crash reports, plugin guard and caches live |
| `read <which>` | reads one of them (blocked plugins, the guard's markers, settings) |
| `guard-block <plugin>` | puts a plugin on the switched-off list, as a crash would (instead of editing the file by hand) |

### The command-line tool

`gck-ctl` (a small Qt console program built only with the same option) sends
one command and prints the answer, or runs a file of commands, one per line,
stopping at the first error: `build/dev/gck-ctl level 1000`,
`build/dev/gck-ctl run checks/issue-53.txt`. Every command the app receives is
written to its log as `Dev control: <command>`, so a session can be read
back afterwards.

## Testing it

- A unit test for the request parsing and the answers (bad JSON, unknown
  command, missing arguments: each a clear error).
- A smoke test that starts the control with the fake engine and runs one
  command from each group against the real Main.qml.
- Used for real at once: the live checks of #53, #55, #56 and #57 are its
  first users (written as command files).
- A packaging check that a release build has no control code (lock 3).

## Keeping it working

- The verification gate also builds the `dev` preset (with /W4 /WX and the
  same static analysis) whenever the control's files or the engine and UI
  calls it uses change, so code compiled only there cannot rot unseen.
- CI builds the `dev` preset too (built and tested, never packaged or
  uploaded).
- Cost: a second build folder (`build/dev`), one full build the first time,
  then incremental.

## Left to the builder

- Exact class layout (one `DevControl` class in its own folder, linked into
  the app only when the option is ON; `main.cpp` starts it under the option).
- Which existing controller each command calls (DocumentController,
  SettingsController, IEngine), and the marker string's wording.
