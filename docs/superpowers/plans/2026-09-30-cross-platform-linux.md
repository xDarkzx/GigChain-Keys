# Cross-platform piece 1 (platform layer + Linux) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every piece of Windows-only code behind a small platform layer (Windows unchanged), then add the Linux side so GigChain Keys builds, passes its tests and runs in WSL with a Linux VST3 plugin.

**Architecture:** A static library `gigchain_platform` (`src/platform/`) with one interface per piece and one file per system (`_win.cpp`, `_posix.cpp`, `_linux.cpp`), chosen by CMake. Pieces that need engine internals (the VST3 run loop) are system-suffixed files next to their users. A ctest scans `src/` so no system header or `#ifdef _WIN32` appears outside those files. Linux builds in WSL with GCC 13, Qt 6.10 (official binaries) and vcpkg `x64-linux`.

**Tech Stack:** C++20, Qt 6.10.2, CMake ≥ 3.24 + Ninja, vcpkg, RtAudio/RtMidi (our overlay ports), Steinberg VST3 SDK 3.8, MSVC (Windows), GCC 13 (Linux).

**Spec:** `docs/superpowers/specs/2026-09-30-cross-platform-linux-design.md`

## Global Constraints

- Windows stays exactly as it works today: its code is moved, not rewritten; `tools\verify.ps1` (the commit hook) passes on every commit.
- Code outside the platform files has no `#ifdef _WIN32` / `__linux__` / `__APPLE__` and no system headers (`windows.h`, `unistd.h`, ...); a ctest enforces it.
- Every failure is returned with its precise cause and logged (never silent).
- Linux: plugin windows are X11 (`kPlatformTypeX11EmbedWindowID`), the app runs Qt's `xcb` platform unless the player sets another; the host provides `Steinberg::Linux::IRunLoop`.
- Linux audio: System = PulseAudio, pro = JACK, also ALSA; MIDI = ALSA. Settings offers only what the system has; a saved driver the system lacks falls back to System, logged and told once.
- Standard VST3 folders: Windows `C:/Program Files/Common Files/VST3`; Linux `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3`.
- Linux build: WSL Ubuntu 22.04, GCC 13, CMake ≥ 3.24, Qt 6.10.2 `gcc_64`, vcpkg `x64-linux`; presets `linux-debug`, `linux-release`, `linux-asan`; `tools/run.sh`, `tools/verify.sh`.
- Real-instrument tests use Piano V2 on Windows, Surge XT on Linux, and skip (saying so) when none is installed.
- Source edits with the Edit/Write tools only (never sed/Python rewrites).

## Review Focus

1. **A crash on Linux inside the signal handler** (the handler itself must not allocate or call Qt): a second fault while writing must not hang; the note is still written. Test: Task 5, `aCrashInAChildLeavesItsNote` runs the crash in a child process and checks the note and exit.
2. **A stale lock file after a crash** (Linux single instance): the next start must become the first, not hand over to nobody. Test: Task 6, `aLockLeftByACrashDoesNotBlock`.
3. **A plugin that registers run-loop timers/handlers and never unregisters** (closes its editor): nothing may call into it after the window closes. Test: Task 9, `closingTheEditorStopsItsTimers`.
4. **A setlist or settings saved on Windows with ASIO, opened on Linux:** System audio, logged and said once, not a failure to start. Test: Task 8, `aDriverThisSystemLacksFallsBackToSystem`.
5. **Two users on one Linux machine** (single instance): each user's app is their own; the socket is not in shared `/tmp` with a shared name. Test: Task 6, `theSocketIsPerUser`.

---

### Task 1: The platform library and its boundary check

