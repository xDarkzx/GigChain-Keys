#pragma once

#include "gigchain/core/Error.h"

#include <QString>

class QProcess;

namespace gigchain::platform {

// Takes the current folder out of where the system looks for libraries a
// program asks for by name. Windows: double-clicking a setlist starts the app
// in the setlist's folder (often Downloads), and a DLL left there must never
// load; plugins still load their own DLLs from next to them. Elsewhere
// libraries load by full path: nothing to do. Call first thing in main(); an
// error says why it could not (the app goes on).
core::Result<void> hardenLibrarySearch();

// While alive, the system loader reports bad or missing libraries (corrupt,
// 32-bit, missing dependency) to the caller as errors instead of showing a
// modal dialog that would block the app (Windows; nothing elsewhere). The
// caller still receives the failure and must return and log it. Restores
// the previous mode on exit.
class SilentLoaderErrors
{
public:
    SilentLoaderErrors();
    ~SilentLoaderErrors();
    SilentLoaderErrors(const SilentLoaderErrors&) = delete;
    SilentLoaderErrors& operator=(const SilentLoaderErrors&) = delete;
    SilentLoaderErrors(SilentLoaderErrors&&) = delete;
    SilentLoaderErrors& operator=(SilentLoaderErrors&&) = delete;

private:
    unsigned long m_previous = 0; // Windows' previous error mode
};

// A helper process the app starts (the plugin scanner) opens no window of
// its own (Windows: no console window).
void quietChildProcess(QProcess& process);

// For the plugin scanner: a broken plugin crashing it is expected, so the
// process ends at once with an exit code the app reads (never 0, 2 or 3),
// with no error dialog and no debugger offered (nobody would answer them).
void endQuietlyOnCrash();

// A program's file name here ("<base>.exe" on Windows, "<base>" elsewhere).
[[nodiscard]] QString programFileName(const QString& base);

// Opens the system's file manager at `path` (Windows: Explorer with the file
// selected; elsewhere its folder). An error with the reason when it cannot.
core::Result<void> showInFileManager(const QString& path);

// Development builds (Windows' debug C runtime): memory leaks are reported
// to the debugger's output when the app ends. Nothing elsewhere.
void reportLeaksAtExit();

} // namespace gigchain::platform
