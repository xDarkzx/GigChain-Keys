#include "gigchain/platform/CrashHandler.h"

#include "PlatformLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string_view>

#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace gigchain::platform {
namespace {

constexpr std::array kSignals{SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};

// Prepared at install; read by the signal handler, which may only use
// async-signal-safe calls (open, write, close, clock_gettime, raise,
// sigaction, backtrace_symbols_fd): fixed buffers, no allocation, no Qt.
struct CrashState
{
    std::array<char, 4096> pathStart{}; // "<folder>/<prefix>-"
    std::array<char, 512> lastAction{}; // empty: nothing marked yet
    std::array<char, 4096> lastReport{};
    std::array<char, 65536> altStack{}; // a stack overflow still gets a report
    std::array<struct sigaction, kSignals.size()> previous{};
    std::terminate_handler previousTerminate = nullptr;
    std::atomic<bool> writing{false};
    bool installed = false;
};
CrashState g_crash; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): read by the signal handler

// Appends `text` at `at` (never past the end, always terminated); the new end.
std::size_t append(std::array<char, 4096>& buffer, std::size_t at, const char* text)
{
    while (*text != '\0' && at + 1 < buffer.size()) buffer.at(at++) = *text++; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): a C string, in a signal handler
    buffer.at(at) = '\0';
    return at;
}

// `value` in decimal (a signal handler cannot use printf).
std::array<char, 24> decimal(unsigned long long value)
{
    std::array<char, 24> digits{};
    std::array<char, 24> out{};
    std::size_t count = 0;
    do {
        digits.at(count++) = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < digits.size());
    for (std::size_t i = 0; i < count; ++i) out.at(i) = digits.at(count - 1 - i);
    return out;
}

std::array<char, 24> hex(unsigned long long value)
{
    std::array<char, 24> out{'0', 'x'};
    constexpr std::string_view kDigits = "0123456789abcdef";
    for (std::size_t i = 0; i < 16; ++i) out.at(2 + i) = kDigits.at((value >> ((15 - i) * 4)) & 0xF);
    return out;
}

void put(int fd, const char* text)
{
    // In a crash nothing more can be done if the disk refuses: the rest of
    // the note is still tried.
    if (write(fd, text, std::strlen(text)) < 0) return;
}

// Writes one report; false when it could not be created. `signal` 0 and
// `address` null: written on request, not by a crash.
bool writeReport(const char* reason, int signal, const void* address)
{
    if (g_crash.pathStart.front() == '\0') return false;
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    std::array<char, 4096> path{};
    std::size_t at = append(path, 0, g_crash.pathStart.data());
    const unsigned long long ms =
        static_cast<unsigned long long>(now.tv_sec) * 1000ULL + static_cast<unsigned long long>(now.tv_nsec) / 1000000ULL;
    at = append(path, at, decimal(ms).data());
    append(path, at, ".crash");

    const int fd = open(path.data(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); // NOLINT(cppcoreguidelines-pro-type-vararg): open's mode
    if (fd < 0) return false;
    put(fd, "Reason: ");
    put(fd, reason);
    put(fd, "\n");
    if (signal != 0) {
        put(fd, "Signal: ");
        put(fd, decimal(static_cast<unsigned long long>(signal)).data());
        put(fd, " at ");
        put(fd, hex(reinterpret_cast<unsigned long long>(address)).data()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): an address, printed
        put(fd, "\n");
    }
    put(fd, "Last action: ");
    put(fd, g_crash.lastAction.front() != '\0' ? g_crash.lastAction.data() : "(nothing yet)");
    put(fd, "\nBacktrace:\n");
    std::array<void*, 64> frames{};
    const int count = backtrace(frames.data(), static_cast<int>(frames.size()));
    backtrace_symbols_fd(frames.data(), count, fd);
    close(fd);
    std::memcpy(g_crash.lastReport.data(), path.data(), path.size());
    return true;
}

void onSignal(int signal, siginfo_t* info, void* /*context*/)
{
    // A second fault while writing (or another thread crashing at once):
    // straight to the end.
    if (!g_crash.writing.exchange(true)) writeReport("the app crashed", signal, info != nullptr ? info->si_addr : nullptr);
    // The system's own ending (a crash exit, a core dump where enabled).
    struct sigaction standard{};
    standard.sa_handler = SIG_DFL;
    sigemptyset(&standard.sa_mask);
    sigaction(signal, &standard, nullptr);
    raise(signal);
}

[[noreturn]] void onTerminate()
{
    if (!g_crash.writing.exchange(true)) writeReport("an error nothing caught (std::terminate)", 0, nullptr);
    if (g_crash.previousTerminate != nullptr) g_crash.previousTerminate();
    std::abort();
}

} // namespace

