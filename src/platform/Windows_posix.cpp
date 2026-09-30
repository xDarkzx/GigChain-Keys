#include "gigchain/platform/Windows.h"

#include <QGuiApplication>

namespace gigchain::platform {

NativeWindowKind nativeWindowKind()
{
    // What Qt runs on, not what was hoped for: an off-screen (or Wayland)
    // window is no NSView or X11 window, and a plugin must never get one.
#ifdef Q_OS_MACOS
    return QGuiApplication::platformName() == u"cocoa" ? NativeWindowKind::Cocoa : NativeWindowKind::None;
#else
    return QGuiApplication::platformName() == u"xcb" ? NativeWindowKind::X11 : NativeWindowKind::None;
#endif
}

double pluginPixelRatio(const QWindow& window)
{
#ifdef Q_OS_MACOS
    Q_UNUSED(window);
    return 1.0;
#else
    return window.devicePixelRatio();
#endif
}

bool pluginsTakeContentScale()
{
#ifdef Q_OS_MACOS
    return false;
#else
    return true;
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
