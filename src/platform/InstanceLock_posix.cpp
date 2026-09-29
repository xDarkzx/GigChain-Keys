#include "gigchain/platform/InstanceLock.h"

#include "PlatformLog.h"

#include <QDir>
#include <QLockFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <cerrno>
#include <csignal>

using namespace Qt::StringLiterals;

namespace gigchain::platform {
namespace {

// The user's own runtime folder (XDG_RUNTIME_DIR: only this user can enter
// it; Qt makes one of its own, likewise private, where there is none).
QString runtimeFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
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

} // namespace gigchain::platform