bool installCrashHandler(const QString& folder, const QString& prefix)
{
    const QByteArray start = QFile::encodeName(QDir(folder).absolutePath() + u'/' + prefix + u'-');
    if (start.size() >= static_cast<qsizetype>(g_crash.pathStart.size()) - 64) {
        qCWarning(lcPlatform).noquote() << "No crash reports: the folder path is too long:" << folder;
        return false;
    }
    std::memcpy(g_crash.pathStart.data(), start.constData(), static_cast<std::size_t>(start.size()));
    g_crash.pathStart.at(static_cast<std::size_t>(start.size())) = '\0';

    if (!g_crash.installed) {
        // backtrace() loads its unwinder on first use (which allocates): now,
        // not in the handler.
        std::array<void*, 4> warmUp{};
        (void)backtrace(warmUp.data(), static_cast<int>(warmUp.size()));

        stack_t stack{};
        stack.ss_sp = g_crash.altStack.data();
        stack.ss_size = g_crash.altStack.size();
        if (sigaltstack(&stack, nullptr) != 0) {
            qCWarning(lcPlatform) << "Crash reports: no separate stack for them (sigaltstack failed); a stack overflow gets none";
        }
        struct sigaction action{};
        action.sa_sigaction = &onSignal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        for (std::size_t i = 0; i < kSignals.size(); ++i) sigaction(kSignals.at(i), &action, &g_crash.previous.at(i));
        g_crash.previousTerminate = std::set_terminate(onTerminate);
        g_crash.writing = false;
        g_crash.installed = true;
    }
    return true;
}

void uninstallCrashHandler()
{
    if (!g_crash.installed) return;
    for (std::size_t i = 0; i < kSignals.size(); ++i) sigaction(kSignals.at(i), &g_crash.previous.at(i), nullptr);
    std::set_terminate(g_crash.previousTerminate);
    g_crash.installed = false;
    g_crash.pathStart.front() = '\0';
}

QString crashFolder()
{
    if (g_crash.pathStart.front() == '\0') return {};
    return QFileInfo(QFile::decodeName(g_crash.pathStart.data())).path();
}

void setCrashContext(const QByteArray& utf8)
{
    const auto length = std::min<qsizetype>(utf8.size(), static_cast<qsizetype>(g_crash.lastAction.size()) - 1);
    std::memcpy(g_crash.lastAction.data(), utf8.constData(), static_cast<std::size_t>(length));
    g_crash.lastAction.at(static_cast<std::size_t>(length)) = '\0';
}

core::Result<QString> writeCrashReportNow(const char* reason)
{
    if (g_crash.pathStart.front() == '\0') {
        return core::fail(core::ErrorCode::FileWriteFailed, u"no crash report folder"_s);
    }
    if (!writeReport(reason, 0, nullptr)) {
        return core::fail(core::ErrorCode::FileWriteFailed, u"could not create the report: %1"_s.arg(QString::fromLocal8Bit(std::strerror(errno))));
    }
    return QFile::decodeName(g_crash.lastReport.data());
}

QString crashReportPattern()
{
    return u"*.crash"_s;
}

QString crashNoteOf(const QString& report)
{
    return report; // the report is its own note
}

} // namespace gigchain::platform
