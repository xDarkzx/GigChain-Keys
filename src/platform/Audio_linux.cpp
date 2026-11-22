#include "gigchain/platform/Audio.h"

#include "PlatformLog.h"

#include <QCoreApplication>

#include <sys/resource.h>

#include <cerrno>
#include <cstring>
#include <limits>

namespace gigchain::platform {

QString systemAudioName()
{
    return QCoreApplication::translate("Settings", "PulseAudio");
}

core::Result<std::optional<int>> realtimePriorityLimit()
{
    rlimit limit{};
    if (getrlimit(RLIMIT_RTPRIO, &limit) != 0) {
        const int error = errno;
        const QString problem = QStringLiteral("The realtime priority limit (RLIMIT_RTPRIO) could not be read: %1")
                                    .arg(QString::fromLocal8Bit(std::strerror(error)));
        qCWarning(lcPlatform).noquote() << problem;
        return core::fail(core::ErrorCode::SystemRefused, problem);
    }
    // (The soft limit: what the process may use now.)
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur > static_cast<rlim_t>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(limit.rlim_cur);
}

} // namespace gigchain::platform
