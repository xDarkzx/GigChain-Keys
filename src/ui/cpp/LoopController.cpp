#include "LoopController.h"

#include "DocumentController.h"

#include "gigchain/engine/IEngine.h"

#include <QLoggingCategory>
#include <QSettings>

#include <algorithm>

using namespace Qt::StringLiterals;

Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace gigchain::ui {
namespace {

const QString kStripKey = u"loops/stripVisible"_s;
const QString kTempoKey = u"loops/tempoFromFirstLoop"_s;
// Knob moves looked at before deciding what kind of knob it is.
constexpr std::size_t kKnobMovesToLearn = 4;

engine::MidiTrigger toTrigger(const core::LearnedControl& c)
{
    if (!c.isSet()) return {};
    return engine::MidiTrigger{.kind = static_cast<engine::MidiTrigger::Kind>(c.kind),
                               .channel = static_cast<uint8_t>(c.channel - 1),
                               .number = static_cast<uint8_t>(c.number)};
}

core::LearnedControl fromTrigger(const engine::MidiTrigger& t)
{
    if (!t.isSet()) return {};
    return core::LearnedControl{.kind = static_cast<int>(t.kind), .channel = t.channel + 1, .number = t.number};
}

// What kind of knob sends these values when turned a little: an endless
// encoder sends small steps (1-7 up, 121-127 down, or 64 +- a few); a knob
// with a range sends where it is.
core::LoopControls::KnobMode knobMode(const std::vector<int>& values)
{
    const auto twos = [](int v) { return (v >= 1 && v <= 7) || (v >= 121 && v <= 127); };
    const auto offset = [](int v) { return v >= 57 && v <= 71 && v != 64; };
    if (std::ranges::all_of(values, twos)) return core::LoopControls::Relative;
    if (std::ranges::all_of(values, offset)) return core::LoopControls::RelativeOffset;
    return core::LoopControls::Absolute;
}

} // namespace

QString loopStateName(engine::LoopState state)
{
    using S = engine::LoopState;
    switch (state) {
    case S::Empty: return u"empty"_s;
    case S::Armed: return u"armed"_s;
    case S::Recording: return u"recording"_s;
    case S::Closing: return u"closing"_s;
    case S::Playing: return u"playing"_s;
    case S::OverdubArmed: return u"overdubArmed"_s;
    case S::Overdubbing: return u"overdubbing"_s;
    case S::Stopped: return u"stopped"_s;
    case S::StartArmed: return u"startArmed"_s;
    case S::StopArmed: return u"stopArmed"_s;
    }
    return u"empty"_s;
}

LoopController::LoopController(engine::IEngine& engine, DocumentController& document, QSettings& settings, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document), m_settings(settings)
{
    m_engine.setTempoFromFirstLoop(tempoFromFirstLoop());
    // Another setlist, an undo: its controls.
    connect(&m_document, &DocumentController::loopControlsChanged, this, &LoopController::controlsChanged);
    connect(&m_document, &DocumentController::structureChanged, this, &LoopController::controlsChanged);
    connect(&m_document, &DocumentController::channelsChanged, this, &LoopController::loopsChanged);
}

std::optional<core::ChannelId> LoopController::channelId(int channel) const
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || channel < 0 || std::cmp_greater_equal(channel, patch->channels.size())) return std::nullopt;
    return patch->channels.at(static_cast<std::size_t>(channel)).id;
}

int LoopController::channelCount() const
{
    const core::Patch* patch = m_document.currentPatch();
    return patch != nullptr ? static_cast<int>(patch->channels.size()) : 0;
}

int LoopController::actingChannel() const
{
    const int selected = m_document.selectedChannel();
    return selected >= 0 ? selected : (channelCount() > 0 ? 0 : -1);
}

QVariantList LoopController::channelLoops() const
{
    QVariantList list;
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr) return list;
    for (const core::Channel& channel : patch->channels) {
        const auto it = std::ranges::find_if(m_loops, [&channel](const engine::ChannelLoop& l) { return l.channel == channel.id; });
        const engine::ChannelLoop loop = it != m_loops.end() ? *it : engine::ChannelLoop{};
        list << QVariantMap{{u"state"_s, loopStateName(loop.state)},
                            {u"progress"_s, loop.progress},
                            {u"bar"_s, loop.bar},
                            {u"bars"_s, loop.bars},
                            {u"layers"_s, loop.layers},
                            {u"beatsToGo"_s, loop.beatsToGo}};
    }
    return list;
}

