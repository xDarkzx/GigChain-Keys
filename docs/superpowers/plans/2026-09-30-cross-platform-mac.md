# Cross-platform piece 2: macOS — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** GigChain Keys builds, passes its tests and runs on Apple Silicon
Macs (macOS 13+), built on GitHub's Mac machines on demand, shipped as an
ad-hoc-signed `.dmg`.

**Architecture:** The Mac joins the platform layer as `_mac.cpp` files next
to the `_win` / `_linux` ones (the POSIX files already cover crash reports,
single instance, scanner and process). The build gets Mac branches (compiler
flags, Steinberg's `module_mac.mm` with ARC, a deployment-target triplet, an
app bundle). A manually started GitHub workflow builds, tests and packages.
Every Mac minute is expensive, so everything that can be checked at home is
checked first: a Clang build in WSL, and every shared-code test on Windows
and Linux.

**Tech Stack:** C++20, Qt 6.10.2 (clang_64), CMake 3.24+ / Ninja, vcpkg
(triplet `arm64-osx-13`), Steinberg VST3 SDK 3.8 (`module_mac.mm`),
RtAudio (Core Audio), RtMidi (Core MIDI), GitHub Actions `macos-15`,
`macdeployqt`, `codesign`, `hdiutil`.

**Spec:** `docs/superpowers/specs/2026-09-30-cross-platform-mac-design.md`

## Global Constraints

- Apple Silicon only (`arm64`), **macOS 13.0** minimum
  (`CMAKE_OSX_DEPLOYMENT_TARGET 13.0`, `VCPKG_OSX_DEPLOYMENT_TARGET 13.0`).
- Bundle id **`nz.dkstudios.gigchainkeys`**; the app is `GigChain Keys.app`.
- Ad-hoc signed (`codesign --force --deep --sign -`), **no hardened
  runtime**, not notarized.
- The Mac workflow runs **only** on `workflow_dispatch` and tags `v*`, with
  `timeout-minutes: 60`, in the private repo. Pushing the branch is allowed
  for it; no merge to `main`, no public repo, and no run repeated needlessly.
- Windows and Linux behave exactly as today. The Windows gate
  (`tools/verify.ps1`) passes on every commit; Linux debug and ASan stay
  green.
- No system code outside `_win` / `_posix` / `_linux` / `_mac` files (the
  `platform_boundary` check).
- Every failure returns its precise cause and is logged; nothing is silent.
- Edit source files with the Edit tool (no sed or Python rewrites).

## Review Focus

1. **The app running under Qt's off-screen platform on the Mac.** It must
   not hand a plugin a fake NSView. The window kind is `Cocoa` only when Qt
   runs `cocoa`: `tst_platform::theNativeWindowKindIsThisSystems` (Task 3).
2. **A socket path longer than the system allows** (104 bytes on the Mac,
   108 on Linux). The single instance must refuse it with the path and the
   limit, not with Qt's vague "name error":
   `tst_single_instance::aTooLongSocketPathIsRefusedWithItsLength` (Task 3).
3. **Plugin windows on a Retina Mac.** Sizes are in points: no double
   scaling and no content scale:
   `tst_platform::pluginWindowsUseTheSystemsUnits` (Task 4).
4. **A plugin bundle that crashes while being read on the Mac** (the scanner
   survives it). The crash is in `bundleEntry`, which Mac hosts call first:
   `tst_plugin_scanner` with a Mac bundle (Task 5).
5. **A cache restored onto a machine with another Xcode.** RtAudio's target
   records Xcode's framework paths, so the Xcode version is in the vcpkg
   cache key (Task 7).

---

### Task 1: A Clang build at home (catch Apple-compiler errors before any Mac minute)

**Files:**
- Modify: `tools/setup-linux.sh` (install `clang-18` from apt.llvm.org)
- Modify: `CMakePresets.json` (configure/build/test presets `linux-clang`)
- Modify: whatever source files Clang 18 reports under `-Werror` (each fix
  the minimal one; logged in the ledger)

**Interfaces:**
- Produces: preset `linux-clang` (Debug, `clang-18`/`clang++-18`,
  `x64-linux` triplet, its own `binaryDir`).

- [ ] **Step 1: Install Clang 18 in WSL (setup script)**

In `tools/setup-linux.sh`, after the apt package list, add:

```bash
# Clang 18 (apt.llvm.org): the linux-clang preset checks the code with the
# same compiler family as Apple's before any Mac build runs.
if ! command -v clang++-18 >/dev/null 2>&1; then
    llvm_script="$(mktemp)"
    curl -fsSL https://apt.llvm.org/llvm.sh -o "$llvm_script"
    bash "$llvm_script" 18
    rm -f "$llvm_script"
fi
```

Run: `wsl bash tools/setup-linux.sh` (or only the block above).
Expected: `clang++-18 --version` prints version 18.

- [ ] **Step 2: Add the preset**

In `CMakePresets.json`, add a configure preset after `linux-release`:

```json
    {
      "name": "linux-clang",
      "inherits": "linux-base",
      "description": "Clang 18 with -Werror: Apple's compiler family, checked before any Mac build",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_C_COMPILER": "clang-18",
        "CMAKE_CXX_COMPILER": "clang++-18"
      }
    },
```

Add a build preset `{ "name": "linux-clang", "configurePreset": "linux-clang" }`
and a test preset
`{ "name": "linux-clang", "inherits": "base", "configurePreset": "linux-clang" }`.

- [ ] **Step 3: Build; read every error**

Run (WSL, scratchpad `lx.sh`): `cmake --preset linux-clang && cmake --build --preset linux-clang 2>&1 | grep -E "error:" | sort | uniq`
Expected: a list of Clang-only complaints, possibly empty. For each one, make
the minimal source fix with the Edit tool and write one ledger line. A
warning in third-party code (`_deps`, vcpkg) is not ours; that code is
included as SYSTEM, so none should appear. If one does, record a ruling.

- [ ] **Step 4: Build clean and run the suite**

Run: `cmake --build --preset linux-clang && ctest --preset linux-clang`
Expected: builds with no errors; every test passes (the same count as
`linux-debug`).

