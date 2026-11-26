#include "gigchain/platform/KeepAwake.h"

#include "PlatformLog.h"

#include <windows.h>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

namespace {

// The display on and the system awake, until told otherwise (ES_CONTINUOUS).
constexpr EXECUTION_STATE kAwake = ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED;

} // namespace

struct KeepAwake::System
{
    bool held = false;
};

KeepAwake::KeepAwake() : m_system(std::make_unique<System>()) {}

KeepAwake::~KeepAwake()
{
    if (m_system->held && SetThreadExecutionState(ES_CONTINUOUS) == 0)
        qCWarning(lcPlatform) << "Could not let the screen sleep again: SetThreadExecutionState failed";
}

core::Result<void> KeepAwake::hold(bool on, const QString& reason)
{
    Q_UNUSED(reason); // (Windows keeps no reason for a thread's request)
    if (on == m_system->held) return {};
    // It returns the state before, 0 when it failed (no error code is set).
    if (SetThreadExecutionState(on ? kAwake : ES_CONTINUOUS) == 0)
        return core::fail(core::ErrorCode::SystemRefused,
                          on ? u"Windows did not keep the screen on: SetThreadExecutionState failed"_s
                             : u"Windows did not let the screen sleep again: SetThreadExecutionState failed"_s);
    m_system->held = on;
    return {};
}

bool KeepAwake::held() const { return m_system->held; }

} // namespace gigchain::platform
