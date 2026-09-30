#include "gigchain/platform/InstanceLock.h"

#include "PlatformLog.h"

#include <QDir>
#include <QLockFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <sys/un.h>

#include <cerrno>
#include <csignal>

using namespace Qt::StringLiterals;

namespace gigchain::platform {
namespace {

// The user's own runtime folder, only this user can enter it. Linux:
// XDG_RUNTIME_DIR (Qt makes a private one where there is none). The Mac:
// its per-user temporary folder ($TMPDIR, 0700, short): Qt's runtime
// location there is ~/Library/Application Support, long and shared with
// settings, and socket paths are limited to 104 bytes.
QString runtimeFolder()
{
#ifdef Q_OS_MACOS
    return QDir::tempPath();
#else
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
}

// Whether the process that wrote a lock is gone (it crashed or was killed).
bool ownerIsGone(const QLockFile& lock)
{
    qint64 pid = 0;
    QString host;
    QString app;
    if (!lock.getLockInfo(&pid, &host, &app) || pid <= 0) return false;
    if (host != QSysInfo::machineHostName()) return false; // another machine's: cannot tell
    return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH;
}

} // namespace

struct InstanceLock::Impl
{
    std::unique_ptr<QLockFile> lock; // held while this is the first start
};

InstanceLock::InstanceLock() : m_impl(std::make_unique<Impl>()) {}

InstanceLock::~InstanceLock() = default;

bool InstanceLock::acquire(const QString& name)
{
    const QString folder = runtimeFolder();
    if (folder.isEmpty() || !QDir().mkpath(folder)) {
        qCWarning(lcPlatform).noquote() << "Could not check for a running app: no runtime folder (" << folder << ")";
        return true; // cannot tell: start as the first rather than not at all
    }
    auto lock = std::make_unique<QLockFile>(folder + u'/' + name + u".lock"_s);
    lock->setStaleLockTime(0); // stale only when its app is gone (below), never by age
    if (lock->tryLock(0)) {
        m_impl->lock = std::move(lock);
        return true;
    }
    if (lock->error() == QLockFile::LockFailedError && ownerIsGone(*lock)) {
        qCInfo(lcPlatform) << "The last run left its lock (it crashed or was killed): taken over";
        if (lock->removeStaleLockFile() && lock->tryLock(0)) {
            m_impl->lock = std::move(lock);
            return true;
        }
    }
    if (lock->error() == QLockFile::LockFailedError) return false; // another start is running
    qCWarning(lcPlatform).noquote() << "Could not check for a running app: the lock file" << lock->fileName()
                                    << "could not be created (error" << lock->error() << ")";
    return true;
}

QString instanceSocketName(const QString& name)
{
    return runtimeFolder() + u'/' + name + u".sock"_s;
}

core::Result<void> checkLocalSocketName(const QString& path)
{
    const qsizetype bytes = path.toUtf8().size();
    const auto most = static_cast<qsizetype>(sizeof(sockaddr_un{}.sun_path)) - 1; // (its ending zero)
    if (bytes > most) {
        return core::fail(core::ErrorCode::InvalidData,
                          u"The app's socket path is too long for this system (%1 bytes, at most %2): %3"_s.arg(bytes).arg(most).arg(path));
    }
    return {};
}

} // namespace gigchain::platform
