#pragma once

#include "gigchain/core/Error.h"

#include <functional>
#include <memory>

namespace gigchain::engine {
class IEngine;
}
namespace gigchain::platform {
class KeepAwake;
}

namespace gigchain::ui {

// Keeps the screen on, and the computer awake, while the sound runs (#92),
// unless switched off in Settings > Audio. A MIDI keyboard is no activity to
// the system (only a typing keyboard and a mouse are), so in the middle of a
// set the screen would go dark and lock; arpeggiators and held pads play for
// minutes with no key pressed at all, so it follows the sound, not the keys.
// Follows the engine's poll: the audio going (the beat clock runs) holds,
// stopped (a device lost, the app between devices) lets go. A refusal is
// logged with why, once: asked again when the sound starts again.
class StayAwake
{
public:
    // What asks the system: on or off (platform::KeepAwake when none is given).
    using Ask = std::function<core::Result<void>(bool on)>;

    explicit StayAwake(engine::IEngine& engine, Ask ask = {}, bool enabled = true);
    ~StayAwake();
    StayAwake(const StayAwake&) = delete;
    StayAwake& operator=(const StayAwake&) = delete;
    StayAwake(StayAwake&&) = delete;
    StayAwake& operator=(StayAwake&&) = delete;

    // Settings > Audio's switch: taken at once.
    void setEnabled(bool on);
    [[nodiscard]] bool enabled() const { return m_enabled; }
    // Called as the engine is polled.
    void poll();
    [[nodiscard]] bool holding() const { return m_holding; }

private:
    // Holds while switched on and the sound runs; lets go otherwise.
    void follow();

    engine::IEngine& m_engine;
    std::unique_ptr<platform::KeepAwake> m_system; // when no Ask was given
    Ask m_ask;
    bool m_enabled = true;
    bool m_holding = false;
    bool m_refused = false; // the system said no: not asked again until the sound stops
};

} // namespace gigchain::ui