QVariantList LoopController::allLoops() const
{
    // The loops' channels by name, wherever they are in the setlist.
    const auto nameOf = [this](const core::ChannelId& id) {
        for (const core::Song& song : m_document.setlist().songs) {
            for (const core::Patch& patch : song.patches) {
                const auto it = std::ranges::find_if(patch.channels, [&id](const core::Channel& c) { return c.id == id; });
                if (it != patch.channels.end()) return it->name;
            }
        }
        return tr("(a removed channel)");
    };
    QVariantList list;
    for (const engine::ChannelLoop& loop : m_loops) {
        list << QVariantMap{{u"id"_s, loop.channel.value()},
                            {u"name"_s, nameOf(loop.channel)},
                            {u"state"_s, loopStateName(loop.state)},
                            {u"progress"_s, loop.progress},
                            {u"bar"_s, loop.bar},
                            {u"bars"_s, loop.bars}};
    }
    return list;
}

int LoopController::playingCount() const
{
    using S = engine::LoopState;
    return static_cast<int>(std::ranges::count_if(m_loops, [](const engine::ChannelLoop& l) {
        return l.state == S::Playing || l.state == S::OverdubArmed || l.state == S::Overdubbing || l.state == S::StopArmed;
    }));
}

bool LoopController::recording() const
{
    using S = engine::LoopState;
    return std::ranges::any_of(m_loops, [](const engine::ChannelLoop& l) {
        return l.state == S::Recording || l.state == S::Closing || l.state == S::Overdubbing;
    });
}

bool LoopController::stripVisible() const
{
    return m_settings.value(kStripKey, true).toBool();
}

void LoopController::setStripVisible(bool visible)
{
    if (visible == stripVisible()) return;
    m_settings.setValue(kStripKey, visible);
    emit settingsChanged();
}

bool LoopController::tempoFromFirstLoop() const
{
    return m_settings.value(kTempoKey, false).toBool();
}

void LoopController::setTempoFromFirstLoop(bool take)
{
    if (take == tempoFromFirstLoop()) return;
    m_settings.setValue(kTempoKey, take);
    m_engine.setTempoFromFirstLoop(take);
    emit settingsChanged();
}

QVariantList LoopController::controls() const
{
    static constexpr std::array<const char*, engine::kLoopButtonCount> kLabels{
        QT_TR_NOOP("Record"), QT_TR_NOOP("Loop play / stop"), QT_TR_NOOP("Undo layer"),
        QT_TR_NOOP("Stop all loops"), QT_TR_NOOP("Next instrument"), QT_TR_NOOP("Previous instrument")};
    QVariantList list;
    const core::LoopControls& controls = m_document.loopControls();
    for (std::size_t i = 0; i < kLabels.size(); ++i) {
        const engine::MidiTrigger trigger = toTrigger(controls.buttons.at(i));
        list << QVariantMap{{u"button"_s, static_cast<int>(i)},
                            {u"label"_s, tr(kLabels.at(i))},
                            {u"trigger"_s, trigger.isSet() ? trigger.describe() : QString()}};
    }
    return list;
}

QString LoopController::selectorText() const
{
    const core::LearnedControl& knob = m_document.loopControls().selector;
    if (!knob.isSet()) return {};
    return tr("Knob CC %1 (channel %2)").arg(knob.number).arg(knob.channel);
}

int LoopController::selectorMode() const
{
    return m_document.loopControls().selectorMode;
}

void LoopController::setSelectorMode(int mode)
{
    core::LoopControls setup = m_document.loopControls();
    if (setup.selectorMode == mode) return;
    setup.selectorMode = mode;
    m_document.setLoopControls(setup); // reports a bad value
}

void LoopController::send(int channel, engine::LoopCommand what)
{
    const auto id = channelId(channel);
    if (!id) {
        qCWarning(lcUi) << "Loop button for channel" << channel + 1 << "ignored: the patch has" << channelCount() << "channels";
        return;
    }
    m_engine.loopCommand(*id, what);
    poll(); // show it at once
}

void LoopController::record(int channel) { send(channel, engine::LoopCommand::Record); }
void LoopController::playStop(int channel) { send(channel, engine::LoopCommand::PlayStop); }
void LoopController::undo(int channel) { send(channel, engine::LoopCommand::Undo); }
void LoopController::clear(int channel) { send(channel, engine::LoopCommand::Clear); }

