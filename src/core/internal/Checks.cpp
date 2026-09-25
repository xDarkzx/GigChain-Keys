#include "gigchain/core/Checks.h"

#include <QLoggingCategory>

#include <atomic>

Q_LOGGING_CATEGORY(lcChecks, "gigchain.checks")

namespace gigchain::core::checks {
namespace {

std::atomic<bool> g_fatal{true};

} // namespace

bool failed(const char* condition, const char* file, int line)
{
    qCCritical(lcChecks).noquote() << "Check failed:" << condition << "at" << file << ':' << line;
#ifdef QT_DEBUG
    if (g_fatal.load(std::memory_order_relaxed)) {
        qFatal("Check failed: %s at %s:%d", condition, file, line); // stop where it went wrong
    }
#endif
    return true;
}

void setFatal(bool fatal)
{
    g_fatal.store(fatal, std::memory_order_relaxed);
}

} // namespace gigchain::core::checks
