#include "PanelController.h"

#include "DocumentController.h"

#include "gigchain/engine/IEngine.h"

#include <QLoggingCategory>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace gigchain::ui {
namespace {

// A control learned on the keyboard, as the engine reports it.
engine::MidiTrigger triggerOf(const core::LearnedControl& c)
{
    if (!c.isSet()) return {};
    return engine::MidiTrigger{.kind = static_cast<engine::MidiTrigger::Kind>(c.kind),
                               .channel = static_cast<uint8_t>(c.channel - 1),
                               .number = static_cast<uint8_t>(c.number)};
}

// A control in a few letters, for under a panel button: "PAD C1" (a note
// on channel 10, where pads usually are), "KEY C4", "CC 20", "PROG 5".
QString shortName(const engine::MidiTrigger& t)
{
    static constexpr std::array<const char*, 12> kNotes{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    switch (t.kind) {
    case engine::MidiTrigger::Note:
        return u"%1 %2%3"_s.arg(t.channel == 9 ? u"PAD"_s : u"KEY"_s, QLatin1StringView(kNotes.at(t.number % 12)))
            .arg((t.number / 12) - 1);
    case engine::MidiTrigger::ControlChange: return u"CC %1"_s.arg(t.number);
    case engine::MidiTrigger::ProgramChange: return u"PROG %1"_s.arg(t.number + 1);
    case engine::MidiTrigger::None: break;
    }
    return {};
}

} // namespace

PanelController::PanelController(engine::IEngine& engine, DocumentController& document, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document)
{
    for (const auto signal : {&DocumentController::currentChanged, &DocumentController::structureChanged,
                              &DocumentController::channelsChanged, &DocumentController::panelControlsChanged,
                              &DocumentController::hasSetlistChanged}) {
        connect(&m_document, signal, this, &PanelController::panelChanged);
    }
    connect(&m_document, &DocumentController::channelUpdated, this, &PanelController::panelChanged);
}

QVariantList PanelController::sounds() const
{
    QVariantList list;
    const core::Cursor at = m_document.cursor();
    const auto& songs = m_document.setlist().songs;
    if (!m_document.hasSetlist() || at.song < 0 || std::cmp_greater_equal(at.song, songs.size())) return list;
    const core::Song& song = songs.at(static_cast<std::size_t>(at.song));
    const core::PanelControls& panel = m_document.panelControls();
    for (std::size_t i = 0; i < song.patches.size(); ++i) {
        const engine::MidiTrigger assigned = i < panel.sounds.size() ? triggerOf(panel.sounds.at(i)) : engine::MidiTrigger{};
        list << QVariantMap{{u"name"_s, song.patches.at(i).name},
                            {u"active"_s, std::cmp_equal(i, at.patch)},
                            {u"assignable"_s, i < panel.sounds.size()},
                            {u"assigned"_s, assigned.isSet() ? assigned.describe() : QString()},
                            {u"pad"_s, shortName(assigned)}};
    }
    return list;
}

QVariantList PanelController::parts() const
{
    QVariantList list;
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr) return list;
    const core::PanelControls& panel = m_document.panelControls();
    for (std::size_t i = 0; i < patch->channels.size(); ++i) {
        const core::Channel& channel = patch->channels.at(i);
        const engine::MidiTrigger assigned = i < panel.parts.size() ? triggerOf(panel.parts.at(i)) : engine::MidiTrigger{};
        list << QVariantMap{{u"name"_s, channel.name},
                            {u"on"_s, !channel.mute},
                            {u"keyLow"_s, channel.keyLow},
                            {u"keyHigh"_s, channel.keyHigh},
                            {u"assignable"_s, i < panel.parts.size()},
                            {u"assigned"_s, assigned.isSet() ? assigned.describe() : QString()},
                            {u"pad"_s, shortName(assigned)}};
    }
    return list;
}

void PanelController::setAssigning(bool on)
{
    if (m_assigning == on) return;
    m_assigning = on;
    m_armedIndex = -1;
    emit assigningChanged();
}

QVariantMap PanelController::armed() const
{
    return {{u"kind"_s, m_armedKind}, {u"index"_s, m_armedIndex}};
}

void PanelController::pickSound(int index)
{
    const core::Cursor at = m_document.cursor();
    (void)m_document.selectPatch(at.song, index); // a failure is reported and logged
}