void LoopController::playStopLoop(const QString& id)
{
    m_engine.loopCommand(core::ChannelId(id), engine::LoopCommand::PlayStop);
    poll();
}

void LoopController::stopAll()
{
    m_engine.stopAllLoops();
    poll();
}

void LoopController::clearAll()
{
    m_engine.clearAllLoops();
    poll();
}

void LoopController::select(int channel)
{
    const int count = channelCount();
    if (count == 0) return;
    m_document.setSelectedChannel(std::clamp(channel, 0, count - 1));
}

void LoopController::act(engine::LoopAction action)
{
    using A = engine::LoopAction;
    const int channel = actingChannel();
    if (action != A::StopAll && action != A::NextChannel && action != A::PreviousChannel && channel >= 0 &&
        m_document.selectedChannel() < 0) {
        select(channel); // show which channel the keyboard acts on
    }
    switch (action) {
    case A::Record: record(channel); break;
    case A::PlayStop: playStop(channel); break;
    case A::Undo: undo(channel); break;
    case A::Clear: clear(channel); break;
    case A::StopAll: stopAll(); break;
    case A::NextChannel: select(m_document.selectedChannel() + 1); break;
    case A::PreviousChannel: select(std::max(m_document.selectedChannel(), 1) - 1); break;
    }
}

void LoopController::poll()
{
    // The keyboard's looper buttons and instrument knob.
    if (m_learning < 0) {
        for (const engine::LoopAction action : m_engine.takeLoopActions()) act(action);
        const engine::SelectorMove move = m_engine.takeSelectorMove();
        if (const int count = channelCount(); count > 0) {
            if (move.value >= 0) select(std::min(count - 1, move.value * count / 128));
            if (move.steps != 0) select(std::max(m_document.selectedChannel(), 0) + move.steps);
        }
    } else {
        pollLearning();
    }

    std::vector<engine::ChannelLoop> loops = m_engine.loops();
    if (loops != m_loops) {
        m_loops = std::move(loops);
        emit loopsChanged();
    }
}

void LoopController::setLearning(int what)
{
    if (m_learning == what) return;
    m_learning = what;
    emit learningChanged();
}

void LoopController::learn(int what)
{
    if (what < 0 || what > kLearnSelector) {
        qCWarning(lcUi) << "Learn looper control" << what << "ignored: there is no such control";
        return;
    }
    // Only what is pressed or turned from now on counts.
    (void)m_engine.takeLearnedTrigger();
    while (m_engine.takeControllerMove()) {
    }
    m_knob.reset();
    m_knobValues.clear();
    setLearning(what);
}

void LoopController::cancelLearning()
{
    setLearning(-1);
}

void LoopController::forget(int what)
{
    core::LoopControls setup = m_document.loopControls();
    if (what == kLearnSelector) setup.selector = {};
    else if (what >= 0 && what < engine::kLoopButtonCount) setup.buttons.at(static_cast<std::size_t>(what)) = {};
    else return;
    m_document.setLoopControls(setup);
}

void LoopController::pollLearning()
{
    core::LoopControls setup = m_document.loopControls();
    if (m_learning == kLearnSelector) {
        // Turn the knob a little: which one, then what kind it is.
        while (const auto move = m_engine.takeControllerMove()) {
            const std::pair knob{move->at(0), move->at(1)};
            if (m_knob != knob) {
                m_knob = knob;
                m_knobValues.clear();
            }
            m_knobValues.push_back(move->at(2));
        }
        if (!m_knob || m_knobValues.size() < kKnobMovesToLearn) return;
        setup.selector = core::LearnedControl{.kind = 0xB0, .channel = m_knob->first, .number = m_knob->second};
        setup.selectorMode = knobMode(m_knobValues);
        qCInfo(lcUi) << "Instrument knob learned: CC" << m_knob->second << "channel" << m_knob->first << "mode"
                     << setup.selectorMode;
    } else {
        const engine::MidiTrigger pressed = m_engine.takeLearnedTrigger();
        if (!pressed.isSet()) return;
        const core::LearnedControl learned = fromTrigger(pressed);
        // One button, one job: it leaves any other looper button.
        std::ranges::for_each(setup.buttons, [&learned](core::LearnedControl& button) {
            if (button == learned) button = {};
        });
        setup.buttons.at(static_cast<std::size_t>(m_learning)) = learned;
        qCInfo(lcUi).noquote() << "Looper button learned:" << pressed.describe();
    }
    setLearning(-1);
    m_document.setLoopControls(setup);
}

} // namespace gigchain::ui
