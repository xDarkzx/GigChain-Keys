#pragma once

#include "pluginterfaces/gui/iplugview.h"

#include <memory>

namespace gigchain::engine {

// The host side of a plugin editor's event handling where the system needs
// one: on Linux a plugin asks its editor's IPlugFrame for the host's
// Steinberg::Linux::IRunLoop and runs its timers and its file handlers (its
// X11 connection) through it; Windows plugins run their own. Everything runs
// on the main thread, through Qt's own timers and socket notifiers.
//
// Owned by the editor (not reference counted by the plugin). Every handler
// and timer a plugin registers is held until it unregisters it, and all are
// let go by clear() when its editor closes, whatever the plugin forgot.
class Vst3RunLoop
{
public:
    virtual ~Vst3RunLoop() = default;
    Vst3RunLoop(const Vst3RunLoop&) = delete;
    Vst3RunLoop& operator=(const Vst3RunLoop&) = delete;
    Vst3RunLoop(Vst3RunLoop&&) = delete;
    Vst3RunLoop& operator=(Vst3RunLoop&&) = delete;

    // What the editor hands a plugin asking for Linux::IRunLoop.
    [[nodiscard]] virtual Steinberg::Linux::IRunLoop* runLoop() = 0;
    // The editor's queryInterface: true (and `object` set) when the plugin
    // asks for the run loop. (Its interface id exists only where it is used.)
    [[nodiscard]] virtual bool answers(const Steinberg::TUID requested, void** object) = 0;
    // Stops and lets go of every handler and timer (the editor closed).
    virtual void clear() = 0;

protected:
    Vst3RunLoop() = default;
};

// This system's run loop for one editor; nullptr where plugins need none
// (Vst3RunLoop_<system>.cpp).
[[nodiscard]] std::unique_ptr<Vst3RunLoop> makeVst3RunLoop();

} // namespace gigchain::engine
