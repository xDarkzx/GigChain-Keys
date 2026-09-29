#include "gigchain/platform/CrashHandler.h"

#include "PlatformLog.h"

#include <QDir>

#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>

using namespace Qt::StringLiterals;

namespace gigchain::platform {
namespace {

// Prepared at install; read by the crash handler, which must not allocate:
// fixed buffers, and one global, as the handler is a plain C callback with
// nowhere else to look.
struct CrashState
{
    std::array<wchar_t, MAX_PATH> folder{};
    std::array<wchar_t, 64> prefix{}; // the executable name, from branding
    std::array<char, 512> lastAction{}; // empty: nothing marked yet
    std::array<wchar_t, MAX_PATH> lastDump{};
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
    std::terminate_handler previousTerminate = nullptr;
    bool installed = false;
};
CrashState g_crash; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): read by the crash handler, a C callback

bool writeReport(EXCEPTION_POINTERS* exception, const char* reason)
{
    if (g_crash.folder.front() == L'\0') return false;
    SYSTEMTIME now;
    GetLocalTime(&now);
    std::array<wchar_t, MAX_PATH> dumpPath{};
    std::array<wchar_t, MAX_PATH> notePath{};
    // The C formatters: they do not allocate, which a crash handler must not.
    const int written = swprintf_s(dumpPath.data(), dumpPath.size(), // NOLINT(cppcoreguidelines-pro-type-vararg)
                                   L"%s\\%s-%04u%02u%02u-%02u%02u%02u-%03u", g_crash.folder.data(), g_crash.prefix.data(),
                                   now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    if (written < 0) return false;
    wcscpy_s(notePath.data(), notePath.size(), dumpPath.data());
    if (wcscat_s(dumpPath.data(), dumpPath.size(), L".dmp") != 0 || wcscat_s(notePath.data(), notePath.size(), L".txt") != 0) {
        return false;
    }

    HANDLE dump = CreateFileW(dumpPath.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump == INVALID_HANDLE_VALUE) return false;
    MINIDUMP_EXCEPTION_INFORMATION info{.ThreadId = GetCurrentThreadId(), .ExceptionPointers = exception, .ClientPointers = FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory
                                                 | MiniDumpWithUnloadedModules);
    const BOOL dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type,
                                          exception != nullptr ? &info : nullptr, nullptr, nullptr);
    CloseHandle(dump);

    std::array<char, 1024> note{};
    const unsigned long code = exception != nullptr ? exception->ExceptionRecord->ExceptionCode : 0;
    const void* address = exception != nullptr ? exception->ExceptionRecord->ExceptionAddress : nullptr;
    const char* lastAction = g_crash.lastAction.front() != '\0' ? g_crash.lastAction.data() : "(nothing yet)";
    const int length = std::snprintf(note.data(), note.size(), // NOLINT(cppcoreguidelines-pro-type-vararg): no allocation here
                                     "Reason: %s\r\nException code: 0x%08lX at %p\r\nLast action: %s\r\n"
                                     "Crash dump written: %s\r\n",
                                     reason, code, address, lastAction, dumped ? "yes" : "no");
    HANDLE text = CreateFileW(notePath.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (text != INVALID_HANDLE_VALUE) {
        DWORD out = 0;
        WriteFile(text, note.data(), static_cast<DWORD>(std::clamp(length, 0, static_cast<int>(note.size()) - 1)), &out, nullptr);
        CloseHandle(text);
    }
    wcscpy_s(g_crash.lastDump.data(), g_crash.lastDump.size(), dumpPath.data());
    return dumped != FALSE;
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* exception)
{
    writeReport(exception, "the app crashed");
    // Let Windows (and a debugger, if attached) handle it as usual.
    return g_crash.previousFilter != nullptr ? g_crash.previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void onTerminate()
{
    writeReport(nullptr, "an error nothing caught (std::terminate)");
    if (g_crash.previousTerminate != nullptr) g_crash.previousTerminate();
    std::abort();
}

} // namespace

bool installCrashHandler(const QString& folder, const QString& prefix)
{
    const QString native = QDir::toNativeSeparators(QDir(folder).absolutePath());
    if (native.size() >= MAX_PATH - 64) {
        qCWarning(lcPlatform).noquote() << "No crash reports: the folder path is too long:" << native;
        return false;
    }
    native.toWCharArray(g_crash.folder.data());
    g_crash.folder.at(static_cast<std::size_t>(native.size())) = L'\0';
    const QString shortPrefix = prefix.left(static_cast<qsizetype>(g_crash.prefix.size()) - 4);
    shortPrefix.toWCharArray(g_crash.prefix.data());
    g_crash.prefix.at(static_cast<std::size_t>(shortPrefix.size())) = L'\0';

    if (!g_crash.installed) {
        g_crash.previousFilter = SetUnhandledExceptionFilter(onCrash);
        g_crash.previousTerminate = std::set_terminate(onTerminate);
        g_crash.installed = true;
    }
    return true;
}

void uninstallCrashHandler()
{
    if (!g_crash.installed) return;
    SetUnhandledExceptionFilter(g_crash.previousFilter);
    std::set_terminate(g_crash.previousTerminate);
    g_crash.installed = false;
    g_crash.folder.front() = L'\0';
}

QString crashFolder()
{
    return g_crash.folder.front() == L'\0' ? QString() : QDir::fromNativeSeparators(QString::fromWCharArray(g_crash.folder.data()));
}

void setCrashContext(const QByteArray& utf8)
{
    const auto length = std::min<qsizetype>(utf8.size(), static_cast<qsizetype>(g_crash.lastAction.size()) - 1);
    std::memcpy(g_crash.lastAction.data(), utf8.constData(), static_cast<std::size_t>(length));
    g_crash.lastAction.at(static_cast<std::size_t>(length)) = '\0';
}

core::Result<QString> writeCrashReportNow(const char* reason)
{
    if (!writeReport(nullptr, reason)) {
        return core::fail(core::ErrorCode::FileWriteFailed,
                          g_crash.folder.front() == L'\0' ? u"no crash report folder"_s
                                                          : u"Windows error %1"_s.arg(GetLastError()));
    }
    return QDir::fromNativeSeparators(QString::fromWCharArray(g_crash.lastDump.data()));
}

QString crashReportPattern()
{
    return u"*.dmp"_s;
}

QString crashNoteOf(const QString& report)
{
    const qsizetype dot = report.lastIndexOf(u'.');
    return (dot > 0 ? report.left(dot) : report) + u".txt"_s;
}

} // namespace gigchain::platform
