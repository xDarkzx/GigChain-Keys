#include "gigchain/platform/Windows.h"

namespace gigchain::platform {

NativeWindowKind nativeWindowKind()
{
#ifdef Q_OS_MACOS
    return NativeWindowKind::Cocoa;
#else
    return NativeWindowKind::X11;
#endif
}

void prepareGuiPlatform()
{
#ifndef Q_OS_MACOS
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
}

void bringToFront(QWindow& window)
{
    window.raise();
    window.requestActivate();
}

std::unique_ptr<QAbstractNativeEventFilter> makeNoFlickerFilter(WId /*pluginWindow*/)
{
    return nullptr; // plugin windows do not flicker here
}

} // namespace gigchain::platform