int PanelController::addSound(bool copyPlaying)
{
    const core::Cursor at = m_document.cursor();
    const auto& songs = m_document.setlist().songs;
    if (at.song < 0 || std::cmp_greater_equal(at.song, songs.size())) {
        m_document.reportMessage(tr("Add a song first: its sounds go in it"), Notifications::Warning);
        return -1;
    }
    if (copyPlaying && at.patch >= 0) {
        if (!m_document.duplicatePatch(at.song, at.patch)) return -1; // reported
        // The copy goes after the others, as a new sound does.
        const auto last = static_cast<int>(m_document.setlist().songs.at(static_cast<std::size_t>(at.song)).patches.size()) - 1;
        const int copy = m_document.cursor().patch;
        if (copy != last && !m_document.movePatch(at.song, copy, last)) return -1;
        (void)m_document.selectPatch(at.song, last);
        return last;
    }
    if (!m_document.addPatch(at.song)) return -1; // reported
    return m_document.cursor().patch;
}

bool PanelController::renameSound(int index, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false; // a blank name: the old one stays
    return m_document.renamePatch(m_document.cursor().song, index, trimmed);
}

bool PanelController::duplicateSound(int index)
{
    return m_document.duplicatePatch(m_document.cursor().song, index);
}

bool PanelController::deleteSound(int index)
{
    const core::Cursor at = m_document.cursor();
    const auto& songs = m_document.setlist().songs;
    if (at.song < 0 || std::cmp_greater_equal(at.song, songs.size())) return false;
    if (songs.at(static_cast<std::size_t>(at.song)).patches.size() <= 1) {
        m_document.reportMessage(tr("A song keeps at least one sound"), Notifications::Warning);
        return false;
    }
    return m_document.removePatch(at.song, index);
}

void PanelController::togglePart(int index)
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || index < 0 || std::cmp_greater_equal(index, patch->channels.size())) {
        qCWarning(lcUi) << "Part" << index + 1 << "ignored: the sound playing has" << (patch != nullptr ? patch->channels.size() : 0) << "parts";
        return;
    }
    (void)m_document.setChannelMute(index, !patch->channels.at(static_cast<std::size_t>(index)).mute);
}

bool PanelController::hasSlot(int kind, int index)
{
    const std::size_t count = kind == PartButton ? core::PanelControls::kParts : core::PanelControls::kSounds;
    return (kind == SoundButton || kind == PartButton) && index >= 0 && std::cmp_less(index, count);
}

bool PanelController::setSlot(core::PanelControls& controls, int kind, int index, const core::LearnedControl& value)
{
    if (!hasSlot(kind, index)) return false;
    (kind == PartButton ? controls.parts : controls.sounds).at(static_cast<std::size_t>(index)) = value;
    return true;
}

void PanelController::arm(int kind, int index)
{
    if (!hasSlot(kind, index)) {
        qCWarning(lcUi) << "Assign ignored: the panel has no button" << kind << index;
        return;
    }
    const bool again = m_armedKind == kind && m_armedIndex == index;
    m_assigning = true;
    m_armedKind = kind;
    m_armedIndex = again ? -1 : index; // tapped again: not any more
    (void)m_engine.takeLearnedTrigger(); // only a press from now on counts
    emit assigningChanged();
}

void PanelController::clearAssignment(int kind, int index)
{
    core::PanelControls controls = m_document.panelControls();
    if (!hasSlot(kind, index)) return;
    const auto& buttons = kind == PartButton ? controls.parts : controls.sounds;
    if (!buttons.at(static_cast<std::size_t>(index)).isSet()) return;
    (void)setSlot(controls, kind, index, {});
    (void)m_document.setPanelControls(controls); // a failure is reported and logged
}

void PanelController::startZoneLearning(int part)
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || part < 0 || std::cmp_greater_equal(part, patch->channels.size())) {
        qCWarning(lcUi) << "Set by playing ignored: the sound playing has no part" << part + 1;
        return;
    }
    setAssigning(false); // one thing learned at a time
    m_zonePart = part;
    m_zoneFirst = -1;
    (void)m_engine.takeLearnedTrigger(); // only a press from now on counts
    emit zoneLearningChanged();
}

void PanelController::cancelZoneLearning()
{
    if (m_zonePart < 0) return;
    m_zonePart = -1;
    m_zoneFirst = -1;
    emit zoneLearningChanged();
}

