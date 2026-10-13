#include "KeyboardTeaching.h"

#include "gigchain/core/Model.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <map>
#include <set>

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

// A control learned in the setlist (its channel 1-16) as the engine sees it.
engine::MidiTrigger triggerOf(const core::LearnedControl& c)
{
    if (!c.isSet()) return {};
    return engine::MidiTrigger{.kind = static_cast<engine::MidiTrigger::Kind>(c.kind),
                               .channel = static_cast<uint8_t>(std::clamp(c.channel, 1, 16) - 1),
                               .number = static_cast<uint8_t>(std::clamp(c.number, 0, 127))};
}

QString tr(const char* text)
{
    return QCoreApplication::translate("KeyboardTeaching", text);
}

} // namespace

QString controlActionLabel(int action)
{
    static constexpr std::array<const char*, engine::kControlActionCount> kLabels{
        QT_TRANSLATE_NOOP("SettingsController", "Next song"),
        QT_TRANSLATE_NOOP("SettingsController", "Previous song"),
        QT_TRANSLATE_NOOP("SettingsController", "Next sound"),
        QT_TRANSLATE_NOOP("SettingsController", "Previous sound"),
        QT_TRANSLATE_NOOP("SettingsController", "Panic (stop all sound)"),
        QT_TRANSLATE_NOOP("SettingsController", "Tap tempo"),
        QT_TRANSLATE_NOOP("SettingsController", "Song / backing track: play / stop"),
        QT_TRANSLATE_NOOP("SettingsController", "Next part of the song (on the next bar)"),
        QT_TRANSLATE_NOOP("SettingsController", "Repeat this part once more"),
        QT_TRANSLATE_NOOP("SettingsController", "Hold this part (loops until pressed again)"),
        QT_TRANSLATE_NOOP("SettingsController", "Play the song"),
        QT_TRANSLATE_NOOP("SettingsController", "Stop the song"),
        QT_TRANSLATE_NOOP("SettingsController", "Previous part of the song (on the next bar)"),
        QT_TRANSLATE_NOOP("SettingsController", "Click on / off")};
    if (action < 0 || action >= engine::kControlActionCount) return {};
    return QCoreApplication::translate("SettingsController", kLabels.at(static_cast<std::size_t>(action)));
}

std::vector<TeachableControl> controlsInUse(const core::Setlist& setlist, const engine::ControlTriggers& buttons)
{
    std::vector<TeachableControl> controls;
    const auto add = [&controls](engine::MidiTrigger trigger, const QString& label) {
        if (!trigger.isSet()) return;
        const auto known = std::ranges::find_if(controls, [&](const TeachableControl& c) { return c.learned == trigger; });
        if (known != controls.end()) {
            known->label += u", "_s + label; // one control, two jobs
            return;
        }
        controls.push_back(TeachableControl{.learned = trigger, .label = label});
    };

    for (int action = 0; action < engine::kControlActionCount; ++action) {
        add(buttons.at(static_cast<std::size_t>(action)), controlActionLabel(action));
    }

    const core::MixerControls& mixer = setlist.mixerControls;
    add(triggerOf(mixer.master), tr("Master fader"));
    for (std::size_t strip = 0; strip < mixer.volume.size(); ++strip) {
        add(triggerOf(mixer.volume.at(strip)), tr("Fader of mixer strip %1").arg(strip + 1));
        add(triggerOf(mixer.pan.at(strip)), tr("Pan of mixer strip %1").arg(strip + 1));
    }

    static constexpr std::array<const char*, core::LoopControls::ButtonCount> kLoopButtons{
        QT_TRANSLATE_NOOP("KeyboardTeaching", "Looper: Record"),        QT_TRANSLATE_NOOP("KeyboardTeaching", "Looper: Play / stop"),
        QT_TRANSLATE_NOOP("KeyboardTeaching", "Looper: Undo"),          QT_TRANSLATE_NOOP("KeyboardTeaching", "Looper: Stop all"),
        QT_TRANSLATE_NOOP("KeyboardTeaching", "Looper: Next channel"), QT_TRANSLATE_NOOP("KeyboardTeaching", "Looper: Previous channel")};
    for (std::size_t b = 0; b < kLoopButtons.size(); ++b) add(triggerOf(setlist.loopControls.buttons.at(b)), tr(kLoopButtons.at(b)));
    add(triggerOf(setlist.loopControls.selector), tr("Instrument knob"));

    // The knobs learned for plugins: one per controller, named by what it
    // turns and in how many songs.
    struct Knob
    {
        std::set<QString> parameters;
        std::set<const core::Song*> songs;
    };
    std::map<uint32_t, Knob> knobs; // by the trigger, packed (in the order learned below)
    std::vector<uint32_t> order;
    for (const core::Song& song : setlist.songs) {
        for (const core::Patch& patch : song.patches) {
            for (const core::Channel& channel : patch.channels) {
                for (const core::ControlMapping& m : channel.mappings) {
                    const engine::MidiTrigger trigger{.kind = engine::MidiTrigger::ControlChange,
                                                      .channel = static_cast<uint8_t>(std::clamp(m.midiChannel, 1, 16) - 1),
                                                      .number = static_cast<uint8_t>(std::clamp(m.controller, 0, 127))};
                    auto [knob, added] = knobs.try_emplace(trigger.pack());
                    if (added) order.push_back(trigger.pack());
                    if (!m.parameterName.isEmpty()) knob->second.parameters.insert(m.parameterName);
                    knob->second.songs.insert(&song);
                }
            }
        }
    }
    for (const uint32_t packed : order) {
        const Knob& knob = knobs.at(packed);
        QStringList names(knob.parameters.begin(), knob.parameters.end());
        if (names.size() > 2) names = names.mid(0, 2) << u"…"_s;
        const QString what = names.isEmpty() ? tr("Knob") : names.join(u", "_s);
        add(engine::MidiTrigger::unpack(packed), tr("%1 (%2 songs)").arg(what).arg(knob.songs.size()));
    }
    return controls;
}

KeyboardTeaching::KeyboardTeaching(std::vector<TeachableControl> controls)
    : m_controls(std::move(controls)), m_taught(m_controls.size())
{
}

const TeachableControl* KeyboardTeaching::current() const
{
    return done() ? nullptr : &m_controls.at(m_at);
}

bool KeyboardTeaching::take(engine::MidiTrigger moved)
{
    if (done() || !moved.isSet() || moved == m_last) return false;
    for (std::size_t i = 0; i < m_taught.size(); ++i) {
        if (i != m_at && m_taught.at(i) == moved) return false; // already another control's
    }
    m_taught.at(m_at) = moved;
    m_last = moved;
    ++m_at;
    return true;
}

void KeyboardTeaching::skip()
{
    if (done()) return;
    m_taught.at(m_at) = {};
    ++m_at;
}

void KeyboardTeaching::back()
{
    if (m_at == 0) return;
    --m_at;
    m_last = {}; // the control before may be taught as before
}

engine::KeyboardRig KeyboardTeaching::rig() const
{
    engine::KeyboardRig rig;
    for (std::size_t i = 0; i < m_controls.size(); ++i) {
        const engine::MidiTrigger& from = m_taught.at(i);
        const engine::MidiTrigger& to = m_controls.at(i).learned;
        if (from.isSet() && from != to) rig.push_back(engine::RigEntry{.from = from, .to = to});
    }
    return rig;
}

} // namespace gigchain::ui