- [ ] **Step 5: Windows gate and commit**

```bash
git add tools/setup-linux.sh CMakePresets.json <fixed files>
git commit -m "build: a Clang 18 build on Linux (Apple's compiler family) before any Mac build"
```
Expected: the commit hook's `tools/verify.ps1` passes.

---

### Task 2: The build knows the Mac (flags, platform files, Steinberg's Mac loader, triplet, presets)

**Files:**
- Modify: `cmake/CompilerHardening.cmake` (an Apple branch)
- Modify: `cmake/PlatformSources.cmake` (an Apple branch)
- Modify: `cmake/Vst3Sdk.cmake` (`module_mac.mm` with `-fobjc-arc`, Cocoa)
- Modify: `cmake/CheckPlatformBoundary.cmake` (add `Q_OS_MAC`, `TARGET_OS_`)
- Create: `triplets/arm64-osx-13.cmake`
- Modify: `CMakePresets.json` (`mac-release` configure/build/test)
- Modify: `CMakeLists.txt` (deployment target and architecture on Apple)

**Interfaces:**
- Produces: `gigchain_platform_sources(<target> <base>...)` picks
  `<base>_posix.cpp` plus `<base>_mac.cpp` or `<base>_mac.mm` on Apple.
  Preset `mac-release` (Ninja, Release, triplet `arm64-osx-13`, overlay
  triplets `${sourceDir}/triplets`, `binaryDir` `${sourceDir}/build/mac-release`).

- [ ] **Step 1: The boundary check catches Mac code (failing first)**

Create `build/boundary-probe/src/probe.cpp` (a scratch folder, not
committed) containing `#ifdef Q_OS_MACOS\n#endif`, then run:
`cmake -DSOURCE_DIR=build/boundary-probe/src -P cmake/CheckPlatformBoundary.cmake`
Expected before the change: `platform boundary: 1 files checked, no system code…` (it misses it).

- [ ] **Step 2: Extend the check**

In `cmake/CheckPlatformBoundary.cmake`, change the regex alternation
`_WIN32|__linux__|__APPLE__|_MSC_VER` to
`_WIN32|__linux__|__APPLE__|Q_OS_MAC|TARGET_OS_|_MSC_VER`.
(`Q_OS_MAC` also matches `Q_OS_MACOS`.)

Run the probe again. Expected: FATAL_ERROR naming `probe.cpp`. Then run
`ctest --preset debug -R platform_boundary` (Windows).
Expected: passes. No `src` file outside suffixed files uses these today.
Delete `build/boundary-probe`.

- [ ] **Step 3: Compiler hardening, Apple branch**

In `cmake/CompilerHardening.cmake`, replace the `else()` branch body with:

```cmake
    else()
        # GCC and Clang: the same strictness everywhere.
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            # Designated initializers leaving members at their defaults are
            # this code's style.
            -Wno-missing-field-initializers
            -fstack-protector-strong)
        if(APPLE)
            # The Mac: Apple's SDK fortifies library calls itself (defining
            # _FORTIFY_SOURCE again is a redefinition error), its linker makes
            # stacks non-executable and code position-independent by default
            # and has no -z options, and stack-clash protection is not offered
            # for Apple targets.
        else()
            # Linux: fortified library calls, stack-clash protection,
            # read-only relocations, no executable stack.
            target_compile_options(${target} PRIVATE
                -fstack-clash-protection
                $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
            target_link_options(${target} PRIVATE -Wl,-z,relro,-z,now -Wl,-z,noexecstack)
        endif()
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
```

Run: `cmake --build --preset linux-debug` (WSL) and the Windows build.
Expected: both build. Check that Linux keeps its flags:
`grep -c "fstack-clash-protection" ~/.cache/gigchain/build/linux-debug/compile_commands.json`
must be > 0.

- [ ] **Step 4: Platform sources, Apple branch**

In `cmake/PlatformSources.cmake`, update the comment's last line to
`(<base>_mac.cpp or <base>_mac.mm on macOS).` After the Linux `if`, add:

```cmake
        if(APPLE)
            if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${base}_mac.cpp)
                target_sources(${target} PRIVATE ${base}_mac.cpp)
            elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${base}_mac.mm)
                target_sources(${target} PRIVATE ${base}_mac.mm)
            endif()
        endif()
```

- [ ] **Step 5: Steinberg's Mac module loader**

In `cmake/Vst3Sdk.cmake`, extend the loader `if`:

```cmake
elseif(APPLE)
    # As Steinberg's editorhost sample: the Mac loader is Objective-C++ and
    # refuses to build without ARC ("#error this file needs to be compiled
    # with automatic reference counting enabled").
    target_sources(vst3_host_support PRIVATE ${VST3_HOSTING_DIR}/module_mac.mm)
    set_source_files_properties(${VST3_HOSTING_DIR}/module_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    # Cocoa (AppKit, Foundation) for module_mac.mm and sdk_common's
    # threadchecker_mac.mm / systemclipboard_mac.mm; CoreFoundation for CFBundle.
    target_link_libraries(vst3_host_support PUBLIC "-framework Cocoa" "-framework CoreFoundation")
```

(`.mm` files compile with the C++ compiler, which Clang treats as
Objective-C++ by extension; this is how the SDK's own `sdk_common` builds
its `.mm` files with Ninja. No `enable_language(OBJCXX)`.)

- [ ] **Step 6: Deployment target and architecture**

In `CMakeLists.txt`, before `project(...)`, add:

```cmake
# The Mac: Apple Silicon, macOS 13 and newer (Qt 6.10's floor; Qt says not
# to go lower). Before project() so every target, and vcpkg's, agrees.
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Oldest macOS the app runs on")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Apple Silicon only")
endif()
```

- [ ] **Step 7: The vcpkg triplet**

Create `triplets/arm64-osx-13.cmake`:

```cmake
# Apple Silicon libraries for macOS 13 and newer (the app's floor): without
# it vcpkg builds for the build machine's macOS, and the app could call what
# macOS 13 lacks.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.0)
```

