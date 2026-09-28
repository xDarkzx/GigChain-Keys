#include "CrashReports.h"

#include "gigchain/core/Branding.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

constexpr int kKeepReports = 10; // dumps can be large: only the newest are kept
const QString kSeenFile = u"reported.txt"_s;

// Prepared at install(); read by the crash handler, which must not allocate:
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

QStringList readSeen(const QDir& folder)
{
    QFile file(folder.filePath(kSeenFile));
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcUi).noquote() << "Could not read" << file.fileName() << ":" << file.errorString();
        return {};
    }
    // A list of report names: far below this unless damaged (then the tail is ignored).
    constexpr qint64 kMaxSeenBytes = 1024LL * 1024;
    return QString::fromUtf8(file.read(kMaxSeenBytes)).split(u'\n', Qt::SkipEmptyParts);
}

} // namespace

void CrashReports::install(const QString& folder)
{
    if (!QDir().mkpath(folder)) {
        qCWarning(lcUi).noquote() << "No crash reports: could not create" << folder;
        return;
    }
    const QString native = QDir::toNativeSeparators(QDir(folder).absolutePath());
    if (native.size() >= MAX_PATH - 64) {
        qCWarning(lcUi).noquote() << "No crash reports: the folder path is too long:" << native;
        return;
    }
    native.toWCharArray(g_crash.folder.data());
    g_crash.folder.at(static_cast<std::size_t>(native.size())) = L'\0';
    const QString prefix = branding::executable().left(static_cast<qsizetype>(g_crash.prefix.size()) - 4);
    prefix.toWCharArray(g_crash.prefix.data());
    g_crash.prefix.at(static_cast<std::size_t>(prefix.size())) = L'\0';

    // Only the newest reports are kept.
    const QDir dir(folder);
    const QFileInfoList dumps = dir.entryInfoList({u"*.dmp"_s}, QDir::Files, QDir::Time);
    for (qsizetype i = kKeepReports; i < dumps.size(); ++i) {
        const QFileInfo& old = dumps.at(i);
        QFile::remove(old.filePath());
        QFile::remove(old.path() + u'/' + old.completeBaseName() + u".txt"_s);
    }

    if (!g_crash.installed) {
        g_crash.previousFilter = SetUnhandledExceptionFilter(onCrash);
        g_crash.previousTerminate = std::set_terminate(onTerminate);
        g_crash.installed = true;
    }
    qCInfo(lcUi).noquote() << "Crash reports go to" << native;
}

void CrashReports::uninstall()
{
    if (!g_crash.installed) return;
    SetUnhandledExceptionFilter(g_crash.previousFilter);
    std::set_terminate(g_crash.previousTerminate);
    g_crash.installed = false;
    g_crash.folder.front() = L'\0';
}

void CrashReports::setLastAction(const QString& action)
{
    const QByteArray utf8 = action.toUtf8();
    const auto length = std::min<qsizetype>(utf8.size(), static_cast<qsizetype>(g_crash.lastAction.size()) - 1);
    std::memcpy(g_crash.lastAction.data(), utf8.constData(), static_cast<std::size_t>(length));
    g_crash.lastAction.at(static_cast<std::size_t>(length)) = '\0';
}

QStringList CrashReports::takeNewReports()
{
    if (g_crash.folder.front() == L'\0') return {};
    const QDir dir(QString::fromWCharArray(g_crash.folder.data()));
    QStringList seen = readSeen(dir);
    QStringList fresh;
    for (const QFileInfo& dump : dir.entryInfoList({u"*.dmp"_s}, QDir::Files, QDir::Time | QDir::Reversed)) {
        if (seen.contains(dump.fileName())) continue;
        fresh << dump.filePath();
        seen << dump.fileName();
    }
    if (!fresh.isEmpty()) {
        seen = seen.mid(std::max<qsizetype>(0, seen.size() - 100)); // a short memory is enough
        QSaveFile out(dir.filePath(kSeenFile));
        if (!out.open(QIODevice::WriteOnly) || out.write(seen.join(u'\n').toUtf8()) < 0 || !out.commit()) {
            qCWarning(lcUi).noquote() << "Could not save" << out.fileName() << ":" << out.errorString();
        }
    }
    return fresh;
}

QString CrashReports::writeNow(const char* reason)
{
    if (!writeReport(nullptr, reason)) {
        qCWarning(lcUi) << "Could not write a crash report:" << GetLastError();
        return {};
    }
    return QDir::fromNativeSeparators(QString::fromWCharArray(g_crash.lastDump.data()));
}

} // namespace gigchain::ui
