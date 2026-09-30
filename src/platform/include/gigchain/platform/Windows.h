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
    None,  // the app runs where plugins have no window (Linux on Wayland, off-screen)
};

// The kind the app's own windows really are: on Linux X11 only when Qt runs
// its X11 platform (xcb); a player who chose Wayland gets None, and plugin
// editors are refused with the way out, never handed a window they cannot use.
[[nodiscard]] NativeWindowKind nativeWindowKind();

// Before the GUI starts: the window system plugin windows need. Linux
// plugins draw into X11 windows, so the app runs Qt's X11 platform (xcb, also
// on Wayland desktops through their X11 layer, as Reaper and Bitwig do) unless
// QT_QPA_PLATFORM says otherwise. Nothing to do on Windows.
void prepareGuiPlatform();

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
