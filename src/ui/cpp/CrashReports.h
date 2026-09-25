#pragma once

#include <QString>
#include <QStringList>

namespace gigchain::ui {

// If the app crashes, Windows hands the crash to this before the process
// ends: a crash dump (.dmp, opened with Visual Studio or WinDbg to see where
// it crashed) and a note with the last thing the app was doing are written
// to the reports folder. At the next start the new report is logged and
// the user told. (Audacity 4 does this with crashpad in a separate process;
// here it is written in-process with the Windows debug helper.)
//
// Everything the crash handler needs is prepared at install(): at crash
// time nothing is allocated and no Qt code runs.
class CrashReports
{
public:
    // Main thread, once at start-up.
    static void install(const QString& folder);
    static void uninstall();

    // What the app is doing (any thread); written into the note of a crash.
    static void setLastAction(const QString& action);

    // Reports written since the last call (at start-up: by the last run's
    // crash). Each is then remembered as seen.
    [[nodiscard]] static QStringList takeNewReports();

    // Writes a report now without crashing (tests, diagnostics). Returns the
    // .dmp path, empty on failure (logged).
    static QString writeNow(const char* reason);
};

} // namespace gigchain::ui
