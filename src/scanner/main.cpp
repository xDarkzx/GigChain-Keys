// The plugin scanner: reads one VST3 plugin and writes what it found, for
// the app's plugin scan (PluginCatalog::scan). It runs as a process of its
// own, as Audacity 4 reads plugins, so a plugin that crashes while being read
// ends this process and never the app.
//
//   <scanner> <plugin.vst3> <result.json>
//
// Exit codes: 0 read (the result says whether the plugin loaded), 2 wrong
// arguments, 3 the result could not be written. Anything else: the plugin
// crashed it.
#include "PluginCatalog.h"

#include "gigchain/engine/ProcessHardening.h"

#include <QCoreApplication>

#include <windows.h>

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {

// A broken plugin crashing is expected here: no Windows error dialog and no
// debugger offered (nobody would answer them); the app reads the exit code.
LONG WINAPI endQuietly(EXCEPTION_POINTERS* crash)
{
    TerminateProcess(GetCurrentProcess(), crash->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

int main(int argc, char** argv)
{
    // It loads plugins: no DLL from the folder it was started in (said on
    // stderr, which the app logs, if it cannot).
    (void)gigchain::engine::hardenDllSearch();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(&endQuietly);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG // the debug C runtime's reports: printed, not a dialog (release builds have none)
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif

    const QCoreApplication app(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() != 3) {
        std::fputs("usage: <scanner> <plugin.vst3> <result.json>\n", stderr);
        return 2;
    }
    return gigchain::engine::PluginCatalog::readToFile(arguments.at(1), arguments.at(2)) ? 0 : 3;
}