- [ ] **Step 8: The Mac presets**

In `CMakePresets.json`, add configure preset:

```json
    {
      "name": "mac-release",
      "inherits": "base",
      "description": "macOS, Apple Silicon (GitHub's Mac machines: .github/workflows/mac.yml)",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "VCPKG_TARGET_TRIPLET": "arm64-osx-13",
        "VCPKG_OVERLAY_TRIPLETS": "${sourceDir}/triplets"
      }
    },
```

a build preset `{ "name": "mac-release", "configurePreset": "mac-release" }`
and a test preset
`{ "name": "mac-release", "inherits": "base", "configurePreset": "mac-release" }`.

- [ ] **Step 9: Windows and Linux unchanged; commit**

Run: Windows `tools/verify.ps1` (via commit hook), `ctest --preset linux-debug`, `ctest --preset linux-clang`.
Expected: all pass.

```bash
git add cmake/ triplets/arm64-osx-13.cmake CMakePresets.json CMakeLists.txt
git commit -m "build: the Mac in the build (flags, platform files, Steinberg's Mac loader, macOS 13 triplet, preset)"
```

---

### Task 3: The Mac platform files, the window kind, and the socket path check

**Files:**
- Create: `src/platform/PluginFolders_mac.cpp`, `src/platform/MemoryUse_mac.cpp`, `src/platform/Audio_mac.cpp`
- Create: `src/engine/internal/AudioApis_mac.cpp`, `src/engine/internal/Vst3RunLoop_mac.cpp`
- Modify: `src/platform/Windows_posix.cpp` (Cocoa only under Qt's `cocoa`)
- Modify: `src/platform/InstanceLock_posix.cpp` (Mac runtime folder; `checkLocalSocketName`)
- Modify: `src/platform/InstanceLock_win.cpp` (`checkLocalSocketName`: always fine)
- Modify: `src/platform/include/gigchain/platform/InstanceLock.h`
- Modify: `src/ui/cpp/SingleInstance.cpp` (check before listening)
- Modify: `src/platform/include/gigchain/platform/Audio.h`, `src/engine/include/gigchain/engine/EngineTypes.h` (comments name Core Audio)
- Test: `tests/platform/tst_platform.cpp`, `tests/ui/tst_single_instance.cpp`, `tests/engine/tst_audio_device.cpp`, `tests/ui/tst_settings.cpp`

**Interfaces:**
- Consumes: `gigchain_platform_sources` Apple branch (Task 2).
- Produces: `core::Result<void> platform::checkLocalSocketName(const QString& path)`:
  ok, or `ErrorCode::InvalidData` with
  `"The app's socket path is too long for this system (<n> bytes, at most <max>): <path>"`.

- [ ] **Step 1: Failing test for the socket length (runs on Linux)**

In `tests/ui/tst_single_instance.cpp`, add a slot (outside `#ifdef`s):

```cpp
    // A socket path longer than the system allows (104 bytes on the Mac,
    // 108 on Linux) is refused saying so, not with Qt's vague "name error".
    void aTooLongSocketPathIsRefusedWithItsLength()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows pipe names have no such limit");
#else
        SingleInstance running(uniqueName() + QString(200, u'x'));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"too long for this system"_s));
        const auto listened = running.listen();
        QVERIFY(!listened);
        QVERIFY2(listened.error().message.contains(u"too long for this system"_s), qPrintable(listened.error().message));
        QVERIFY2(listened.error().message.contains(u"at most"_s), qPrintable(listened.error().message));
#endif
    }
```

Run (WSL): `ctest --preset linux-debug -R tst_single_instance --output-on-failure`
Expected: FAIL. The message has Qt's error, not "too long for this system".
(If `listen()` does not log the warning, drop the `ignoreMessage` and log in
Step 2. Check how `listen()` reports today, `SingleInstance.cpp:63-75`.)

- [ ] **Step 2: `checkLocalSocketName`**

In `InstanceLock.h`, after `instanceSocketName`:

```cpp
// Whether the system can listen on `path` as a local socket (Unix sockets
// have a short path limit: 104 bytes on the Mac, 108 on Linux); an error
// saying the length and the limit when it cannot. Always fine on Windows.
[[nodiscard]] core::Result<void> checkLocalSocketName(const QString& path);
```

Add `#include "gigchain/core/Error.h"` there, or whichever header declares
`core::Result`, as the other platform headers do.

In `InstanceLock_win.cpp`:

```cpp
core::Result<void> checkLocalSocketName(const QString& /*path*/)
{
    return {}; // pipe names have no such limit
}
```

In `InstanceLock_posix.cpp`, add `#include <sys/un.h>` and:

```cpp
core::Result<void> checkLocalSocketName(const QString& path)
{
    const qsizetype bytes = path.toUtf8().size();
    const auto most = static_cast<qsizetype>(sizeof(sockaddr_un{}.sun_path)) - 1; // (its ending zero)
    if (bytes > most) {
        return core::fail(core::ErrorCode::InvalidData,
                          u"The app's socket path is too long for this system (%1 bytes, at most %2): %3"_s.arg(bytes).arg(most).arg(path));
    }
    return {};
}
```

In `SingleInstance::listen()`, before `m_server.listen(m_pipe)`:

```cpp
    if (auto usable = platform::checkLocalSocketName(m_pipe); !usable) {
        qCWarning(lcUi).noquote() << usable.error().message;
        return usable;
    }
```

(Match the function's existing error style: if it builds its errors with
`core::fail` and logs them at the caller, do the same.)

Run: `ctest --preset linux-debug -R tst_single_instance`. Expected: PASS.

- [ ] **Step 3: The Mac's runtime folder**

In `InstanceLock_posix.cpp`, replace `runtimeFolder()` with:

```cpp
// The user's own runtime folder, only this user can enter it. Linux:
// XDG_RUNTIME_DIR (Qt makes a private one where there is none). The Mac:
// its per-user temporary folder ($TMPDIR, 0700, short): Qt's runtime
// location there is ~/Library/Application Support, long and shared with
// settings, and socket paths are limited to 104 bytes.
QString runtimeFolder()
{
#ifdef Q_OS_MACOS
    return QDir::tempPath();
#else
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
}
```

In `tst_single_instance.cpp` (the `#else` branch at lines 120-125), make the
expected folder per system:

```cpp
#else
#ifdef Q_OS_MACOS
        const QString runtime = QDir::tempPath();
#else
        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
```

(the rest unchanged).

- [ ] **Step 4: The window kind follows Qt's real platform on the Mac**

In `Windows_posix.cpp`:

```cpp
NativeWindowKind nativeWindowKind()
{
    // What Qt runs on, not what was hoped for: an off-screen (or Wayland)
    // window is no NSView or X11 window, and a plugin must never get one.
#ifdef Q_OS_MACOS
    return QGuiApplication::platformName() == u"cocoa" ? NativeWindowKind::Cocoa : NativeWindowKind::None;
#else
    return QGuiApplication::platformName() == u"xcb" ? NativeWindowKind::X11 : NativeWindowKind::None;
#endif
}
```

`tst_platform::theNativeWindowKindIsThisSystems` already expects
`offscreen` → `None` in its `#else` branch; this now holds on the Mac too.

- [ ] **Step 5: Mac expectations in the tests (written before the Mac files)**

`tests/platform/tst_platform.cpp`, `theStandardPluginFoldersAreAbsolute`:
replace `#else` with

```cpp
#elif defined(Q_OS_MACOS)
        QCOMPARE(folders, (QStringList{QDir::homePath() + u"/Library/Audio/Plug-Ins/VST3"_s, u"/Library/Audio/Plug-Ins/VST3"_s}));
#else
```

`aBundlesModuleIsThisSystems`:

```cpp
#elif defined(Q_OS_MACOS)
        QCOMPARE(platform::vst3ModuleFile(u"/Library/Audio/Plug-Ins/VST3/Surge XT.vst3"_s),
                 u"/Library/Audio/Plug-Ins/VST3/Surge XT.vst3/Contents/MacOS/Surge XT"_s);
        QCOMPARE(platform::fileNameCase(), Qt::CaseInsensitive);
#else
```

`tests/engine/tst_audio_device.cpp`, `thisSystemsDriversAreListed`:

```cpp
#elif defined(Q_OS_MACOS)
        QCOMPARE(drivers, std::vector<AudioDriver>{AudioDriver::System});
        QCOMPARE(apiName(AudioDriver::System), u"Core Audio"_s);
#else
```

`tests/ui/tst_settings.cpp`, `theDriversAreThisSystems`:

```cpp
#elif defined(Q_OS_MACOS)
        QCOMPARE(drivers.at(0).toMap().value(u"name"_s).toString(), u"Core Audio"_s);
#else
```

and update its comment to `(Windows: WASAPI; Linux: PulseAudio; the Mac: Core Audio)`.

- [ ] **Step 6: The Mac files**

`src/platform/PluginFolders_mac.cpp`:

```cpp
#include "gigchain/platform/PluginFolders.h"

#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

QStringList standardVst3Folders()
{
    return {QDir::homePath() + u"/Library/Audio/Plug-Ins/VST3"_s, u"/Library/Audio/Plug-Ins/VST3"_s};
}

QString vst3ModuleFile(const QString& bundle)
{
    return bundle + u"/Contents/MacOS/"_s + QFileInfo(bundle).completeBaseName();
}

Qt::CaseSensitivity fileNameCase()
{
    return Qt::CaseInsensitive; // the Mac's disks, by default
}

} // namespace gigchain::platform
```

`src/platform/MemoryUse_mac.cpp`:

```cpp
#include "gigchain/platform/MemoryUse.h"

#include <mach/mach.h>
#include <mach/mach_error.h>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

core::Result<qint64> residentBytes()
{
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t result =
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Mach's own buffer type
    if (result != KERN_SUCCESS) {
        return core::fail(core::ErrorCode::SystemRefused,
                          u"task_info failed: %1 (%2)"_s.arg(QString::fromUtf8(mach_error_string(result))).arg(result));
    }
    return static_cast<qint64>(info.resident_size);
}

} // namespace gigchain::platform
```

`src/platform/Audio_mac.cpp`:

```cpp
#include "gigchain/platform/Audio.h"

#include <QCoreApplication>

namespace gigchain::platform {

QString systemAudioName()
{
    return QCoreApplication::translate("Settings", "Core Audio");
}

} // namespace gigchain::platform
```

`src/engine/internal/AudioApis_mac.cpp`:

```cpp
#include "AudioApis.h"

using namespace Qt::StringLiterals;

namespace gigchain::engine {

// The Mac: Core Audio, low-latency already (Reaper offers only it).
std::vector<AudioDriver> systemAudioDrivers()
{
    return {AudioDriver::System};
}

RtAudio::Api toRtApi(AudioApi /*api*/)
{
    return RtAudio::MACOSX_CORE; // (the only driver here)
}

QString apiName(AudioApi api)
{
    switch (api) {
    case AudioApi::System: return u"Core Audio"_s;
    case AudioApi::Asio: return u"ASIO"_s;
    case AudioApi::Jack: return u"JACK"_s;
    case AudioApi::Alsa: return u"ALSA"_s;
    }
    return u"Core Audio"_s;
}

} // namespace gigchain::engine
```

`src/engine/internal/Vst3RunLoop_mac.cpp`:

```cpp
#include "Vst3RunLoop.h"

namespace gigchain::engine {

std::unique_ptr<Vst3RunLoop> makeVst3RunLoop()
{
    return nullptr; // Mac plugins run on the main thread's run loop, which Qt runs
}

} // namespace gigchain::engine
```

Update comments: `Audio.h` → `"Windows Audio (WASAPI)", "PulseAudio", "Core Audio".`;
`EngineTypes.h:52` → `the system's own: Windows audio (WASAPI), PulseAudio on Linux, Core Audio on the Mac; the default`.

- [ ] **Step 7: Linux and Windows green; Clang green; commit**

Run: `ctest --preset linux-debug`, `ctest --preset linux-clang`, Windows gate.
Expected: all pass. (The Mac files first compile in Task 7.)

```bash
git add src/platform src/engine src/ui/cpp/SingleInstance.cpp tests/
git commit -m "feat: the Mac's platform files; socket paths too long are refused with their length"
```

---

### Task 4: Plugin windows in the system's units (points on the Mac)

**Files:**
- Modify: `src/platform/include/gigchain/platform/Windows.h`, `src/platform/Windows_win.cpp`, `src/platform/Windows_posix.cpp`
- Modify: `src/ui/cpp/PluginEditorHost.cpp` (lines ~112, ~134, ~164), `src/ui/cpp/EffectWindows.cpp` (~167, and any `setContentScale` there)
- Test: `tests/platform/tst_platform.cpp`

**Interfaces:**
- Produces: `double platform::pluginPixelRatio(const QWindow& window)`,
  `bool platform::pluginsTakeContentScale()`.

- [ ] **Step 1: Failing test**

In `tst_platform.cpp`, replace `QTEST_MAIN(TestPlatform)` with a main that
scales the off-screen screen by 2 (so the ratio means something):

```cpp
int main(int argc, char** argv)
{
    qputenv("QT_SCALE_FACTOR", "2"); // a "Retina" screen, off-screen
    QGuiApplication app(argc, argv);
    TestPlatform test;
    return QTest::qExec(&test, argc, argv);
}
```

and add:

```cpp
    // Plugin windows in the system's units: pixels where plugins size in
    // pixels (Windows, Linux: the screen's ratio, and they are told the
    // scale), points on the Mac (1.0, and the system scales them).
    void pluginWindowsUseTheSystemsUnits()
    {
        QWindow window;
        window.resize(100, 100);
        window.create();
        QCOMPARE(window.devicePixelRatio(), 2.0);
#ifdef Q_OS_MACOS
        QCOMPARE(platform::pluginPixelRatio(window), 1.0);
        QVERIFY(!platform::pluginsTakeContentScale());
#else
        QCOMPARE(platform::pluginPixelRatio(window), 2.0);
        QVERIFY(platform::pluginsTakeContentScale());
#endif
    }
```

Run: `ctest --preset debug -R tst_platform` (Windows).
Expected: FAIL to compile (`pluginPixelRatio` is not a member of `platform`).

- [ ] **Step 2: Implement**

`Windows.h`, add:

```cpp
// The ratio between a plugin view's size and the window's own units: the
// screen's device pixel ratio where plugins size themselves in pixels
// (Windows, Linux), 1.0 on the Mac, whose views are sized in points (as
// Audacity 4's VstView).
[[nodiscard]] double pluginPixelRatio(const QWindow& window);

// Whether plugins are told the screen's scale (IPlugViewContentScaleSupport):
// not on the Mac, where the system scales views itself.
[[nodiscard]] bool pluginsTakeContentScale();
```

`Windows_win.cpp`:

```cpp
double pluginPixelRatio(const QWindow& window)
{
    return window.devicePixelRatio();
}

bool pluginsTakeContentScale()
{
    return true;
}
```

`Windows_posix.cpp`:

```cpp
double pluginPixelRatio(const QWindow& window)
{
#ifdef Q_OS_MACOS
    Q_UNUSED(window);
    return 1.0;
#else
    return window.devicePixelRatio();
#endif
}

bool pluginsTakeContentScale()
{
#ifdef Q_OS_MACOS
    return false;
#else
    return true;
#endif
}
```

- [ ] **Step 3: Use it**

`PluginEditorHost.cpp`:
- ~112: `if (platform::pluginsTakeContentScale()) editor->setContentScale(host->devicePixelRatio());`
- ~134 (inside the `screenChanged` lambda): `if (platform::pluginsTakeContentScale()) m_editor->setContentScale(screen->devicePixelRatio());`
- ~164: `const double dpr = platform::pluginPixelRatio(*window());`

`EffectWindows.cpp` ~167: `entry->ratio = platform::pluginPixelRatio(*window);`.
Grep the file for other `devicePixelRatio` / `setContentScale` uses, and
guard or convert each the same way.

Run: `ctest --preset debug -R "tst_platform|tst_plugin_view|tst_qml_smoke"` (Windows),
`ctest --preset linux-debug -R tst_platform`.
Expected: PASS (Windows and Linux: ratio 2.0, as before the change).

- [ ] **Step 4: Commit**

```bash
git add src/platform src/ui/cpp tests/platform/tst_platform.cpp
git commit -m "feat: plugin windows in the system's units (points on the Mac, no content scale there)"
```

---

### Task 5: The tests run on the Mac (plugins, handles, the crashing bundle, case, editor)

**Files:**
- Modify: `tests/common/TestPlugins.h`, `tests/common/Handles.h`
- Modify: `tests/engine/crashing_plugin.cpp`, `tests/engine/tst_plugin_scanner.cpp`
- Modify: `tests/engine/tst_plugin_load_guard.cpp`, `tests/engine/tst_vst3_editor.cpp`
- Modify: `tests/engine/CMakeLists.txt` (`tst_vst3_run_loop` on Linux only)

**Interfaces:**
- Consumes: `platform::fileNameCase()`, `platform::nativeWindowKind()`.

- [ ] **Step 1: Test plugins per system**

`TestPlugins.h`: header comment gains
`//  - macOS: Surge XT and Surge XT Effects (brew install --cask surge-xt).`
Replace `#else` with:

```cpp
#elif defined(Q_OS_MACOS)
inline const QString kVst3Folder = QStringLiteral("/Library/Audio/Plug-Ins/VST3");
inline const TestPlugin kInstrument{QStringLiteral("/Library/Audio/Plug-Ins/VST3/Surge XT.vst3"), QStringLiteral("Surge XT"),
                                    QStringLiteral("surge")};
inline const TestPlugin kEffect{QStringLiteral("/Library/Audio/Plug-Ins/VST3/Surge XT Effects.vst3"),
                                QStringLiteral("Surge XT Effects"), QStringLiteral("surge")};
#else
```

- [ ] **Step 2: Handles on the Mac**

`Handles.h`: comment gains `//  - macOS: open file descriptors from /dev/fd, and the threads (Mach).`
Restructure the conditionals to `#if defined(_WIN32)` (the Windows function,
unchanged) / `#elif defined(__APPLE__)` / `#else` (the Linux function,
unchanged). Includes: `<windows.h>` under `_WIN32`; `<QDir>` plus
`<mach/mach.h>` under `__APPLE__`; `<QDir>`, `<QFileInfo>` otherwise. The Mac
function:

```cpp
inline std::map<QString, int> handlesByType()
{
    std::map<QString, int> counts;
    const QDir fds(QStringLiteral("/dev/fd"));
    counts[QStringLiteral("file")] =
        static_cast<int>(fds.entryList(QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot).size()) - 1; // (the listing's own)
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS) {
        counts[QStringLiteral("Thread")] = static_cast<int>(count);
        for (mach_msg_type_number_t i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), threads[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): Mach's array
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Mach's array
    }
    return counts;
}
```

- [ ] **Step 3: The crashing plugin as a Mac bundle**

`crashing_plugin.cpp`: before the `#ifndef _WIN32` Linux block, add, and
change that block's guard to `#elif !defined(_WIN32)`:

```cpp
#if defined(__APPLE__)
// Mac hosts call the bundle's entry first (Steinberg's module_mac, once it
// has found bundleEntry, bundleExit and GetPluginFactory): the crash is there.
extern "C" GIGCHAIN_EXPORT bool bundleEntry(void* /*bundle*/)
{
    volatile std::uintptr_t nowhere = 0;
    // cppcheck-suppress nullPointer ; the crash is the point
    *reinterpret_cast<volatile int*>(nowhere) = 1; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): the crash is the point
    return false;
}
extern "C" GIGCHAIN_EXPORT bool bundleExit()
{
    return true;
}
#elif !defined(_WIN32)
```

`tst_plugin_scanner.cpp` `init()`: replace the `#else` block with:

```cpp
#elif defined(Q_OS_MACOS)
        // Mac plugins are bundles: Crasher.vst3/Contents/MacOS/Crasher, named by its Info.plist.
        const QString contents = m_folder + u"/Crasher.vst3/Contents"_s;
        QVERIFY(QDir().mkpath(contents + u"/MacOS"_s));
        QVERIFY(QFile::copy(m_crasher, contents + u"/MacOS/Crasher"_s));
        QFile plist(contents + u"/Info.plist"_s);
        QVERIFY(plist.open(QIODevice::WriteOnly));
        plist.write(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>Crasher</string>
<key>CFBundleIdentifier</key><string>nz.dkstudios.gigchainkeys.test.crasher</string>
<key>CFBundlePackageType</key><string>BNDL</string>
</dict></plist>
)");
        plist.close();
#else
```

(keeping the Linux block after it). Check the file's two other
`Q_OS_WIN` spots for Linux-only assumptions, and give the Mac its own branch
where one exists.

- [ ] **Step 4: Case, the editor test, the run loop test**

`tst_plugin_load_guard.cpp` `#else` branch:

```cpp
#else
        QVERIFY(nextStart.isBlocked(kPiano));
        // Where names differ by case they are other plugins (Linux); where
        // they do not, the same one (the Mac).
        QCOMPARE(nextStart.isBlocked(kPiano.toLower()), platform::fileNameCase() == Qt::CaseInsensitive);
#endif
```

(Add `#include "gigchain/platform/PluginFolders.h"`.)

`tst_vst3_editor.cpp`:
- Include `"TestPlugins.h"`.
- In the non-Windows block, use `const QString kInstrument = test::kInstrument.path;`
  and `const QString kTitle = test::kInstrument.name;`.
- The header comment adds the Mac (a Cocoa window's NSView).
- `initTestCase`'s non-Windows skip becomes:
  `if (platform::nativeWindowKind() == platform::NativeWindowKind::None) QSKIP("Needs the system's window system (X11 on Linux: DISPLAY)");`
- `main()`:

```cpp
#if defined(Q_OS_MACOS)
    qputenv("QT_QPA_PLATFORM", "cocoa"); // real NSViews (the tests' default is off-screen)
#elif !defined(Q_OS_WIN)
    if (!qEnvironmentVariableIsEmpty("DISPLAY")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
```

`tests/engine/CMakeLists.txt`: `if(NOT WIN32)` around `tst_vst3_run_loop`
becomes `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")`, with the comment
`# Linux's run loop for plugin editors (Windows and Mac plugins run their own)`.

- [ ] **Step 5: Windows and Linux still green; commit**

Run: Windows gate; `ctest --preset linux-debug`; `ctest --preset linux-clang`.
Expected: all pass (the Mac branches first compile in Task 7).

```bash
git add tests/
git commit -m "test: the tests on the Mac (Surge XT, handles, the crashing bundle, case, editor)"
```

---

### Task 6: The app bundle and the .dmg

**Files:**
- Modify: `branding.cmake` (`PRODUCT_BUNDLE_ID`, `PRODUCT_BRAND_DIR`)
- Create: `branding/Info.plist.in`
- Modify: `src/app/CMakeLists.txt` (MACOSX_BUNDLE, output name, icon), `src/scanner/CMakeLists.txt` (into the bundle)
- Create: `tools/package-mac.sh`
- Modify: `CMakeLists.txt` install rules (the bundle's install on Apple)

**Interfaces:**
- Produces: the build folder holds `GigChain Keys.app` with the executable
  `Contents/MacOS/GigChain Keys` and the scanner
  `Contents/MacOS/GigChainKeysScan`. `tools/package-mac.sh <build dir> <version>`
  writes `<build dir>/GigChain Keys-<version>-arm64.dmg`.

- [ ] **Step 1: Branding**

In `branding.cmake`, after `PRODUCT_EXECUTABLE`:

```cmake
set(PRODUCT_BUNDLE_ID "nz.dkstudios.gigchainkeys") # the Mac's app id: never changes after the first release
```

and, next to `PRODUCT_SPLASH_IMAGE`:

```cmake
set(PRODUCT_BRAND_DIR "${CMAKE_CURRENT_LIST_DIR}/branding") # images and the Mac's Info.plist
```

- [ ] **Step 2: Info.plist template**

`branding/Info.plist.in`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>
    <key>CFBundleIconFile</key><string>app.icns</string>
    <key>CFBundleIdentifier</key><string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>${MACOSX_BUNDLE_BUNDLE_NAME}</string>
    <key>CFBundleDisplayName</key><string>${MACOSX_BUNDLE_BUNDLE_NAME}</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${MACOSX_BUNDLE_SHORT_VERSION_STRING}</string>
    <key>CFBundleVersion</key><string>${MACOSX_BUNDLE_BUNDLE_VERSION}</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
    <key>NSMicrophoneUsageDescription</key><string>GigChain Keys uses your audio inputs for the instruments and effects you play through them.</string>
    <key>NSHumanReadableCopyright</key><string>${MACOSX_BUNDLE_COPYRIGHT}</string>
</dict>
</plist>
```

- [ ] **Step 3: The app is a bundle on the Mac**

In `src/app/CMakeLists.txt`, after the `set_target_properties` line:

```cmake
# The Mac: "GigChain Keys.app" with its Info.plist (bundle id, macOS 13,
# the microphone's reason) and icon, made from branding/app-icon.png.
if(APPLE)
    set(_icns ${CMAKE_CURRENT_BINARY_DIR}/app.icns)
    add_custom_command(OUTPUT ${_icns}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/app.iconset
        COMMAND sips -z 16 16 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_16x16.png
        COMMAND sips -z 32 32 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_16x16@2x.png
        COMMAND sips -z 32 32 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_32x32.png
        COMMAND sips -z 64 64 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_32x32@2x.png
        COMMAND sips -z 128 128 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_128x128.png
        COMMAND sips -z 256 256 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_128x128@2x.png
        COMMAND sips -z 256 256 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_256x256.png
        COMMAND sips -z 512 512 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_256x256@2x.png
        COMMAND sips -z 512 512 ${PRODUCT_BRAND_DIR}/app-icon.png --out ${CMAKE_CURRENT_BINARY_DIR}/app.iconset/icon_512x512.png
        COMMAND iconutil -c icns ${CMAKE_CURRENT_BINARY_DIR}/app.iconset -o ${_icns}
        DEPENDS ${PRODUCT_BRAND_DIR}/app-icon.png
        COMMENT "The app's Mac icon")
    target_sources(gigchain_app PRIVATE ${_icns})
    set_source_files_properties(${_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(gigchain_app PROPERTIES
        OUTPUT_NAME "${PRODUCT_NAME}"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${PRODUCT_BRAND_DIR}/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER ${PRODUCT_BUNDLE_ID}
        MACOSX_BUNDLE_BUNDLE_NAME "${PRODUCT_NAME}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PRODUCT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PRODUCT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "${PRODUCT_COPYRIGHT}")
endif()
```

Check `main.cpp:140`: the scanner is found at
`applicationDirPath() + "/" + programFileName(branding::executable() + "Scan")`,
which is `.../GigChain Keys.app/Contents/MacOS/GigChainKeysScan` on the Mac.

- [ ] **Step 4: The scanner inside the bundle**

In `src/scanner/CMakeLists.txt`, after `set_target_properties`:

```cmake
# The Mac: inside the app's bundle, where the app looks for it (next to its
# own executable), in the build folder as when installed.
if(APPLE)
    set_target_properties(gigchain_scanner PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${PRODUCT_NAME}.app/Contents/MacOS")
endif()
```

- [ ] **Step 5: Install rules**

In the top `CMakeLists.txt` (or `src/app/CMakeLists.txt`, where the install
block is), change
`install(TARGETS gigchain_app gigchain_scanner RUNTIME DESTINATION .)` to:

```cmake
if(APPLE) # the scanner is inside the bundle already
    install(TARGETS gigchain_app BUNDLE DESTINATION .)
else()
    install(TARGETS gigchain_app gigchain_scanner RUNTIME DESTINATION .)
endif()
```

Licences and notices stay as they are (on the Mac they land next to the
bundle, and the package script copies them into the `.dmg`).

- [ ] **Step 6: The package script**

`tools/package-mac.sh`:

```bash
#!/usr/bin/env bash
# The Mac's .dmg: Qt inside the app (macdeployqt), the whole bundle ad-hoc
# signed and verified, then a compressed disk image with an Applications
# link and the licences.
#   bash tools/package-mac.sh <build dir> <version>
set -euo pipefail
BUILD="$1"
VERSION="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$BUILD/GigChain Keys.app"
DMG="$BUILD/GigChain Keys-$VERSION-arm64.dmg"
[ -d "$APP" ] || { echo "No app bundle at $APP"; exit 1; }
[ -x "$APP/Contents/MacOS/GigChainKeysScan" ] || { echo "The plugin scanner is missing from $APP"; exit 1; }

"$QT_ROOT_DIR/bin/macdeployqt" "$APP" -qmldir="$ROOT/src/ui" -verbose=1
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
[ -f "$BUILD/installer/THIRD-PARTY-NOTICES.txt" ] && cp "$BUILD/installer/THIRD-PARTY-NOTICES.txt" "$STAGE/"
rm -f "$DMG"
hdiutil create -volname "GigChain Keys" -srcfolder "$STAGE" -format UDZO -ov "$DMG"
rm -rf "$STAGE"
echo "Made $DMG"
```

(The QML files live in `src/ui`, the `qt_add_qml_module` of `src/ui/CMakeLists.txt`.)

- [ ] **Step 7: Windows and Linux unchanged; commit**

Run: Windows gate; `cmake --build --preset linux-debug && ctest --preset linux-debug`.
Expected: all pass. Linux still installs the app and scanner side by side.

```bash
git add branding.cmake branding/Info.plist.in src/app/CMakeLists.txt src/scanner/CMakeLists.txt CMakeLists.txt tools/package-mac.sh
git commit -m "build: the Mac app bundle (Info.plist, icon, scanner inside) and its .dmg"
```

---

### Task 7: The Mac build on GitHub, run until green

**Files:**
- Create: `.github/workflows/mac.yml`
- Modify: whatever the Mac run reports (each fix the minimal one, TDD where a test can show it)

- [ ] **Step 1: The workflow**

`.github/workflows/mac.yml`:

```yaml
name: Mac

# Only when started by hand (Actions → Mac → Run workflow) or on a release
# tag: Mac minutes count ten times on a private repo.
"on":
  workflow_dispatch:
  push:
    tags: ["v*"]

jobs:
  mac:
    runs-on: macos-15
    timeout-minutes: 60
    env:
      VCPKG_ROOT: ${{ github.workspace }}/../vcpkg
      VCPKG_BINARY_SOURCES: clear;files,${{ github.workspace }}/../vcpkg-cache,readwrite
    steps:
      - uses: actions/checkout@v4

      - name: Xcode
        id: xcode
        run: |
          xcodebuild -version
          echo "version=$(xcodebuild -version | head -1 | tr ' ' '-')" >> "$GITHUB_OUTPUT"

      - name: Install Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: "6.10.2"
          host: mac
          target: desktop
          arch: clang_64
          modules: qtmultimedia
          cache: true

      - name: Tools
        run: brew install ninja

      # The same vcpkg commit as vcpkg.json's builtin-baseline.
      - name: Get vcpkg
        run: |
          baseline=$(python3 -c "import json;print(json.load(open('vcpkg.json'))['builtin-baseline'])")
          git clone --quiet https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
          git -C "$VCPKG_ROOT" checkout --quiet "$baseline"
          "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
          mkdir -p "${{ github.workspace }}/../vcpkg-cache"

      # Restored and saved separately: saved even when the build fails, so
      # a second attempt skips the libraries. The Xcode version is in the key
      # (RtAudio's exported target records Xcode's framework paths).
      - name: Restore the library cache
        id: cache
        uses: actions/cache/restore@v4
        with:
          path: ${{ github.workspace }}/../vcpkg-cache
          key: vcpkg-mac-${{ steps.xcode.outputs.version }}-${{ hashFiles('vcpkg.json', 'ports/**', 'triplets/**') }}
          restore-keys: vcpkg-mac-${{ steps.xcode.outputs.version }}-

      - name: Configure
        run: cmake --preset mac-release

      - name: Build
        run: cmake --build --preset mac-release

      - name: Save the library cache
        if: always() && steps.cache.outputs.cache-hit != 'true'
        uses: actions/cache/save@v4
        with:
          path: ${{ github.workspace }}/../vcpkg-cache
          key: vcpkg-mac-${{ steps.xcode.outputs.version }}-${{ hashFiles('vcpkg.json', 'ports/**', 'triplets/**') }}

      - name: Test plugins
        run: brew install --cask surge-xt

      - name: Test
        run: ctest --preset mac-release

      - name: Package
        run: bash tools/package-mac.sh build/mac-release "$(grep -E 'set\(PRODUCT_VERSION' branding.cmake | sed -E 's/.*"(.*)".*/\1/')"

      - name: Upload the .dmg
        uses: actions/upload-artifact@v4
        with:
          name: GigChain-Keys-mac-arm64
          path: build/mac-release/*.dmg
          if-no-files-found: error

      - name: Upload test logs
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: mac-test-logs
          path: |
            build/mac-release/tests/**/*.log
            build/mac-release/Testing/Temporary/LastTest.log
          if-no-files-found: ignore
```

Before the first push, check the YAML locally (`python -c "import yaml,sys;yaml.safe_load(open('.github/workflows/mac.yml'))"`)
and check each step's commands for typos. Every failed run costs minutes.

- [ ] **Step 2: Commit, push the branch, start the run**

```bash
git add .github/workflows/mac.yml
git commit -m "ci: the Mac build (by hand or on a release tag): build, test, .dmg"
git push -u origin feat/cross-platform   # starts Mac run 1
```

GitHub runs a `workflow_dispatch` workflow only once the file is on the
default branch (`main`), and this branch is not merged. So while this task
runs, `mac.yml` also has a push trigger limited to this branch:

```yaml
  push:
    tags: ["v*"]
    branches: [feat/cross-platform] # (piece 2's runs; removed when it is done)
```

Push **only** when a fix is ready: each push is a Mac run. Pushing
`feat/cross-platform` costs no Windows minutes (Windows CI runs on `main`
and pull requests only). Replace `gh workflow run` above with the push
itself. At the end of the task, remove the `branches:` line in the final
commit, **and do not push that commit**: it would start one more run.

- [ ] **Step 3: Read the result; fix; repeat**

Run: `gh run watch <id>` (or `gh run view <id> --log-failed | tail -200`).
Expected: green. For each failure, find the cause first, make the minimal
fix, and run the Windows gate and the Linux suites on the fix before the next
push. Record each run in the ledger (`Mac run <n>: <minutes>, <step>, <cause>`).
Stop and tell the user if five runs have not reached the Test step. That
would mean the plan's assumptions are wrong, and minutes are being burned.

- [ ] **Step 4: The .dmg is sound**

From the green run, download the artifact (`gh run download <id>`) and check
it is a UDZO disk image of reasonable size (tens of MB). Mount checks happen
on the friend's Mac.

---

### Task 8: The friend's checklist, README, roadmap

**Files:**
- Create: `docs/testing/mac-checklist.md` (the spec's checklist, for the tester, in plain words)
- Modify: `README.md` (a "Building on macOS" section: the workflow, the preset, and Open Anyway for testers)
- Modify: `docs/ROADMAP.md` (piece 2 done; piece 3 next)

- [ ] **Step 1: Write the three docs** from the spec's "The friend's M5
  checklist" and "The build on GitHub" sections: what to download, Open
  Anyway (System Settings → Privacy & Security), the microphone prompt, what
  to try, and where the logs are.

- [ ] **Step 2: Commit**

```bash
git add docs/testing/mac-checklist.md README.md docs/ROADMAP.md
git commit -m "docs: building on macOS, the Mac tester's checklist, the roadmap"
```