bool PanelController::othersHear(int part) const
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || part < 0 || std::cmp_greater_equal(part, patch->channels.size())) return false;
    const int channel = patch->channels.at(static_cast<std::size_t>(part)).midiChannel;
    if (channel == 0) return false; // it listens to everything itself
    for (std::size_t i = 0; i < patch->channels.size(); ++i) {
        if (std::cmp_not_equal(i, part) && patch->channels.at(i).midiChannel == 0) return true;
    }
    return false;
}

QVariantMap PanelController::zoneLearning() const
{
    return {{u"active"_s, m_zonePart >= 0},
            {u"part"_s, m_zonePart},
            {u"step"_s, m_zoneFirst < 0 ? 0 : 1},
            {u"othersHearIt"_s, othersHear(m_lastZonePart)}};
}

void PanelController::keepOthersOnTheKeys(int part, int keysChannel)
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || keysChannel < 1 || keysChannel > 16) return;
    for (std::size_t i = 0; i < patch->channels.size(); ++i) {
        if (std::cmp_equal(i, part) || patch->channels.at(i).midiChannel != 0) continue;
        (void)m_document.setChannelMidiChannel(static_cast<int>(i), keysChannel); // a failure is reported and logged
    }
    emit zoneLearningChanged();
}

void PanelController::takeZonePress(const engine::MidiTrigger& pressed)
{
    if (pressed.kind != engine::MidiTrigger::Note) return; // keys and pads only
    if (m_zoneFirst < 0) {
        m_zoneFirst = pressed.number;
        m_zoneChannel = pressed.channel + 1;
        emit zoneLearningChanged();
        return;
    }
    const int part = m_zonePart;
    const int low = std::min<int>(m_zoneFirst, pressed.number);
    const int high = std::max<int>(m_zoneFirst, pressed.number);
    m_zonePart = -1;
    m_zoneFirst = -1;
    m_lastZonePart = part;
    if (m_document.setChannelKeyRange(part, low, high)) {
        (void)m_document.setChannelMidiChannel(part, m_zoneChannel);
        qCInfo(lcUi) << "Part" << part + 1 << "set by playing: keys" << low << "to" << high << "on channel" << m_zoneChannel;
    }
    emit zoneLearningChanged();
}

void PanelController::poll()
{
    if (m_zonePart >= 0) {
        if (const engine::MidiTrigger pressed = m_engine.takeLearnedTrigger(); pressed.isSet()) takeZonePress(pressed);
    }
    if (m_assigning && m_armedIndex >= 0) {
        if (const engine::MidiTrigger pressed = m_engine.takeLearnedTrigger(); pressed.isSet()) {
            const core::LearnedControl learned{.kind = pressed.kind, .channel = pressed.channel + 1, .number = pressed.number};
            core::PanelControls controls = m_document.panelControls();
            // One pad, one job: taken from any other button that had it.
            // (replace_if, not replace: MSVC's vectorised replace refuses a 12-byte element under clang.)
            for (auto* buttons : {&controls.sounds, &controls.parts}) {
                std::ranges::replace_if(*buttons, [&learned](const core::LearnedControl& b) { return b == learned; }, core::LearnedControl{});
            }
            (void)setSlot(controls, m_armedKind, m_armedIndex, learned);
            if (m_document.setPanelControls(controls)) {
                qCInfo(lcUi).noquote() << "Assigned" << pressed.describe() << "to the panel's" << (m_armedKind == PartButton ? "part" : "sound")
                                       << m_armedIndex + 1;
            }
            m_armedIndex = -1; // done: tap the next
            emit assigningChanged();
        }
    }
    for (const engine::PanelPress& pressed : m_engine.takePanelPresses()) press(pressed);
}

void PanelController::press(const engine::PanelPress& pressed)
{
    if (pressed.kind == engine::PanelPress::Part) {
        togglePart(pressed.index);
        return;
    }
    const core::Cursor at = m_document.cursor();
    const auto& songs = m_document.setlist().songs;
    const std::size_t count = at.song >= 0 && std::cmp_less(at.song, songs.size()) ? songs.at(static_cast<std::size_t>(at.song)).patches.size() : 0;
    if (std::cmp_greater_equal(pressed.index, count)) {
        m_document.reportMessage(tr("Sound %1: this song has %2 sounds").arg(pressed.index + 1).arg(count), Notifications::Warning);
        return;
    }
    pickSound(pressed.index);
}

} // namespace gigchain::ui
