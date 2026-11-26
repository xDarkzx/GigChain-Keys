#pragma once

#include "gigchain/core/Error.h"

#include <QString>

#include <memory>

namespace gigchain::platform {

// Keeps the screen on and the computer from going to sleep on its own while
// the app plays (#92). A MIDI keyboard is no activity to the system: only a
// typing keyboard and a mouse are, so in the middle of a set the screen
// would go dark and lock. The way each system offers:
//   Windows  SetThreadExecutionState (display and system required): the
//            display is not turned off, so the lock that comes with it does
//            not either; a screen saver set in Windows still starts (it is
//            off unless the player turned it on: the help says so)
//   macOS    an IOKit power assertion (no display sleep while it is held)
//   Linux    org.freedesktop.ScreenSaver.Inhibit on the session bus (blanking
//            and lock: GNOME, KDE and most desktops) and logind's "idle"
//            inhibitor on the system bus (automatic suspend)
// What it holds is let go when it is destroyed, and by the system when the
// app ends, even by a crash. Use it from the app's main thread (Windows ties
// the request to the thread that made it).
class KeepAwake
{
public:
    KeepAwake();
    ~KeepAwake();
    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;
    KeepAwake(KeepAwake&&) = delete;
    KeepAwake& operator=(KeepAwake&&) = delete;

    // Holds (true) or lets go (false); asking for what is already so does
    // nothing. Holding fails only when nothing could be held, with each
    // reason (on Linux one lock of the two is enough: a desktop without a
    // screen saver service still has logind; the one missing is logged).
    // `reason` is what the system shows for it ("Playing live").
    core::Result<void> hold(bool on, const QString& reason);
    [[nodiscard]] bool held() const;

private:
    struct System;
    std::unique_ptr<System> m_system;
};

} // namespace gigchain::platform
