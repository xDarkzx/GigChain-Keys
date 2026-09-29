#include "gigchain/platform/Process.h"

#include "PlatformLog.h"

#include <QDir>
#include <QProcess>

#include <windows.h>

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

using namespace Qt::StringLiterals;

namespace gigchain::platform {
namespace {

LONG WINAPI endQuietly(EXCEPTION_POINTERS* crash)
{
    TerminateProcess(GetCurrentProcess(), crash->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

core::Result<void> hardenLibrarySearch()
{
    // An empty DLL directory removes the current folder from the search
    // (unlike SetDefaultDllDirectories, plugins keep finding DLLs of their
    // own through their usual search).
    if (SetDllDirectoryW(L"") == FALSE) {
        const QString why = u"Could not take the current folder out of the DLL search (error %1)"_s.arg(GetLastError());
        qCWarning(lcPlatform).noquote() << why;
        return core::fail(core::ErrorCode::SystemRefused, why);
    }
    return {};
}

SilentLoaderErrors::SilentLoaderErrors()
{
    DWORD previous = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previous);
    m_previous = previous;
}

SilentLoaderErrors::~SilentLoaderErrors()
{
    SetThreadErrorMode(m_previous, nullptr);
}

void quietChildProcess(QProcess& process)
{
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
}

void endQuietlyOnCrash()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(&endQuietly);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG // the debug C runtime's reports: printed, not a dialog (release builds have none)
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
}

QString programFileName(const QString& base)
{
    return base + u".exe"_s;
}

core::Result<void> showInFileManager(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    if (!QProcess::startDetached(u"explorer.exe"_s, {u"/select,"_s + native})) {
        return core::fail(core::ErrorCode::SystemRefused, u"Could not open Explorer for %1"_s.arg(native));
    }
    return {};
}

void reportLeaksAtExit()
{
#ifdef _DEBUG
    // Report leaks to the debugger output at exit during development.
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
}

} // namespace gigchain::platform
