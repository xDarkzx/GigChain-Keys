#include "gigchain/platform/Process.h"

#include <QProcess>

#include <csignal>
#include <initializer_list>
#include <unistd.h>

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

void reportLeaksAtExit() {}

} // namespace gigchain::platform
