# Cross-platform piece 2: macOS

The second of four pieces that make GigChain Keys the same app on Windows,
Linux and macOS before it goes public (piece 1: the platform layer and
Linux, `2026-09-30-cross-platform-linux-design.md`; piece 3: packages and
CI; piece 4: the pre-publication check and going public).

## What it delivers

- GigChain Keys builds, passes its tests and runs on **Apple Silicon Macs
  (M1 to M5), macOS 13 or newer** (Qt 6.10's floor; Qt says not to go lower).
  No Intel Macs: it doubles the build for few machines; it can be added later
  without changing the code.
- **Built on GitHub's Mac machines**, in the private repo, **only when
  started by hand or on a release tag** (about 200 free Mac minutes a month
  on a private repo; minutes count ten times). Not on every push.
- **The result:** `GigChain Keys.dmg` holding `GigChain Keys.app` (Qt, its
  QML and the plugin scanner inside), **ad-hoc signed** (free; the Mac can
  tell it is intact), not notarized. A tester opens it once through System
  Settings → Privacy & Security → Open Anyway. Notarization (Apple's
  developer program, US$99 a year) can come later, but it is more than
  signing: it needs the hardened runtime, and with it entitlements for a
  plugin host: `com.apple.security.cs.disable-library-validation` (to load
  other makers' plugins), `com.apple.security.device.audio-input`, and for
  some plugins JIT or unsigned-memory ones. Plugins the hardened runtime
  refuses are the risk to test then. (Found by the final review.)
- **Tested on a friend's M5** with a real keyboard and plugins (checklist
  below).
- Windows and Linux behave exactly as today.

## What was studied

- Audacity 4's Mac CI (`.github/workflows/au4_build_macos.yml`,
  `buildscripts/ci/macos/`): the Mac machine and a pinned Xcode, Qt from
  `jurplel/install-qt-action` (`host: mac`, `arch: clang_64`), a build cache
  saved even when the build fails, packaging to a `.dmg`.
- Muse's VST3 view (`muse/framework/vst/qml/Muse/Vst/vstview.cpp`): plugins
  get `kPlatformTypeNSView`; on the Mac the pixel ratio is 1.0 and the
  plugin is not given a content scale (Cocoa works in points).
- Steinberg's `editorhost` sample (`public.sdk/samples/vst-hosting/editorhost`):
  `module_mac.mm` compiled with `-fobjc-arc`, linked with `-framework Cocoa`.
- RtAudio 6.0.1 and our RtMidi commit: Core Audio and Core MIDI are on by
  default on Apple; their exported targets carry absolute paths to Xcode's
  frameworks.
- Qt 6 on macOS (doc.qt.io/qt-6/macos.html): macOS 13+, arm64, Xcode 15+.
- Homebrew's `surge-xt` cask (a `.pkg`: VST3 into the standard plugin folder).

## What would break a first Mac build, and the change for each