**Files:**
- Create: `src/platform/CMakeLists.txt`, `src/platform/include/gigchain/platform/PluginFolders.h`, `src/platform/PluginFolders_win.cpp`, `src/platform/PluginFolders_linux.cpp`, `src/platform/include/gigchain/platform/MemoryUse.h`, `src/platform/MemoryUse_win.cpp`, `src/platform/MemoryUse_linux.cpp`
- Create: `cmake/PlatformSources.cmake` (the per-system source picker), `cmake/CheckPlatformBoundary.cmake`
- Modify: `CMakeLists.txt` (add `src/platform` before `src/engine`), `src/engine/internal/PluginCatalog.cpp` (`standardFolder()`, `fingerprintOf`'s `x86_64-win`), `src/engine/CMakeLists.txt` (link `gigchain::platform`), `src/ui/cpp/EngineStatus.cpp` (`readMemoryMb`), `src/ui/CMakeLists.txt` (link)
- Test: `tests/platform/tst_platform.cpp`, `tests/platform/CMakeLists.txt`, `tests/CMakeLists.txt` (add subdirectory; register `platform_boundary`)

**Interfaces:**
- Produces:
  - `QStringList gigchain::platform::standardVst3Folders()` — absolute folders, in search order.
  - `QString gigchain::platform::vst3ModuleFolder()` — the bundle subfolder holding this system's binary: `"x86_64-win"` / `"x86_64-linux"`.
  - `core::Result<qint64> gigchain::platform::residentBytes()` — the process's resident memory.
  - CMake function `gigchain_platform_sources(<target> <base>...)`: adds `<base>_win.cpp` on Windows, `<base>_posix.cpp` if it exists and not Windows, `<base>_linux.cpp` on Linux.
  - ctest `platform_boundary`.

- [ ] **Step 1: Write the failing tests**

`tests/platform/tst_platform.cpp`:
```cpp
// The platform layer: what differs by system, each piece on its own.
#include "gigchain/platform/MemoryUse.h"
#include "gigchain/platform/PluginFolders.h"

#include <QDir>
#include <QtTest>

using namespace gigchain;

class TestPlatform : public QObject
{
    Q_OBJECT

private slots:
    void theStandardPluginFoldersAreAbsolute()
    {
        const QStringList folders = platform::standardVst3Folders();
        QVERIFY(!folders.isEmpty());
        for (const QString& folder : folders) QVERIFY2(QDir::isAbsolutePath(folder), qPrintable(folder));
#ifdef Q_OS_WIN
        QCOMPARE(folders.front(), QStringLiteral("C:/Program Files/Common Files/VST3"));
        QCOMPARE(platform::vst3ModuleFolder(), QStringLiteral("x86_64-win"));
#else
        QVERIFY(folders.contains(QDir::homePath() + QStringLiteral("/.vst3")));
        QVERIFY(folders.contains(QStringLiteral("/usr/lib/vst3")));
        QCOMPARE(platform::vst3ModuleFolder(), QStringLiteral("x86_64-linux"));
#endif
    }

    void theMemoryInUseIsRead()
    {
        const auto bytes = platform::residentBytes();
        QVERIFY2(bytes.has_value(), bytes ? "" : qPrintable(bytes.error().message));
        QVERIFY(*bytes > 1024 * 1024);        // a Qt test program: megabytes
        QVERIFY(*bytes < 64LL * 1024 * 1024 * 1024);
    }
};

QTEST_GUILESS_MAIN(TestPlatform)
#include "tst_platform.moc"
```
(`Q_OS_WIN` in a test is allowed: `tests/` is not under the boundary check.)

`tests/platform/CMakeLists.txt`:
```cmake
gigchain_add_test(tst_platform SOURCES tst_platform.cpp LIBS gigchain::platform)
```
In `tests/CMakeLists.txt` add `add_subdirectory(platform)` and:
```cmake
# No system code outside the platform files (docs/superpowers/specs/2026-09-30-cross-platform-linux-design.md).
add_test(NAME platform_boundary
         COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src -P ${PROJECT_SOURCE_DIR}/cmake/CheckPlatformBoundary.cmake)
```

`cmake/CheckPlatformBoundary.cmake`:
```cmake
# Fails when system code appears outside the platform files: files named
# *_win.*, *_posix.*, *_linux.*, *_mac.* may use it; nothing else may.
file(GLOB_RECURSE sources "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.mm")
set(offenders "")
foreach(source IN LISTS sources)
    get_filename_component(stem "${source}" NAME_WE)
    if(stem MATCHES "_(win|posix|linux|mac)$")
        continue()
    endif()
    file(STRINGS "${source}" lines REGEX
        "#include <(windows|winsock2|psapi|dbghelp|timeapi|crtdbg|unistd|dlfcn|signal|execinfo|pthread)\\.h>|#include <(sys|X11|mach)/|_WIN32|__linux__|__APPLE__|_MSC_VER|\\bHWND\\b")
    if(lines)
        string(REPLACE "${SOURCE_DIR}/" "" relative "${source}")
        list(APPEND offenders "${relative}: ${lines}")
    endif()
endforeach()
if(offenders)
    list(JOIN offenders "\n  " report)
    message(FATAL_ERROR "System code outside the platform files:\n  ${report}")
endif()
message(STATUS "platform boundary: clean (${CMAKE_MATCH_COUNT} files checked)")
```

- [ ] **Step 2: Run to see it fail**

Run: `tools\build.ps1 -Target tst_platform -Filter "tst_platform|platform_boundary"`
Expected: configure fails — `gigchain::platform` does not exist. (Once the library exists, `platform_boundary` must fail listing the current offenders: `app/main.cpp`, `engine/internal/LoaderErrors.h`, `MidiClockOut.cpp`, `PluginCatalog.cpp`, `ProcessHardening.cpp`, `Vst3Node.cpp`, `scanner/main.cpp`, `ui/cpp/CrashReports.cpp`, `EngineStatus.cpp`, `PluginEditorHost.cpp`, `SingleInstance.cpp`, `SingleInstance.h`. Tasks 2–7 empty that list; the test is expected to fail until Task 7.)

- [ ] **Step 3: Implement**

`cmake/PlatformSources.cmake`:
```cmake
# Adds the system's own files of each platform piece to `target`:
# <base>_win.cpp on Windows; elsewhere <base>_posix.cpp (when there is one)
# and <base>_linux.cpp on Linux (<base>_mac.mm on macOS, piece 2).
function(gigchain_platform_sources target)
    foreach(base IN LISTS ARGN)
        if(WIN32)
            target_sources(${target} PRIVATE ${base}_win.cpp)
        else()
            if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${base}_posix.cpp)
                target_sources(${target} PRIVATE ${base}_posix.cpp)
            endif()
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${base}_linux.cpp)
                target_sources(${target} PRIVATE ${base}_linux.cpp)
            endif()
        endif()
    endforeach()
endfunction()
```
Include it from the top-level `CMakeLists.txt` next to the other `include()`s.

`src/platform/CMakeLists.txt`:
```cmake
# What differs by system: one interface per piece, one file per system
# (docs/superpowers/specs/2026-09-30-cross-platform-linux-design.md).
add_library(gigchain_platform STATIC)
add_library(gigchain::platform ALIAS gigchain_platform)
target_include_directories(gigchain_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gigchain_platform PUBLIC gigchain::core Qt6::Core)
if(WIN32)
    target_compile_definitions(gigchain_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
gigchain_platform_sources(gigchain_platform PluginFolders MemoryUse)
gigchain_target_defaults(gigchain_platform)
```

`PluginFolders.h`:
```cpp
#pragma once

#include <QString>
#include <QStringList>

namespace gigchain::platform {

// The folders where this system keeps VST3 plugins, absolute, in the order
// they are searched (the VST3 standard for each system).
[[nodiscard]] QStringList standardVst3Folders();

// The folder inside a .vst3 bundle's Contents holding this system's binary.
[[nodiscard]] QString vst3ModuleFolder();

} // namespace gigchain::platform
```
`PluginFolders_win.cpp`: returns `{u"C:/Program Files/Common Files/VST3"_s}` (the string moved from `PluginCatalog::standardFolder()`) and `u"x86_64-win"_s`.
`PluginFolders_linux.cpp`:
```cpp
#include "gigchain/platform/PluginFolders.h"

#include <QDir>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

QStringList standardVst3Folders()
{
    return {QDir::homePath() + u"/.vst3"_s, u"/usr/lib/vst3"_s, u"/usr/local/lib/vst3"_s};
}

QString vst3ModuleFolder()
{
    return u"x86_64-linux"_s;
}

} // namespace gigchain::platform
```

`MemoryUse.h`:
```cpp
#pragma once

#include "gigchain/core/Error.h"

#include <QtGlobal>

namespace gigchain::platform {

// The memory this process is using now (its working set / resident set),
// in bytes; an error with the reason when the system would not say.
[[nodiscard]] core::Result<qint64> residentBytes();

} // namespace gigchain::platform
```
`MemoryUse_win.cpp`: the `GetProcessMemoryInfo` call moved from `EngineStatus::readMemoryMb`, returning `counters.WorkingSetSize`, or `core::fail(ErrorCode::SystemRefused, u"GetProcessMemoryInfo failed, error %1"_s.arg(GetLastError()))`.
`MemoryUse_linux.cpp`:
```cpp
#include "gigchain/platform/MemoryUse.h"

#include <QFile>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

core::Result<qint64> residentBytes()
{
    QFile status(u"/proc/self/status"_s);
    if (!status.open(QIODevice::ReadOnly)) {
        return core::fail(core::ErrorCode::SystemRefused, u"cannot read /proc/self/status: %1"_s.arg(status.errorString()));
    }
    // "VmRSS:    123456 kB"
    for (const QByteArray& line : status.readAll().split('\n')) {
        if (!line.startsWith("VmRSS:")) continue;
        bool ok = false;
        const qint64 kb = line.mid(6).trimmed().split(' ').value(0).toLongLong(&ok);
        if (ok) return kb * 1024;
    }
    return core::fail(core::ErrorCode::ParseFailed, u"no VmRSS line in /proc/self/status"_s);
}

} // namespace gigchain::platform
```
Callers: `PluginCatalog::standardFolder()` returns `platform::standardVst3Folders().front()` (Windows unchanged: one folder); where the catalog scans the standard folder, scan every folder of `standardVst3Folders()` that exists. `fingerprintOf` uses `u"/Contents/"_s + platform::vst3ModuleFolder() + u'/' + file.fileName()` (on Linux the binary is `<name>.so`: use `completeBaseName() + ".so"` when `vst3ModuleFolder()` ends with `linux` — write it as a helper `moduleFileOf(bundle)` in `PluginFolders.h`: `[[nodiscard]] QString vst3ModuleFile(const QString& bundle);` with the Windows file returning `bundle + "/Contents/x86_64-win/" + name` and Linux `bundle + "/Contents/x86_64-linux/" + baseName + ".so"`). `EngineStatus::readMemoryMb()` calls `platform::residentBytes()`, logs the error once (as now), and rounds to whole MB (as now). Remove `windows.h`/`psapi.h` from `EngineStatus.cpp` and `windows.h` from `PluginCatalog.cpp` only once Task 2 moves `CREATE_NO_WINDOW` (leave that include until then).

- [ ] **Step 4: Run to see it pass**

Run: `tools\build.ps1 -Target tst_platform -Filter "tst_platform|tst_plugin_catalog|tst_plugin_scanner|tst_real_engine"`
Expected: PASS (`platform_boundary` still fails, listing the remaining offenders — expected until Task 7; not in this filter).

- [ ] **Step 5: Commit** (the commit hook runs every test; `platform_boundary` is marked `WILL_FAIL`-free, so for Tasks 1–6 register it with `set_tests_properties(platform_boundary PROPERTIES DISABLED ON)` and a comment "enabled in Task 7"; Task 7 removes that line)

```bash
git add src/platform cmake tests CMakeLists.txt src/engine src/ui
git commit -m "feat: a platform layer, starting with plugin folders and memory use"
```

---

### Task 2: Process hardening, loader errors and the scanner's quiet start

**Files:**
- Create: `src/platform/include/gigchain/platform/Process.h`, `src/platform/Process_win.cpp`, `src/platform/Process_posix.cpp`
- Modify: `src/engine/internal/ProcessHardening.cpp` → delete; `src/engine/include/gigchain/engine/ProcessHardening.h` → delete (callers use the platform one); `src/engine/internal/LoaderErrors.h` → delete (users include `gigchain/platform/Process.h`); `src/engine/internal/PluginCatalog.cpp` (the `CREATE_NO_WINDOW` modifier); `src/scanner/main.cpp`; `src/app/main.cpp` (`hardenDllSearch` call, the `_MSC_VER` debug-report block); `tests/common/NoErrorDialogs.cpp` → keep on Windows only, call `platform::quietCrashes()` elsewhere
- Test: `tests/engine/tst_process_hardening.cpp` → move to `tests/platform/tst_process.cpp`

**Interfaces:**
- Produces (`gigchain/platform/Process.h`):
  - `core::Result<void> hardenLibrarySearch();` — Windows: `SetDllDirectoryW(L"")` (moved verbatim); POSIX: returns `{}` (plugins load by full path; nothing to do).
  - `class SilentLoaderErrors` — RAII; Windows: `SetThreadErrorMode` (moved; member `unsigned long m_previous`); POSIX: no-op.
  - `void quietChildProcess(QProcess& process);` — Windows: the `CREATE_NO_WINDOW` modifier (moved); POSIX: nothing.
  - `void endQuietlyOnCrash();` — the scanner's crash behaviour. Windows: `SetErrorMode`, `SetUnhandledExceptionFilter(endQuietly)`, `_set_abort_behavior`, the `_DEBUG` report redirection (all moved from `scanner/main.cpp`). POSIX: `sigaction` for SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT to a handler that `_exit(128 + signal)`.
  - `void debugReportsToStderr();` — the `_MSC_VER && _DEBUG` block from `app/main.cpp` (Windows), nothing on POSIX.

- [ ] **Step 1: Write the failing test** — move `tst_process_hardening.cpp`'s cases into `tests/platform/tst_process.cpp` against `platform::hardenLibrarySearch()` and add:
```cpp
    // The scanner's crash ending: a plugin that crashes ends the process
    // with an exit code (the app reads it), no dialog, no hang.
    void aCrashEndsQuietlyWithACode()
    {
        QProcess child;
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({QStringLiteral("--crash-quietly")});
        child.start();
        QVERIFY(child.waitForFinished(10'000));
        QVERIFY(child.exitCode() != 0 || child.exitStatus() == QProcess::CrashExit);
    }
```
with, in the test's `main` (use a custom `main` instead of `QTEST_GUILESS_MAIN`):
```cpp
int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--crash-quietly") {
        gigchain::platform::endQuietlyOnCrash();
        volatile int* nothing = nullptr;
        return *nothing; // NOLINT: the crash under test
    }
    QCoreApplication app(argc, argv);
    TestProcess test;
    return QTest::qExec(&test, argc, argv);
}
```
- [ ] **Step 2: Run to see it fail** — `tools\build.ps1 -Target tst_process -Filter tst_process` → compile error, `gigchain/platform/Process.h` not found.
- [ ] **Step 3: Implement** — create the header and the two files; move the Windows code verbatim (bodies of `hardenDllSearch`, `SilentLoaderErrors`, the scanner's `endQuietly`/`SetErrorMode`/`_set_abort_behavior`/`_DEBUG` loop, the `CREATE_NO_WINDOW` lambda, main.cpp's `_MSC_VER` block). POSIX:
```cpp
#include "gigchain/platform/Process.h"

#include <QProcess>

#include <csignal>
#include <unistd.h>

namespace gigchain::platform {
namespace {

void endNow(int signal)
{
    _exit(128 + signal); // async-signal-safe: nothing else runs
}

} // namespace

core::Result<void> hardenLibrarySearch()
{
    return {}; // plugins load by full path; the plugin-folder rule is shared code
}

SilentLoaderErrors::SilentLoaderErrors() = default;
SilentLoaderErrors::~SilentLoaderErrors() = default;

void quietChildProcess(QProcess&) {}

void endQuietlyOnCrash()
{
    struct sigaction action{};
    action.sa_handler = &endNow;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(signal, &action, nullptr);
}

void debugReportsToStderr() {}

} // namespace gigchain::platform
```
`SilentLoaderErrors` is declared in the header with out-of-line constructor/destructor (so the header stays portable). The scanner's `main` becomes: `(void)platform::hardenLibrarySearch(); platform::endQuietlyOnCrash();` then as before. `app/main.cpp` calls `platform::debugReportsToStderr()` and `platform::hardenLibrarySearch()` where it called the old ones. The scanner and `PluginCatalog` exit-code handling already treats any code other than 0/2/3 as "the plugin crashed it".
- [ ] **Step 4: Run to see it pass** — `tools\build.ps1 -Target tst_process -Filter "tst_process|tst_plugin_scanner|tst_plugin_catalog|tst_real_engine"` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "refactor: process hardening, loader errors and the scanner's quiet end in the platform layer"`.

---

### Task 3: Precise timing, bringing the window forward, plugin-window flicker filter

**Files:**
- Create: `src/platform/include/gigchain/platform/Timing.h`, `Timing_win.cpp`, `Timing_posix.cpp`; `src/platform/include/gigchain/platform/Windows.h`, `Windows_win.cpp`, `Windows_posix.cpp`
- Modify: `src/engine/internal/MidiClockOut.cpp` (`run()`), `src/engine/CMakeLists.txt` (`winmm` moves to the platform library's Windows link), `src/app/main.cpp` (`bringToFront`), `src/ui/cpp/PluginEditorHost.cpp` (`EraseFilter`), `src/platform/CMakeLists.txt` (Qt6::Gui; `winmm` on Windows)
- Test: `tests/platform/tst_platform.cpp`

**Interfaces:**
- `void platform::preciseTimingForThisThread();` — Windows: `timeBeginPeriod(1)` + `SetThreadPriority(TIME_CRITICAL)` (moved); POSIX: nothing.
- `void platform::bringToFront(QWindow& window);` — Windows: the function moved from `app/main.cpp` verbatim (its log lines go to a `lcPlatform` category, `gigchain.platform`); POSIX: `window.raise(); window.requestActivate();`.
- `std::unique_ptr<QAbstractNativeEventFilter> platform::makeNoFlickerFilter(WId pluginWindow);` — Windows: the `EraseFilter` moved from `PluginEditorHost.cpp`; POSIX: `nullptr`.
- `enum class NativeWindowKind { Win32, X11, Cocoa };` and `NativeWindowKind platform::nativeWindowKind();` (Windows: `Win32`; Linux: `X11`).

- [ ] **Step 1: Write the failing test** (add to `tst_platform.cpp`, GUI test → make `tst_platform` a `QTEST_MAIN` with `Qt6::Gui`; on Linux run with `QT_QPA_PLATFORM=offscreen` from the test's `ENVIRONMENT`):
```cpp
    void theNativeWindowKindIsThisSystems()
    {
#ifdef Q_OS_WIN
        QCOMPARE(platform::nativeWindowKind(), platform::NativeWindowKind::Win32);
#else
        QCOMPARE(platform::nativeWindowKind(), platform::NativeWindowKind::X11);
#endif
    }
    void bringingAWindowForwardDoesNotFail()
    {
        QWindow window;
        window.resize(100, 100);
        window.show();
        platform::bringToFront(window); // nothing to measure off-screen: it must not crash or throw
        QVERIFY(window.isVisible());
    }
```
- [ ] **Step 2:** `tools\build.ps1 -Target tst_platform -Filter tst_platform` → compile error, `Timing.h`/`Windows.h` missing.
- [ ] **Step 3: Implement** as in Interfaces (moves verbatim; the `EraseFilter` class moves whole, `PluginEditorHost` keeps `std::unique_ptr<QAbstractNativeEventFilter> m_eraseFilter` and installs it only when non-null).
- [ ] **Step 4:** `tools\build.ps1 -Target tst_platform -Filter "tst_platform|tst_midi_clock|tst_plugin_view|tst_qml_smoke"` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "refactor: timing, window raising and the flicker filter in the platform layer"`.

---

### Task 4: Which window a plugin editor gets

**Files:**
- Modify: `src/engine/include/gigchain/engine/IPluginEditor.h` (`attach`), `src/engine/internal/Vst3Node.cpp` (`Vst3Editor::attach`), `src/ui/cpp/PluginEditorHost.cpp:141`, `src/ui/cpp/EffectWindows.cpp:168`, test editors in `tests/` that implement `IPluginEditor`
- Test: `tests/engine/tst_vst3_editor.cpp`

**Interfaces:**
- `struct NativeParent { quintptr handle = 0; platform::NativeWindowKind kind = platform::NativeWindowKind::Win32; };` in `IPluginEditor.h`; `virtual core::Result<void> attach(NativeParent parent) = 0;`
- `Vst3Editor::attach` maps `Win32 → kPlatformTypeHWND`, `X11 → kPlatformTypeX11EmbedWindowID`, `Cocoa → kPlatformTypeNSView`; the "does not support" message names the kind: `"%1's editor does not support %2 windows"` with `Windows` / `X11` / `macOS`.
- UI callers pass `{static_cast<quintptr>(window->winId()), platform::nativeWindowKind()}`.

- [ ] **Step 1: Failing test** in `tst_vst3_editor.cpp`:
```cpp
    // A plugin asked for a kind of window it does not support is refused,
    // saying which kind.
    void anUnsupportedWindowKindIsRefused()
    {
        auto editor = openFirstEditor(); // the test file's existing helper (skips without a plugin)
        const auto refused = editor->attach({.handle = 1, .kind = engine::platform::NativeWindowKind::Cocoa});
        QVERIFY(!refused.has_value());
        QVERIFY2(refused.error().message.contains(u"macOS"_s), qPrintable(refused.error().message));
    }
```
(adjust the namespace to `gigchain::platform::NativeWindowKind`).
- [ ] **Step 2:** build → compile error (`attach` takes `quintptr`).
- [ ] **Step 3: Implement** the struct, the mapping, update callers and the test doubles.
- [ ] **Step 4:** `tools\build.ps1 -Target tst_vst3_editor -Filter "tst_vst3_editor|tst_plugin_view|tst_qml_smoke|tst_document_controller"` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "refactor: a plugin editor is told which kind of window it gets"`.

---

### Task 5: Crash reports per system

**Files:**
- Create: `src/platform/include/gigchain/platform/CrashHandler.h`, `CrashHandler_win.cpp` (the handler, dump and note writing moved from `CrashReports.cpp`), `CrashHandler_posix.cpp`
- Modify: `src/ui/cpp/CrashReports.cpp` (keeps folder preparation, pruning, `takeNewReports`, `readSeen`; calls the platform handler), `src/ui/cpp/CrashReports.h` (comment: per system)
- Test: `tests/ui/tst_crash_reports.cpp`

**Interfaces (`gigchain/platform/CrashHandler.h`):**
```cpp
namespace gigchain::platform {
// Prepared once (main thread), used at crash time without allocating.
// `folder` must exist; `prefix` names the files.
[[nodiscard]] bool installCrashHandler(const QString& folder, const QString& prefix);
void uninstallCrashHandler();
// What the app is doing (any thread): copied into a fixed buffer.
void setCrashContext(const QByteArray& utf8);
// Writes a report now (tests, diagnostics): the report file's path, empty on failure.
[[nodiscard]] QString writeCrashReportNow(const char* reason);
// The report file each crash leaves (what the next start looks for):
// "*.dmp" on Windows (with a .txt note beside it), "*.crash" elsewhere (the note and a backtrace).
[[nodiscard]] QString crashReportPattern();
}
```
- POSIX handler: `sigaltstack` (64 KiB static buffer), `sigaction(SA_SIGINFO | SA_ONSTACK | SA_RESETHAND)` for SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL; the path prepared at install (`folder/prefix-` in a fixed `char[]`); in the handler: `clock_gettime` + manual digit formatting into the name (no `snprintf` with locale), `open(O_CREAT|O_WRONLY|O_TRUNC, 0600)`, `write` the reason, signal number, fault address, last action, then `backtrace` + `backtrace_symbols_fd`, `close`; then re-raise with the default action (`SA_RESETHAND` restored it). `std::set_terminate` writes a report as on Windows. A guard flag makes a second fault in the handler skip writing and go straight to re-raising.
- `CrashReports::takeNewReports` and the pruning use `platform::crashReportPattern()` instead of `*.dmp` (Windows: unchanged, `*.dmp`).

- [ ] **Step 1: Failing test** — add to `tst_crash_reports.cpp` (keep the existing Windows cases):
```cpp
    // A real crash (in a child: the test must survive) leaves a report the
    // next start finds.
    void aCrashInAChildLeavesItsNote()
    {
        QTemporaryDir folder;
        QProcess child;
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({QStringLiteral("--crash-into"), folder.path()});
        child.start();
        QVERIFY(child.waitForFinished(20'000));
        QVERIFY(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0);
        ui::CrashReports::install(folder.path());
        const QStringList reports = ui::CrashReports::takeNewReports();
        ui::CrashReports::uninstall();
        QCOMPARE(reports.size(), 1);
#ifndef Q_OS_WIN
        QFile note(reports.front());
        QVERIFY(note.open(QIODevice::ReadOnly));
        const QByteArray text = note.readAll();
        QVERIFY2(text.contains("signal 11") && text.contains("Last action: about to crash"), text.constData());
#endif
    }
```
with a custom `main` handling `--crash-into <folder>`: `CrashReports::install(folder); CrashReports::setLastAction("about to crash"); volatile int* p = nullptr; return *p;`.
- [ ] **Step 2:** on Windows this test passes already (moved code) — run it to confirm the harness works: `tools\build.ps1 -Target tst_crash_reports -Filter tst_crash_reports` → PASS on Windows. Its RED run is on Linux in Task 7 (no POSIX handler before this task's Step 3 compiles there). Ledger the ruling.
- [ ] **Step 3: Implement** the header, move the Windows handler (the `CrashState`, `writeReport`, `onCrash`, `onTerminate` and their install/uninstall) into `CrashHandler_win.cpp` verbatim, write `CrashHandler_posix.cpp` as above, and make `CrashReports.cpp` call the platform functions.
- [ ] **Step 4:** `tools\build.ps1 -Target tst_crash_reports -Filter "tst_crash_reports|tst_qml_smoke"` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "refactor: crash reports per system (Windows moved; POSIX signal handler)"`.

---

### Task 6: One app at a time, per system

**Files:**
- Create: `src/platform/include/gigchain/platform/InstanceLock.h`, `InstanceLock_win.cpp` (the mutex, moved), `InstanceLock_posix.cpp`
- Modify: `src/ui/cpp/SingleInstance.h/.cpp` (no `windows.h`; `m_mutex` becomes `platform::InstanceLock m_lock`; the socket name from the platform)
- Test: `tests/ui/tst_single_instance.cpp`

**Interfaces:**
```cpp
namespace gigchain::platform {
// Who is first: held by the first start until it ends. Windows: a named
// mutex in this session (the installer looks for it). Elsewhere: a lock file
// in the user's runtime folder (a lock left by a crashed app is taken over).
class InstanceLock {
public:
    InstanceLock();
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
    // True: this start is the first (or the check failed: logged, start anyway).
    [[nodiscard]] bool acquire(const QString& name);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
// The local socket a later start hands its setlist to: only this user's.
// Windows: "<name>-<USERNAME>" (as now). Elsewhere: a full path in the
// user's runtime folder, "<runtime>/<name>.sock".
[[nodiscard]] QString instanceSocketName(const QString& name);
}
```
- POSIX: `QLockFile` at `QStandardPaths::writableLocation(RuntimeLocation) + "/" + name + ".lock"`, `setStaleLockTime(0)` and `tryLock(0)`; when it fails with `LockFailedError` because the owner is gone, `removeStaleLockFile()` then `tryLock(0)` again. The socket name is a full path in the runtime folder (per user: `XDG_RUNTIME_DIR` is `0700`); `QLocalServer::removeServer(name)` before `listen` (a socket file left by a crash).
- [ ] **Step 1: Failing tests** — add to `tst_single_instance.cpp`:
```cpp
    void aLockLeftByACrashDoesNotBlock()
    {
        // A child takes the lock and is killed (as a crash leaves it).
        QProcess child;
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({QStringLiteral("--hold-lock"), m_name});
        child.start();
        QVERIFY(child.waitForReadyRead(10'000)); // it says "held"
        child.kill();
        QVERIFY(child.waitForFinished(10'000));
        platform::InstanceLock lock;
        QVERIFY(lock.acquire(m_name)); // the dead app's lock is taken over
    }
    void theSocketIsPerUser()
    {
        const QString name = platform::instanceSocketName(m_name);
#ifdef Q_OS_WIN
        QVERIFY(name.endsWith(u'-' + qEnvironmentVariable("USERNAME")));
#else
        QVERIFY(name.startsWith(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)));
#endif
    }
```
(`--hold-lock <name>` in the test's `main`: `InstanceLock l; if (l.acquire(name)) { puts("held"); fflush(stdout); } QThread::sleep(60);`.) On Windows a killed process releases its mutex, so the first test passes there by the system's own rule.
- [ ] **Step 2:** build → compile error (`InstanceLock.h` missing).
- [ ] **Step 3: Implement** (Windows mutex moved verbatim into `InstanceLock_win.cpp`, `instanceSocketName` returns the old `m_pipe` expression).
- [ ] **Step 4:** `tools\build.ps1 -Target tst_single_instance -Filter "tst_single_instance|tst_qml_smoke"` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "refactor: one app at a time through the platform layer (lock file and user socket on POSIX)"`.

---

### Task 7: A portable build, the boundary on, and the Linux toolchain

**Files:**
- Modify: `cmake/CompilerHardening.cmake` (GCC/Clang flags), `cmake/Vst3Sdk.cmake` (`module_linux.cpp` on Linux), `src/app/CMakeLists.txt` (`WIN32` executable only on Windows), `src/engine/CMakeLists.txt`, `src/scanner/CMakeLists.txt`, `tests/CMakeLists.txt` (`NoErrorDialogs.cpp` on Windows only), `tests/engine/CMakeLists.txt` (`ole32`, `NOMINMAX` on Windows only), `tests/CMakeLists.txt` (enable `platform_boundary`; Windows-only tests: `tst_official_artwork` only on Windows), `vcpkg.json` (per-system features), `ports/rtaudio/vcpkg.json` + `portfile.cmake` (a `jack` feature), `CMakePresets.json` (`linux-debug`, `linux-release`, `linux-asan`), `tests/common/Handles.h` (Windows only: guard its users in CMake)
- Create: `tools/setup-linux.sh`, `tools/verify.sh`, `tools/run.sh`

**Interfaces:**
- `vcpkg.json` dependencies:
```json
{
  "name": "rtaudio",
  "features": [
    { "name": "asio", "platform": "windows" },
    { "name": "pulse", "platform": "linux" },
    { "name": "alsa", "platform": "linux" },
    { "name": "jack", "platform": "linux" }
  ]
},
{ "name": "rtmidi", "features": [ { "name": "alsa", "platform": "linux" } ] }
```
- `ports/rtaudio/vcpkg.json` gains `"jack": { "description": "Build with JACK backend", "supports": "linux" }` (JACK itself from the system's `libjack-jackd2-dev`, found by RtAudio's CMake through pkg-config); `portfile.cmake`: `jack RTAUDIO_API_JACK` in `vcpkg_check_features`, and `vcpkg_find_acquire_program(PKGCONFIG)` when `pulse` or `jack`.
- `CompilerHardening.cmake`, the non-MSVC branch:
```cmake
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers
            -fstack-protector-strong -fno-plt -fstack-clash-protection
            $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
        target_link_options(${target} PRIVATE -Wl,-z,relro,-z,now -Wl,-z,noexecstack)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
```
(plus `check_pie_supported()` once at top level). `-Wno-missing-field-initializers`: designated initializers leaving members at their defaults are the codebase's style.
- Presets:
```json
{ "name": "linux-base", "hidden": true, "inherits": "base",
  "binaryDir": "$env{HOME}/.cache/gigchain/build/${presetName}",
  "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-linux", "CMAKE_CXX_COMPILER": "g++-13", "CMAKE_C_COMPILER": "gcc-13" },
  "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" } },
{ "name": "linux-debug", "inherits": "linux-base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
{ "name": "linux-release", "inherits": "linux-base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } },
{ "name": "linux-asan", "inherits": "linux-base", "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo", "GIGCHAIN_ASAN": "ON" } }
```
with matching build and test presets; the Windows presets get `"condition": {"type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows"}`. The build folder is in WSL's own disk (fast), the sources stay on `D:`. `Sanitizers.cmake` gains the GCC `-fsanitize=address,undefined -fno-omit-frame-pointer` branch.
- `tools/setup-linux.sh` (idempotent; run inside WSL): installs `build-essential ninja-build pkg-config git curl zip unzip tar python3-pip libgl1-mesa-dev libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libx11-dev libx11-xcb-dev libxcb1-dev libasound2-dev libpulse-dev libjack-jackd2-dev libfontconfig1-dev libfreetype6-dev autoconf autoconf-archive automake libtool bison flex`; GCC 13 from `ppa:ubuntu-toolchain-r/test`; CMake ≥ 3.24 with `pip3 install --user cmake`; `aqtinstall` then `aqt install-qt linux desktop 6.10.2 linux_gcc_64 -m qtmultimedia -O ~/Qt`; vcpkg cloned to `~/vcpkg` at the manifest's `builtin-baseline` and bootstrapped; Surge XT's Linux `.deb` from its official release page (`surge-xt-linux-x64-*.deb`), installed with `apt install ./…deb`; prints each tool's version at the end and exits non-zero if one is missing.
- `tools/verify.sh`: `export VCPKG_ROOT=~/vcpkg QT_ROOT_DIR=~/Qt/6.10.2/gcc_64; cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug --output-on-failure`, exit code passed through.
- `tools/run.sh`: builds `linux-release` if needed and starts the app with `QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-xcb}`, its log to `~/.cache/gigchain/run.log`.

- [ ] **Step 1: Failing check** — enable `platform_boundary` (remove the `DISABLED` line from Task 1) and run it: `ctest --preset debug -R platform_boundary` → it must now pass on Windows (Tasks 2–6 emptied the list). Then in WSL: `bash tools/setup-linux.sh` → Expected: every tool version printed, exit 0. Then `bash tools/verify.sh` → Expected: FAIL at compile (the first Linux errors).
- [ ] **Step 2: Fix Linux compile errors one by one** — each with the smallest portable change (a missing `<cstdint>`/`<algorithm>` include GCC needs, `std::` qualifiers, MSVC-only pragmas moved behind the platform files). Windows keeps building; run `tools\build.ps1` after every few fixes. No test is changed to pass on Linux unless it is Windows-only by nature (then it is limited to Windows in CMake with a comment saying why: `tst_official_artwork` (Windows `.ico` artwork), `Handles.h` users (Windows handle counting; the soak gets a Linux counter in Task 11)).
- [ ] **Step 3:** `bash tools/verify.sh` → Expected: builds; the tests run (failures are fixed in Tasks 8–10 or listed here).
- [ ] **Step 4:** on Windows, `tools\verify.ps1` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "build: GigChain builds on Linux (GCC 13, Qt 6.10, vcpkg x64-linux), system code only in the platform files"`.

---

### Task 8: Audio drivers per system

**Files:**
- Modify: `src/engine/include/gigchain/engine/EngineTypes.h` (`AudioDriver`), `src/engine/internal/AudioDevice.h/.cpp` (`AudioApi`, `toRtApi`, `apiName`, the API lists), `src/engine/internal/RealEngine.cpp` (the four `AudioDriver ↔ AudioApi` conversions, the fallback), `src/ui/cpp/SettingsController.h/.cpp` (driver names, `availableDrivers`), `src/ui/SettingsDialog.qml` (the driver box), `src/engine/internal/FakeEngine.cpp`
- Create: `src/platform/include/gigchain/platform/AudioApis.h`, `AudioApis_win.cpp`, `AudioApis_linux.cpp`
- Test: `tests/engine/tst_audio_device.cpp`, `tests/ui/tst_settings.cpp`

**Interfaces:**
- `enum class AudioDriver { System, Asio, Jack, Alsa };` (System first: saved values keep their meaning).
- `std::vector<engine::AudioDriver> platform::audioDrivers();` — Windows `{System, Asio}`; Linux `{System, Jack, Alsa}`. `AudioApis.h` includes `gigchain/engine/EngineTypes.h` (header-only types), so the platform library adds `gigchain/engine/include` as a PRIVATE include directory (types only, no link to the engine: no cycle).
- `AudioApi` becomes an alias of `AudioDriver` (one enum); `toRtApi`: `System → WINDOWS_WASAPI | LINUX_PULSE`, `Asio → WINDOWS_ASIO`, `Jack → UNIX_JACK`, `Alsa → LINUX_ALSA`, chosen through a platform function `RtAudio::Api platform::rtAudioApi(AudioDriver)` in `AudioDevice_win.cpp` / `AudioDevice_linux.cpp` (engine-internal suffix files, since they need RtAudio).
- `apiName`: `System → "WASAPI" | "PulseAudio"`, `Asio → "ASIO"`, `Jack → "JACK"`, `Alsa → "ALSA"`.
- Settings strings: `"system" | "asio" | "jack" | "alsa"`; `Q_PROPERTY(QVariantList drivers …)`: `[{id: "system", name: "Windows Audio (WASAPI)" | "PulseAudio"}, {id: "asio", name: "ASIO"}, …]` from `platform::audioDrivers()` (ASIO listed only when an ASIO device exists, as `asioAvailable` does now; JACK only when the JACK server answers). The QML combo box uses that list.
- A saved driver not in `platform::audioDrivers()`: System is used, logged `"The saved audio driver (%1) is not available on this system: using %2"`, and posted once as an Info notice.

- [ ] **Step 1: Failing tests**
`tst_settings.cpp`:
```cpp
    void aDriverThisSystemLacksFallsBackToSystem()
    {
        QSettings settings;
        settings.setValue(u"audio/driver"_s, u"jack"_s); // a Linux setting …
#ifndef Q_OS_WIN
        settings.setValue(u"audio/driver"_s, u"asio"_s); // … or a Windows one
#endif
        ui::SettingsController controller(*m_engine, *m_doc);
        QCOMPARE(controller.driver(), u"system"_s);
        QVERIFY(m_doc->notifications()->count() > 0);
    }
```
(use the file's existing fixture names for the engine and document).
`tst_audio_device.cpp`:
```cpp
    void thisSystemsDriversAreListed()
    {
        const auto drivers = platform::audioDrivers();
        QCOMPARE(drivers.front(), AudioDriver::System);
#ifdef Q_OS_WIN
        QVERIFY(std::ranges::find(drivers, AudioDriver::Asio) != drivers.end());
#else
        QVERIFY(std::ranges::find(drivers, AudioDriver::Jack) != drivers.end());
        QVERIFY(std::ranges::find(drivers, AudioDriver::Asio) == drivers.end());
#endif
    }
```
- [ ] **Step 2:** build → compile error (`AudioDriver::Jack`, `platform::audioDrivers`).
- [ ] **Step 3: Implement** per Interfaces; the Windows behaviour is identical (System = WASAPI, ASIO as before, `asioAvailable` kept as a derived property for the QML until the box uses `drivers`).
- [ ] **Step 4:** Windows `tools\build.ps1 -Filter "tst_audio_device|tst_settings|tst_real_engine|tst_qml_smoke"` → PASS; Linux `bash tools/verify.sh` → these pass (the real-engine ones play through WSLg's PulseAudio).
- [ ] **Step 5: Commit** — `git commit -m "feat: audio drivers per system (PulseAudio, JACK and ALSA on Linux)"`.

---

### Task 9: Plugin windows on Linux (X11 and the run loop)

**Files:**
- Create: `src/engine/internal/Vst3RunLoop.h` (portable declaration), `src/engine/internal/Vst3RunLoop_linux.cpp`, `src/engine/internal/Vst3RunLoop_win.cpp` (nothing: `nullptr`)
- Modify: `src/engine/internal/Vst3Node.cpp` (`Vst3Editor::queryInterface` answers `Linux::IRunLoop::iid` when there is one; `detach()` clears the run loop's registrations), `src/engine/CMakeLists.txt` (`gigchain_platform_sources(gigchain_engine internal/Vst3RunLoop)`), `src/app/main.cpp` (on Linux, `QT_QPA_PLATFORM` defaults to `xcb` before `QGuiApplication`: through a platform call `platform::prepareGuiPlatform()` — Windows: nothing; Linux: `if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");`)
- Test: `tests/engine/tst_vst3_run_loop.cpp` (Linux only in CMake), `tests/engine/tst_vst3_editor.cpp`

**Interfaces:**
```cpp
// Vst3RunLoop.h
namespace gigchain::engine {
// The host side of a plugin editor's event handling where the system needs
// one (Linux: Steinberg::Linux::IRunLoop through the editor's IPlugFrame).
// Every handler and timer is on the main thread; all are removed when the
// editor closes (clear()), whatever the plugin forgot.
class Vst3RunLoop;
[[nodiscard]] std::unique_ptr<Vst3RunLoop> makeRunLoop();   // nullptr where not needed
Steinberg::FUnknown* runLoopInterface(Vst3RunLoop* loop);     // what queryInterface hands out
void clearRunLoop(Vst3RunLoop* loop);
}
```
Linux implementation: a `Steinberg::Linux::IRunLoop` with `registerEventHandler(handler, fd)` → a `QSocketNotifier(fd, Read)` whose `activated` calls `handler->onFDIsSet(fd)`; `unregisterEventHandler(handler)` deletes that handler's notifiers; `registerTimer(handler, ms)` → a `QTimer` calling `handler->onTimer()`; `unregisterTimer(handler)`; handlers are held with `IPtr` while registered; `clear()` deletes every notifier and timer and releases every handler. Reference counting: the run loop object is owned by the editor (not the plugin): `addRef`/`release` are counted but never delete it (as `Vst3Editor` does for `IPlugFrame`).

- [ ] **Step 1: Failing tests** — `tst_vst3_run_loop.cpp` (Linux only):
```cpp
class CountingTimer final : public Steinberg::Linux::ITimerHandler { /* counts onTimer(); FUNKNOWN with a real refcount */ };
class CountingFd final : public Steinberg::Linux::IEventHandler { /* counts onFDIsSet() */ };

    void aTimerFiresUntilUnregistered()
    {
        auto loop = makeRunLoop();
        FUnknownPtr<Steinberg::Linux::IRunLoop> run(runLoopInterface(loop.get()));
        IPtr<CountingTimer> timer = owned(new CountingTimer);
        QCOMPARE(run->registerTimer(timer, 10), kResultOk);
        QTRY_VERIFY(timer->count >= 3);
        QCOMPARE(run->unregisterTimer(timer), kResultOk);
        const int count = timer->count;
        QTest::qWait(60);
        QCOMPARE(timer->count, count);
    }
    void aFileHandlerFiresWhenThereIsSomethingToRead()
    {
        auto loop = makeRunLoop();
        FUnknownPtr<Steinberg::Linux::IRunLoop> run(runLoopInterface(loop.get()));
        int pipe[2];
        QCOMPARE(::pipe(pipe), 0);
        IPtr<CountingFd> handler = owned(new CountingFd);
        QCOMPARE(run->registerEventHandler(handler, pipe[0]), kResultOk);
        QCOMPARE(::write(pipe[1], "x", 1), 1);
        QTRY_VERIFY(handler->count >= 1);
        run->unregisterEventHandler(handler);
        ::close(pipe[0]);
        ::close(pipe[1]);
    }
    void closingTheEditorStopsItsTimers()
    {
        auto loop = makeRunLoop();
        FUnknownPtr<Steinberg::Linux::IRunLoop> run(runLoopInterface(loop.get()));
        IPtr<CountingTimer> timer = owned(new CountingTimer);
        run->registerTimer(timer, 10); // never unregistered: the plugin forgot
        clearRunLoop(loop.get());
        const int count = timer->count;
        QTest::qWait(60);
        QCOMPARE(timer->count, count);
        QCOMPARE(timer->refCount(), 1); // released by the loop
    }
```
`tst_vst3_editor.cpp`: the existing "opens and sizes" tests take the first installed instrument (Surge XT on Linux) and run under `QT_QPA_PLATFORM=xcb` with WSLg (the test's CMake `ENVIRONMENT`), skipping when `DISPLAY` is empty.
- [ ] **Step 2:** Linux: `bash tools/verify.sh` → `tst_vst3_run_loop` fails to compile (no `Vst3RunLoop.h`).
- [ ] **Step 3: Implement**; `Vst3Editor` owns `std::unique_ptr<Vst3RunLoop> m_runLoop = makeRunLoop();` and in `queryInterface` answers `Steinberg::Linux::IRunLoop::iid` with `runLoopInterface(m_runLoop.get())` when non-null; `detach()` calls `clearRunLoop` after `removed()`.
- [ ] **Step 4:** Linux `bash tools/verify.sh` → PASS including `tst_vst3_editor` with Surge XT; Windows `tools\build.ps1 -Filter "tst_vst3_editor|tst_plugin_view"` → PASS.
- [ ] **Step 5:** Manual check (the spec's "done"): `bash tools/run.sh`, add Surge XT to a patch, open its window, resize it, play the on-screen keyboard; a screenshot saved with `import -window root` (ImageMagick) or WSLg's own capture into `build/linux-shots/`. Record in the ledger.
- [ ] **Step 6: Commit** — `git commit -m "feat: plugin windows on Linux (X11, and the host's run loop for the plugin's timers and events)"`.

---

### Task 10: The rest of the tests on Linux

**Files:** whatever `bash tools/verify.sh` still shows failing, each fixed at its cause (not by skipping), e.g. tests assuming `C:/` paths, `\r\n`, a Windows-only plugin name — made portable in the test.
- [ ] **Step 1:** `bash tools/verify.sh` → the list of failing tests (recorded in the ledger).
- [ ] **Step 2:** for each, the smallest fix at its cause; a test that is Windows-only by nature is limited to Windows in CMake with the reason in a comment.
- [ ] **Step 3:** `bash tools/verify.sh` → 100% of the tests that run on Linux pass; Windows `tools\verify.ps1` → PASS.
- [ ] **Step 4: Commit** — `git commit -m "test: every test that is not about Windows passes on Linux"`.

---

### Task 11: Sanitizers, fuzzers and the soak on Linux; the docs

**Files:**
- Modify: `cmake/Sanitizers.cmake` (GCC/Clang fuzz: the fuzzers build with Clang `-fsanitize=fuzzer,address` when `GIGCHAIN_FUZZ` on Linux; add a `linux-fuzz` preset with `clang-15`), `tests/soak/soak.cpp` (Linux handle/thread counts: `/proc/self/fd` entries and `/proc/self/task` entries; memory from `platform::residentBytes()`), `tools/soak.sh`, `README.md` (building on Linux), `docs/ROADMAP.md` section 7 (piece 1 done; pieces 2–4 next)
- [ ] **Step 1:** `cmake --preset linux-asan && cmake --build --preset linux-asan && ctest --preset linux-asan` → PASS (fix any finding at its cause, each with a test that fails first).
- [ ] **Step 2:** fuzzers on Linux, 5 minutes each: `bash tools/fuzz.sh` → "No fuzzer found a problem".
- [ ] **Step 3:** soak on Linux, 20 minutes against Surge XT: `bash tools/soak.sh 20` → `SOAK PASSED` (notes heard, chart followed, no growth).
- [ ] **Step 4:** Windows `tools\verify.ps1` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "test: sanitizers, fuzzers and the soak on Linux; docs for building on Linux"`.
