#pragma once

#include "gigchain/engine/MidiControl.h"

#include <QString>

#include <vector>

namespace gigchain::core {
struct Setlist;
} // namespace gigchain::core

namespace gigchain::ui {

// A control learned on a keyboard (a knob, fader, button, pad or pedal), and
// what it does, as "Learn this keyboard" asks for it.
struct TeachableControl
{
    engine::MidiTrigger learned;
    QString label; // "Next song", "Cutoff (2 songs)", "Looper: Record"
};

// What a Settings button does (engine::ControlAction), as Settings shows it.
[[nodiscard]] QString controlActionLabel(int action);

// Every control learned, once each: Settings' buttons (`buttons`), then the
// setlist's (the mixer's faders, the looper's buttons and knob, each knob
// learned for a plugin in any song). A knob learned for any channel is
// listed as channel 1.
[[nodiscard]] std::vector<TeachableControl> controlsInUse(const core::Setlist& setlist, const engine::ControlTriggers& buttons);

// Teaching another keyboard the controls, one by one: the control moved on
// it stands for the one asked (a RigEntry).
class KeyboardTeaching
{
public:
    explicit KeyboardTeaching(std::vector<TeachableControl> controls);

    // The control asked for now (nullptr: done).
    [[nodiscard]] const TeachableControl* current() const;
    [[nodiscard]] bool done() const { return m_at >= m_controls.size(); }
    [[nodiscard]] std::size_t at() const { return m_at; }
    [[nodiscard]] std::size_t count() const { return m_controls.size(); }

    // `moved` on the keyboard taught stands for the current control, and it
    // goes on to the next. False (nothing taken): it is the one just taken
    // (still being moved), or already stands for another control.
    bool take(engine::MidiTrigger moved);
    // The current control left as it is (this keyboard has none for it).
    void skip();
    // Back to the control before, to teach it again.
    void back();

    // What was taught (a control that sends just as learned needs nothing).
    [[nodiscard]] engine::KeyboardRig rig() const;

private:
    std::vector<TeachableControl> m_controls;
    std::vector<engine::MidiTrigger> m_taught; // per control: what stands for it (unset: skipped / not yet)
    std::size_t m_at = 0;
    engine::MidiTrigger m_last; // the control taken last (still being moved)
};

} // namespace gigchain::ui
