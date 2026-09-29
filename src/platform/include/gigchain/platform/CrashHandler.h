#pragma once

#include "gigchain/core/Error.h"

#include <QByteArray>
#include <QString>

namespace gigchain::platform {

// Catches the app's crash as the system allows and writes a report into a
// folder before the process ends: Windows a crash dump (.dmp, for Visual
// Studio or WinDbg) and a note beside it (.txt); elsewhere one note (.crash)
// with the reason, the signal and a backtrace. Each says what the app was
// doing (setCrashContext).
//
// Everything the handler needs is prepared at install: at crash time nothing
// is allocated and no Qt code runs. A crash while writing (a second fault)
// skips the rest and ends the process.

// Main thread, once. `folder` must exist; `prefix` starts each report's
// name. False (logged, with the reason) when it cannot be installed.
[[nodiscard]] bool installCrashHandler(const QString& folder, const QString& prefix);
void uninstallCrashHandler();
// The folder reports go to; empty when not installed.
[[nodiscard]] QString crashFolder();

// What the app is doing (any thread), copied for a report.
void setCrashContext(const QByteArray& utf8);

// Writes a report now without crashing (tests, diagnostics): its path, or
// why it could not.
[[nodiscard]] core::Result<QString> writeCrashReportNow(const char* reason);

// The file name pattern of reports ("*.dmp" on Windows, "*.crash" elsewhere),
// and the note that goes with a report (the report itself where it is one).
[[nodiscard]] QString crashReportPattern();
[[nodiscard]] QString crashNoteOf(const QString& report);

} // namespace gigchain::platform
