#include "gigchain/platform/KeepAwake.h"

#include "PlatformLog.h"

#include <IOKit/pwr_mgt/IOPMLib.h>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

struct KeepAwake::System
{
    // The display kept on (so the Mac does not sleep on its own either).
    IOPMAssertionID assertion = kIOPMNullAssertionID;
};

KeepAwake::KeepAwake() : m_system(std::make_unique<System>()) {}

KeepAwake::~KeepAwake()
{
    if (m_system->assertion == kIOPMNullAssertionID) return;
    if (const IOReturn result = IOPMAssertionRelease(m_system->assertion); result != kIOReturnSuccess)
        qCWarning(lcPlatform).noquote() << u"Could not let the screen sleep again: IOPMAssertionRelease error 0x%1"_s
                                               .arg(static_cast<unsigned int>(result), 0, 16);
}

core::Result<void> KeepAwake::hold(bool on, const QString& reason)
{
    if (on == held()) return {};
    if (on) {
        const CFStringRef name = reason.toCFString();
        IOPMAssertionID assertion = kIOPMNullAssertionID;
        const IOReturn result = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep,
                                                            kIOPMAssertionLevelOn, name, &assertion);
        CFRelease(name);
        if (result != kIOReturnSuccess)
            return core::fail(core::ErrorCode::SystemRefused,
                              u"macOS did not keep the screen on: IOPMAssertionCreateWithName error 0x%1"_s
                                  .arg(static_cast<unsigned int>(result), 0, 16));
        m_system->assertion = assertion;
        return {};
    }
    const IOReturn result = IOPMAssertionRelease(m_system->assertion);
    // Kept when the release did not get through (tried again, and on the
    // way out); gone when it did, or when macOS no longer knows it.
    if (result == kIOReturnSuccess || result == kIOReturnNotFound || result == kIOReturnBadArgument)
        m_system->assertion = kIOPMNullAssertionID;
    if (result != kIOReturnSuccess)
        return core::fail(core::ErrorCode::SystemRefused,
                          u"macOS did not let the screen sleep again: IOPMAssertionRelease error 0x%1"_s
                              .arg(static_cast<unsigned int>(result), 0, 16));
    return {};
}

bool KeepAwake::held() const { return m_system->assertion != kIOPMNullAssertionID; }

} // namespace gigchain::platform
