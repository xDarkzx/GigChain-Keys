# Getting help with GigChain Keys

## 1. Look it up

- **The user guide:** press **F1** in the app, or read it [here on GitHub](docs/help/README.md).
- **Troubleshooting:** no sound, crackles, stuck notes, plugins that will not
  load: [Troubleshooting](docs/help/troubleshooting.md).
- **Known problems:** see the [open issues](https://github.com/xDarkzx/GigChain-Keys/issues);
  yours may be there already, with a way round it.

## 2. Ask, or report a bug

[**Open an issue**](https://github.com/xDarkzx/GigChain-Keys/issues/new/choose)
and pick **Bug report** or **Feature request**. A good bug report says:

- **What you did**, step by step, and **what happened**, then **what you expected**.
- **Your GigChain Keys version:** Help → About.
- **Your system:** Windows 10 or 11 (or macOS version), your audio interface
  and driver (WASAPI or ASIO), your MIDI keyboard.
- **The plugins involved**, with their versions, when the problem is with one.
- **The log file.** It says what the app was doing; attach it to the issue:

| System | Log file | Crash reports |
|---|---|---|
| Windows | `%LOCALAPPDATA%\GigChain\GigChain Keys\logs\GigChainKeys.log` | `%LOCALAPPDATA%\GigChain\GigChain Keys\crash-reports\` |
| macOS | `~/Library/Application Support/GigChain/GigChain Keys/logs/` | `.../crash-reports/` (same folder) |
| Linux | `~/.local/share/GigChain/GigChain Keys/logs/` | `.../crash-reports/` (same folder) |

On Windows, paste `%LOCALAPPDATA%\GigChain\GigChain Keys` into File Explorer's
address bar to get there. If GigChain Keys crashed, it says so the next time it
starts and names the report it saved: attach that too.

A setlist that shows the problem helps a lot (File → Save As). It names the
plugins you use, so check you are happy to share it.

## 3. Security problems

Please **do not** open a public issue for a security problem. Report it
privately as described in [SECURITY.md](SECURITY.md).

## What to expect

GigChain Keys is made by one person, in their own time. There is no fixed
response time, but every report is read. Problems that stop a gig (crashes,
dropouts, lost setlists) come first.

If GigChain Keys helps you, you can support it through
[GitHub Sponsors](https://github.com/sponsors/xDarkzx).