| Would break | Change |
|---|---|
| `-Wl,-z,relro,-z,now,-z,noexecstack`: Apple's linker has no `-z` | a Mac branch in `gigchain_harden`: no `-z` flags (the Mac's linker makes stacks non-executable and binaries position-independent by default) |
| `-fstack-clash-protection`: not supported for Apple targets (an unused-flag warning, an error under `-Werror`) | not passed on the Mac; `-fstack-protector-strong` stays |
| `-D_FORTIFY_SOURCE=3`: Apple's SDK defines it already (a redefinition warning, an error) | not passed on the Mac (the SDK's own fortify stays on) |
| `module_mac.mm` is Objective-C++; the project enables only C++ | `enable_language(OBJCXX)` on Apple; `vst3_host_support` gets `module_mac.mm` with `-fobjc-arc` and links `-framework Cocoa` (as Steinberg's sample) |
| `gigchain_platform_sources` knows Windows and Linux only | a Mac branch: `<base>_posix.cpp` plus `<base>_mac.cpp` or `<base>_mac.mm` |
| The app finds the scanner next to itself; on the Mac the app is inside a bundle | the scanner is built into `GigChain Keys.app/Contents/MacOS/` (in the build folder too, so tests find it) |
| vcpkg builds libraries for the machine's macOS (15), the app targets 13 | a triplet `arm64-osx-13` (`VCPKG_OSX_DEPLOYMENT_TARGET 13.0`), and `CMAKE_OSX_DEPLOYMENT_TARGET 13.0`, `CMAKE_OSX_ARCHITECTURES arm64` |
| Audio input without `NSMicrophoneUsageDescription`: macOS ends the app | the app's Info.plist says why it uses the microphone (audio inputs for instruments and effects) |
| Each `platform::` piece with only `_win` and `_linux` files has no Mac code | the Mac files below |

## The Mac files

Each is small and follows its Linux counterpart; code outside them has no
`__APPLE__` / `Q_OS_MACOS` (the boundary check, extended to `__APPLE__`
and `TARGET_OS_`).

| Piece | Mac |
|---|---|
| Standard plugin folders | `~/Library/Audio/Plug-Ins/VST3`, `/Library/Audio/Plug-Ins/VST3` |
| A bundle's binary (fingerprints) | `<bundle>/Contents/MacOS/<name>` |
| File name case | case-insensitive (the Mac's disks by default) |
| RAM in the status bar | Mach `task_info` (`MACH_TASK_BASIC_INFO`, resident size), plain C++; a failure returns its `kern_return_t` |
| "System audio" name | "Core Audio" |
| Audio drivers | Core Audio only (`RtAudio::MACOSX_CORE`); MIDI through Core MIDI |
| Plugin run loop | none: Mac plugins run on the main thread's run loop, which Qt runs (`runLoop()` is null, as on Windows) |
| Plugin windows | Qt's window id is the `NSView*` the plugin wants (`kPlatformTypeNSView`, already mapped) |
| The screen ratio for plugin windows | a new `platform::pluginPixelRatio(const QWindow&)`: the window's device pixel ratio on Windows and Linux (today's behaviour), **1.0 on the Mac**; the content scale is not set on the Mac (Muse) |
| Crash reports, single instance, scanner flags, process | the POSIX files, unchanged |
| Single-instance socket path | a length check (the Mac's limit is 104 bytes): too long is an error saying the path and the limit, never a silent failure |

The three places that use the screen's ratio today
(`PluginEditorHost.cpp` twice, `EffectWindows.cpp`) call
`platform::pluginPixelRatio`, and `setContentScale` only where
`platform::pluginsTakeContentScale()` says so (true on Windows and Linux).

## The app bundle and the .dmg

- `gigchain_app` is a `MACOSX_BUNDLE` named `GigChain Keys.app`, with an
  Info.plist template in `branding/`:
  - bundle id `nz.dkstudios.gigchainkeys` (`PRODUCT_BUNDLE_ID` in
    `branding.cmake`; a reverse-domain name that needs no domain bought,
    and matches one if it ever is; macOS keys settings and permissions to
    it, so it does not change after the first release);
  - `LSMinimumSystemVersion` 13.0;
  - `NSHighResolutionCapable`;
  - `NSMicrophoneUsageDescription`;
  - the version from `branding.cmake`.
- The icon: `app.icns`, made on the build machine from
  `branding/app-icon.png` (`sips` + `iconutil`).
- Packaging (`tools/package-mac.sh`):
  1. `macdeployqt "GigChain Keys.app" -qmldir=src/ui/qml` (Qt, its plugins
     and QML inside the app).
  2. Ad-hoc sign the whole bundle: `codesign --force --deep --sign -`. No
     hardened runtime, so plugins signed by anyone load.
  3. `codesign --verify --deep --strict`: a failure fails the build.
  4. `hdiutil create -format UDZO` → `GigChain Keys-<version>-arm64.dmg`,
     with an Applications link.
- Logs, settings and caches go in the Mac's standard places through
  `QStandardPaths`, as on the other systems.

## The build on GitHub (`.github/workflows/mac.yml`)

Triggers: `workflow_dispatch` and tags `v*`. The runner is `macos-15`
(Apple Silicon). `timeout-minutes: 60`. The steps, cheapest failure first:

1. **Checkout.** Record `xcodebuild -version`.
2. **Qt 6.10.2** (`jurplel/install-qt-action@v4`, `host: mac`,
   `arch: clang_64`, `modules: qtmultimedia`, `cache: true`).
3. **Tools:** `brew install ninja`.
4. **vcpkg** at the manifest's baseline; binary cache restored with a key
   of `vcpkg.json`, `ports/**`, `triplets/**` and **the Xcode version**
   (RtAudio's exported target records Xcode's framework paths).
5. **Configure** (`--preset mac-release`): Apple-specific CMake mistakes
   fail here, in minutes.
6. **Build** (`-Werror`).
7. **Save the vcpkg cache, even when the build failed** (a second attempt
   skips the 30-plus minutes of library building).
8. **Tests:** `brew install --cask surge-xt`, then every test (`ctest`).
   Tests needing an audio device or MIDI keyboard skip themselves, as on
   Windows CI.
9. **Package**, then **upload the `.dmg`** as the run's artifact. Test logs
   are uploaded on failure.

A `mac-release` configure preset (Ninja, arm64, the triplet) goes in
`CMakePresets.json`.

## Keeping failed Mac runs down (every run costs minutes)

- **Before any Mac minute:** the whole project builds in WSL with a recent
  Clang (Apple's compiler is Clang; LLVM 18 from apt.llvm.org) and
  `-Werror`: the warnings GCC and MSVC never raised show up at home. A
  preset `linux-clang` keeps it.
- The Mac-only code is small, and each file is checked against Steinberg's
  sample, Muse, or the Linux file it mirrors.
- **On GitHub:**
  - steps are ordered so setup mistakes fail early;
  - the cache survives failures;
  - runs are capped at 60 minutes;
  - no run is repeated needlessly.

  Each run's result (minutes used, where it failed) goes in the progress
  ledger.

## Tests

- The existing suite on the Mac, with test plugins per system in
  `tests/common/TestPlugins.h`: Surge XT and Surge XT Effects in
  `/Library/Audio/Plug-Ins/VST3`.
- New or changed tests:
  - `tst_platform`: the Mac's folders, module file, case, window kind, pixel
    ratio 1.0, memory reading;
  - `tst_audio_device`: the Mac's drivers ("Core Audio" only);
  - `tst_vst3_editor`: a Mac `HiddenParent` (a `QWindow`'s `NSView`);
  - `tst_single_instance`: the socket-length check (a too-long path is
    refused with the path and the limit);
  - the platform boundary check: `__APPLE__` / `Q_OS_MACOS` outside
    suffixed files fails it.
- Windows: the full gate (`tools/verify.ps1`) on every commit, as now.
  Linux: debug, ASan and the new Clang build, all green.

## The friend's M5 checklist

1. Download the `.dmg`, open it and drag the app to Applications.
2. Start it, and go through Open Anyway once.
3. The plugin scan finds their VST3 plugins.
4. Load an instrument, open its window (right size, sharp, no scaling
   mess), and play it from a USB keyboard.
5. Use an audio input: macOS asks for the microphone once, and the input
   works after "Allow".
6. Unplug and plug back the keyboard and the audio interface.
7. Quit and start again: the setlist comes back.
8. If anything goes wrong, send the logs and crash folders
   (`~/Library/Application Support/GigChain/GigChain Keys/` and the crash
   folder the app names).

## Out of scope

- Intel Macs and universal builds.
- Notarization and a Developer ID certificate.
- Audio Units (VST3 only, as on the other systems).
- The public download page and release automation (piece 3).
- Mac sanitizer and fuzz runs (they run on Linux; the code is shared).
