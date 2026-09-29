#pragma once

#include <QString>

#include <memory>

namespace gigchain::platform {

// Who is the first start of the app, held until it ends. Windows: a named
// mutex in this session (the installer looks for it to ask for the app to be
// closed). Elsewhere: a lock file in the user's runtime folder; a lock left
// by an app that crashed is taken over.
class InstanceLock
{
public:
    InstanceLock();
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
    InstanceLock(InstanceLock&&) = delete;
    InstanceLock& operator=(InstanceLock&&) = delete;

    // True: this start is the first (also when it cannot tell: logged, and
    // starting beats not starting). Once.
    [[nodiscard]] bool acquire(const QString& name);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// The local socket a later start hands its setlist to, reachable only by this
// user. Windows: a pipe named "<name>-<user>" (pipe names are machine-wide).
// Elsewhere: a socket file in the user's own runtime folder.
[[nodiscard]] QString instanceSocketName(const QString& name);

} // namespace gigchain::platform
