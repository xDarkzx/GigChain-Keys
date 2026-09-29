#pragma once

#include <QString>
#include <QStringList>

namespace gigchain::ui {

// If the app crashes, the system hands the crash to this before the process
// ends (platform::installCrashHandler): a report with the last thing the app
// was doing is written to the reports folder (Windows: a crash dump for
// Visual Studio or WinDbg and a note; Linux: a note with a backtrace). At the
// next start the new report is logged and the user told. (Audacity 4 does
// this with crashpad in a separate process; here it is written in-process.)
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
    // report's path, empty on failure (logged).
    static QString writeNow(const char* reason);

    // The note that goes with a report (what the app was doing, why).
    [[nodiscard]] static QString noteOf(const QString& report);
};

} // namespace gigchain::ui
