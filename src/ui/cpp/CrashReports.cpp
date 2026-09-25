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
#include <cstdio>
#include <cstring>
#include <exception>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

constexpr int kKeepReports = 10; // dumps can be large: only the newest are kept
const QString kSeenFile = u"reported.txt"_s;

// Prepared at install(); read by the crash handler, which must not allocate.
wchar_t g_folder[MAX_PATH] = {};
wchar_t g_prefix[64] = {}; // the executable name, from branding
char g_lastAction[512] = "(nothing yet)";
wchar_t g_lastDump[MAX_PATH] = {};
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
std::terminate_handler g_previousTerminate = nullptr;
bool g_installed = false;

bool writeReport(EXCEPTION_POINTERS* exception, const char* reason)
{
    if (g_folder[0] == L'\0') return false;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t dumpPath[MAX_PATH];
    wchar_t notePath[MAX_PATH];
    const int written = swprintf_s(dumpPath, L"%s\\%s-%04u%02u%02u-%02u%02u%02u-%03u", g_folder, g_prefix, now.wYear,
                                   now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    if (written < 0) return false;
    wcscpy_s(notePath, dumpPath);
    if (wcscat_s(dumpPath, L".dmp") != 0 || wcscat_s(notePath, L".txt") != 0) return false;

    HANDLE dump = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump == INVALID_HANDLE_VALUE) return false;
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory
                                                 | MiniDumpWithUnloadedModules);
    const BOOL dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type,
                                          exception != nullptr ? &info : nullptr, nullptr, nullptr);
    CloseHandle(dump);

    char note[1024];
    const unsigned long code = exception != nullptr ? exception->ExceptionRecord->ExceptionCode : 0;
    const void* address = exception != nullptr ? exception->ExceptionRecord->ExceptionAddress : nullptr;
    const int length = std::snprintf(note, sizeof(note),
                                     "Reason: %s\r\nException code: 0x%08lX at %p\r\nLast action: %s\r\n"
                                     "Crash dump written: %s\r\n",
                                     reason, code, address, g_lastAction, dumped ? "yes" : "no");
    HANDLE text = CreateFileW(notePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (text != INVALID_HANDLE_VALUE) {
        DWORD out = 0;
        WriteFile(text, note, static_cast<DWORD>(std::max(length, 0)), &out, nullptr);
        CloseHandle(text);
    }
    wcscpy_s(g_lastDump, dumpPath);
    return dumped != FALSE;
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* exception)
{
    writeReport(exception, "the app crashed");
    // Let Windows (and a debugger, if attached) handle it as usual.
    return g_previousFilter != nullptr ? g_previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void onTerminate()
{
    writeReport(nullptr, "an error nothing caught (std::terminate)");
    if (g_previousTerminate != nullptr) g_previousTerminate();
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
    return QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
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
    native.toWCharArray(g_folder);
    g_folder[native.size()] = L'\0';
    const QString prefix = branding::executable().left(60);
    prefix.toWCharArray(g_prefix);
    g_prefix[prefix.size()] = L'\0';

    // Only the newest reports are kept.
    const QDir dir(folder);
    const QFileInfoList dumps = dir.entryInfoList({u"*.dmp"_s}, QDir::Files, QDir::Time);
    for (qsizetype i = kKeepReports; i < dumps.size(); ++i) {
        QFile::remove(dumps[i].filePath());
        QFile::remove(dumps[i].path() + u'/' + dumps[i].completeBaseName() + u".txt"_s);
    }

    if (!g_installed) {
        g_previousFilter = SetUnhandledExceptionFilter(onCrash);
        g_previousTerminate = std::set_terminate(onTerminate);
        g_installed = true;
    }
    qCInfo(lcUi).noquote() << "Crash reports go to" << native;
}

void CrashReports::uninstall()
{
    if (!g_installed) return;
    SetUnhandledExceptionFilter(g_previousFilter);
    std::set_terminate(g_previousTerminate);
    g_installed = false;
    g_folder[0] = L'\0';
}

void CrashReports::setLastAction(const QString& action)
{
    const QByteArray utf8 = action.toUtf8();
    const auto length = std::min<qsizetype>(utf8.size(), sizeof(g_lastAction) - 1);
    std::memcpy(g_lastAction, utf8.constData(), static_cast<std::size_t>(length));
    g_lastAction[length] = '\0';
}

QStringList CrashReports::takeNewReports()
{
    if (g_folder[0] == L'\0') return {};
    const QDir dir(QString::fromWCharArray(g_folder));
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
    return QDir::fromNativeSeparators(QString::fromWCharArray(g_lastDump));
}

} // namespace gigchain::ui
