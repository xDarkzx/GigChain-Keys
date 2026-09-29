#pragma once

#include <QAbstractNativeEventFilter>
#include <QWindow>

#include <memory>

namespace gigchain::platform {

// The kind of native window a plugin's editor is embedded in on this system.
enum class NativeWindowKind
{
    Win32, // a Windows window handle
    X11,   // an X11 window id (Linux, also on Wayland through its X11 layer)
    Cocoa, // an NSView (macOS)
};

[[nodiscard]] NativeWindowKind nativeWindowKind();

// Puts `window` in front of every other window and makes it the active one
// (a second start handed its setlist over). Windows refuses a plain
// "activate" from an app that is not in front, so it is done its way there;
// elsewhere the window manager may decline, which is its rule, not an error.
void bringToFront(QWindow& window);

// Keeps a plugin's window (and its own windows inside it) from flickering
// while moved or sized, where the system needs it (Windows: they are told
// they are already erased); nullptr where nothing is needed. Install it with
// QCoreApplication::installNativeEventFilter while the plugin is shown.
[[nodiscard]] std::unique_ptr<QAbstractNativeEventFilter> makeNoFlickerFilter(WId pluginWindow);

} // namespace gigchain::platform
