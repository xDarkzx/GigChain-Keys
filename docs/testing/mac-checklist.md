# Testing GigChain Keys on a Mac

Thanks for trying it. This is an early build: something may break, and that
is what this test is for. About 20 minutes.

**You need:** an Apple Silicon Mac (M1 or newer) on macOS 13 (Ventura) or
newer, a USB MIDI keyboard, and a few VST3 plugins already installed (any
instrument; an effect too if you have one). An audio interface is a bonus.

## 1. Install

1. Open `GigChain Keys-<version>-arm64.dmg`.
2. Drag **GigChain Keys** onto **Applications**.
3. Open it from Applications. macOS says it "cannot be opened because Apple
   cannot check it". That is expected: this free beta is not notarized by
   Apple. Click **Done** (not "Move to Bin").
4. Open **System Settings → Privacy & Security**, scroll down to
   "GigChain Keys was blocked…", and click **Open Anyway**. Confirm with
   your password.
5. From then on it opens normally.

## 2. What to try

Tick each one, and note anything odd (a screenshot helps):

- [ ] It starts, and the plugin scan finds your VST3 plugins (Settings shows
      how many).
- [ ] Load an instrument on a channel and **open its window**: it is the
      right size, sharp (not blurry), not cut off, and it redraws when you
      turn its knobs.
- [ ] Play it from the **USB keyboard**. The sound comes out of your Mac, or
      your interface if you picked it in Settings.
- [ ] Add an effect; open its window too.
- [ ] Turn on an **audio input** (Settings). macOS asks once whether
      GigChain Keys may use the microphone: click **Allow**. The input then
      works.
- [ ] Make a small setlist (two songs, two patches each) and switch between
      them while holding a note.
- [ ] **Unplug the keyboard**, wait a few seconds, plug it back in: it plays
      again. Do the same with the audio interface if you have one.
- [ ] Quit (⌘Q) and start again: the setlist comes back.

## 3. If something goes wrong

Send these (zip them):

- The app's logs and settings: in Finder, **Go → Go to Folder…** and paste
  `~/Library/Application Support/GigChain/GigChain Keys/`
- Its crash reports, when the app said it had crashed: the folder it named.
- macOS's own crash report if the app vanished: **Console → Crash Reports**,
  anything named GigChain Keys.
- What you were doing, and which plugin was open.
