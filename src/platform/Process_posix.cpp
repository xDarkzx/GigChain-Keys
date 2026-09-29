#include "gigchain/platform/Process.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#include <csignal>
#include <initializer_list>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace gigchain::platform {
namespace {

// A signal handler may only call async-signal-safe functions: _exit is one.
void endNow(int signal)
{
    _exit(128 + signal);
}

} // namespace

core::Result<void> hardenLibrarySearch()
{
    return {}; // plugins load by full path; "only from the plugin folders" is shared code
}

SilentLoaderErrors::SilentLoaderErrors() = default;
SilentLoaderErrors::~SilentLoaderErrors() = default;

void quietChildProcess(QProcess& /*process*/) {}

void endQuietlyOnCrash()
{
    struct sigaction action{};
    action.sa_handler = &endNow;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(signal, &action, nullptr);
}

QString programFileName(const QString& base)
{
    return base;
}

core::Result<void> showInFileManager(const QString& path)
{
    // The folder, in the desktop's own file manager (xdg-open; Finder on macOS).
    const QString folder = QFileInfo(path).isDir() && !path.endsWith(u".vst3"_s) ? path : QFileInfo(path).absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
        return core::fail(core::ErrorCode::SystemRefused, u"Could not open the file manager at %1"_s.arg(folder));
    }
    return {};
}

void reportLeaksAtExit() {}

} // namespace gigchain::platform
