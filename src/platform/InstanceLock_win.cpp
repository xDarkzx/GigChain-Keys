#include "gigchain/platform/InstanceLock.h"

#include "PlatformLog.h"

#include <windows.h>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

struct InstanceLock::Impl
{
    HANDLE mutex = nullptr; // owned while this is the first start
};

InstanceLock::InstanceLock() : m_impl(std::make_unique<Impl>()) {}

InstanceLock::~InstanceLock()
{
    if (m_impl->mutex != nullptr) CloseHandle(m_impl->mutex);
}

bool InstanceLock::acquire(const QString& name)
{
    // "Local\": this Windows session (another user signed in has their own).
    const std::wstring wide = (u"Local\\"_s + name).toStdWString();
    HANDLE mutex = CreateMutexW(nullptr, FALSE, wide.c_str());
    const DWORD error = GetLastError();
    if (mutex == nullptr) {
        // Cannot tell: start as the first rather than not at all.
        qCWarning(lcPlatform).noquote() << "Could not check for a running app (CreateMutex failed, error" << error << ")";
        return true;
    }
    if (error == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return false;
    }
    m_impl->mutex = mutex;
    return true;
}

QString instanceSocketName(const QString& name)
{
    // Pipe names are machine-wide: each Windows user has an app of their own.
    return name + u'-' + qEnvironmentVariable("USERNAME");
}

core::Result<void> checkLocalSocketName(const QString& /*path*/)
{
    return {}; // pipe names have no such limit
}

} // namespace gigchain::platform
