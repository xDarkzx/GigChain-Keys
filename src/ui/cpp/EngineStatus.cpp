#include "EngineStatus.h"

#include "DocumentController.h"
#include "FreezeWatchdog.h"

#include "gigchain/engine/IEngine.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <utility>

#include <windows.h>
#include <psapi.h>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

Notifications::Level toLevel(engine::Notice::Level level)
{
    switch (level) {
    case engine::Notice::Level::Info: return Notifications::Info;
    case engine::Notice::Level::Warning: return Notifications::Warning;
    case engine::Notice::Level::Error: return Notifications::Error;
    }
    return Notifications::Error; // a level added later is shown as the worst until mapped
}

} // namespace

EngineStatus::EngineStatus(engine::IEngine& engine, DocumentController& document, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document), m_statusText(engine.statusText())
{
    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &EngineStatus::poll);
    m_timer.start();
}

double EngineStatus::masterVolumeDb() const
{
    return m_engine.masterVolume();
}

void EngineStatus::setMasterVolumeDb(double volumeDb)
{
    const double before = m_engine.masterVolume();
    m_engine.setMasterVolume(volumeDb);
    if (m_engine.masterVolume() != before) emit masterVolumeDbChanged();
}

bool EngineStatus::masterMuted() const
{
    return m_engine.masterMuted();
}

void EngineStatus::setMasterMuted(bool muted)
{
    if (m_engine.masterMuted() == muted) return;
    m_engine.setMasterMute(muted);
    emit masterMutedChanged();
}

void EngineStatus::panic()
{
    FreezeWatchdog::mark(u"panic"_s);
    m_engine.panic(); // logged by the engine
    m_document.reportMessage(tr("Panic: every sound stopped"), Notifications::Info);
}

void EngineStatus::playNote(int note, bool on)
{
    m_engine.injectNote(1, note, on ? 100 : 0);
}

QVariantList EngineStatus::keyVelocities() const
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(m_keyboard.velocity.size()));
    for (const uint8_t v : m_keyboard.velocity) list << int{v};
    return list;
}

void EngineStatus::setTempo(double bpm)
{
    m_engine.setTempo(bpm); // out-of-range values are refused and logged there
    pollTransport();
}

void EngineStatus::tapTempo()
{
    constexpr qint64 kRestartMs = 2000;
    constexpr std::size_t kTapsKept = 5; // four beats averaged
    if (!m_tapClock.isValid()) m_tapClock.start();
    const qint64 now = m_tapClock.elapsed();
    if (!m_taps.empty() && now - m_taps.back() > kRestartMs) m_taps.clear();
    m_taps.push_back(now);
    if (m_taps.size() > kTapsKept) m_taps.erase(m_taps.begin());
    if (m_taps.size() < 2) return;
    const double beatMs = static_cast<double>(m_taps.back() - m_taps.front()) / static_cast<double>(m_taps.size() - 1);
    if (beatMs <= 0.0) return;
    const double bpm = std::round(60000.0 / beatMs * 10.0) / 10.0;
    qCInfo(lcUi) << "Tap tempo:" << bpm << "BPM from" << m_taps.size() << "taps";
    setTempo(bpm);
}

void EngineStatus::setClickOn(bool on)
{
    if (on == m_clickOn) return;
    m_clickOn = on;
    m_engine.setClick(m_clickOn, m_clickVolumeDb);
    emit transportChanged();
}

void EngineStatus::setClickVolumeDb(double volumeDb)
{
    if (!std::isfinite(volumeDb)) return;
    m_clickVolumeDb = std::clamp(volumeDb, -60.0, 0.0);
    m_engine.setClick(m_clickOn, m_clickVolumeDb);
    emit transportChanged();
}

void EngineStatus::playPauseTrack()
{
    // A song with sections plays as a whole: its count, and the track with it.
    // (Following chords, there is no count: the pedal plays the track alone.)
    if (m_document.hasSections() && !m_engine.chordFollow().active) {
        if (m_engine.songPosition().playing) m_document.stopSong();
        else m_document.playSong();
        pollTransport();
        return;
    }
    m_track = m_engine.backingTrack(); // as it is now, not as the last poll saw it (a pedal can come first)
    if (!m_track.loaded) {
        m_document.reportMessage(m_track.loading ? tr("The backing track is still being read")
                                                 : tr("This song has no backing track"),
                                 Notifications::Info);
        return;
    }
    m_engine.playBackingTrack(!m_track.playing);
    pollTransport();
}

void EngineStatus::rewindTrack()
{
    m_engine.rewindBackingTrack();
    pollTransport();
}

void EngineStatus::pollTransport()
{
    const double bpm = m_engine.tempo();
    const engine::BackingTrackState track = m_engine.backingTrack();
    const bool changed = bpm != m_tempo || track.loaded != m_track.loaded || track.loading != m_track.loading ||
                         track.playing != m_track.playing || track.length != m_track.length ||
                         std::abs(track.position - m_track.position) >= 0.1 || track.path != m_track.path;
    m_tempo = bpm;
    m_track = track;
    if (changed) emit transportChanged();
    if (const engine::SongPosition song = m_engine.songPosition(); song != m_song) {
        m_song = song;
        emit songPositionChanged();
    }
    if (const engine::ChordFollowPosition follow = m_engine.chordFollow(); follow != m_follow) {
        m_follow = follow;
        emit chordFollowChanged();
    }
}

std::optional<core::ChannelId> EngineStatus::channelId(int channel) const
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || channel < 0 || std::cmp_greater_equal(channel, patch->channels.size())) return std::nullopt;
    return patch->channels.at(static_cast<std::size_t>(channel)).id;
}

QVariantList EngineStatus::parameters(int channel, int target) const
{
    QVariantList list;
    const auto id = channelId(channel);
    if (!id) return list;
    for (const engine::PluginParameter& p : m_engine.pluginParameters(*id, target)) {
        list << QVariantMap{{u"id"_s, p.id}, {u"name"_s, p.name}};
    }
    return list;
}

void EngineStatus::startMappingLearn(int channel, int target)
{
    const auto id = channelId(channel);
    if (!id) return;
    m_learnChannel = channel;
    m_learnTarget = target;
    m_learnedKnob.reset();
    m_learnedParameter.reset();
    // Whatever moved before this does not count.
    (void)m_engine.takeMovedController();
    (void)m_engine.takeTouchedParameter(*id, target);
    emit mappingLearnChanged();
}

void EngineStatus::setLearnParameter(quint32 id, const QString& name)
{
    if (m_learnChannel < 0) return;
    m_learnedParameter = engine::PluginParameter{.id = id, .name = name};
    emit mappingLearnChanged();
    pollMappingLearn(); // done, if the knob was already moved
}

void EngineStatus::cancelMappingLearn()
{
    if (m_learnChannel < 0) return;
    m_learnChannel = -1;
    m_learnedKnob.reset();
    m_learnedParameter.reset();
    emit mappingLearnChanged();
}

QString EngineStatus::learnedKnob() const
{
    if (!m_learnedKnob) return {};
    return tr("Knob CC %1 (channel %2)").arg(m_learnedKnob->second).arg(m_learnedKnob->first);
}

void EngineStatus::pollMappingLearn()
{
    if (m_learnChannel < 0) return;
    const auto id = channelId(m_learnChannel);
    if (!id) { // the patch changed under it
        cancelMappingLearn();
        return;
    }
    bool changed = false;
    if (const auto moved = m_engine.takeMovedController()) {
        m_learnedKnob = moved;
        changed = true;
    }
    if (const auto touched = m_engine.takeTouchedParameter(*id, m_learnTarget)) {
        m_learnedParameter = touched;
        changed = true;
    }
    if (m_learnedKnob && m_learnedParameter) {
        const int channel = m_learnChannel;
        if (m_document.addMapping(channel, m_learnedKnob->first, m_learnedKnob->second, m_learnTarget, m_learnedParameter->id,
                                  m_learnedParameter->name)) {
            emit mappingLearned(channel);
        }
        cancelMappingLearn();
        return;
    }
    if (changed) emit mappingLearnChanged();
}

double EngineStatus::readMemoryMb()
{
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        if (!m_memoryErrorLogged) { // polled 30 times a second: say it once
            m_memoryErrorLogged = true;
            qCWarning(lcUi) << "Could not read memory use: GetProcessMemoryInfo failed, error" << GetLastError();
        }
        return 0.0;
    }
    constexpr double kBytesPerMb = 1024.0 * 1024.0;
    return std::round(static_cast<double>(counters.WorkingSetSize) / kBytesPerMb); // whole MB: no flicker
}

void EngineStatus::poll()
{
    // Notices are already logged by the engine; each is shown at its level.
    for (const engine::Notice& notice : m_engine.poll()) m_document.reportMessage(notice.text, toLevel(notice.level));
    if (m_engine.takePluginEdits()) m_document.markPluginSettingsChanged();
    // Pedals, pads and buttons learned in Settings.
    for (const engine::ControlAction action : m_engine.takeControlActions()) {
        switch (action) {
        case engine::ControlAction::NextSong: m_document.nextSong(); break;
        case engine::ControlAction::PreviousSong: m_document.previousSong(); break;
        case engine::ControlAction::NextPatch: m_document.nextPatch(); break;
        case engine::ControlAction::PreviousPatch: m_document.previousPatch(); break;
        case engine::ControlAction::Panic: panic(); break;
        case engine::ControlAction::TapTempo: tapTempo(); break;
        case engine::ControlAction::PlayBacking: playPauseTrack(); break;
        case engine::ControlAction::NextSection: m_document.nextSection(); break;
        }
    }
    // A keyboard's patch buttons (Program Change).
    if (const int program = m_engine.takeProgramChange(); program >= 0) m_document.selectProgram(program);
    pollTransport();
    pollMappingLearn();
    if (const engine::MidiActivity keyboard = m_engine.keyboardActivity(); keyboard != m_keyboard) {
        m_keyboard = keyboard;
        emit keyboardChanged();
    }

    if (const float peak = m_engine.masterLevel().peak; peak != m_masterPeak) {
        m_masterPeak = peak;
        emit masterLevelChanged();
    }

    const bool wasLimiting = limiting();
    if (m_engine.takeLimiterActivity()) m_limitingPolls = 500 / kPollIntervalMs;
    else if (m_limitingPolls > 0) --m_limitingPolls;
    if (limiting() != wasLimiting) emit limitingChanged();

    const float cpu = m_engine.cpuLoad();
    const bool midi = m_engine.midiActivity();
    const QString status = m_engine.statusText();
    const double memory = readMemoryMb();
    const int inputs = m_engine.audioInputChannels();
    if (cpu != m_cpuLoad || midi != m_midiActivity || status != m_statusText || memory != m_memoryMb ||
        inputs != m_audioInputs) {
        m_audioInputs = inputs;
        m_cpuLoad = cpu;
        m_memoryMb = memory;
        m_midiActivity = midi;
        m_statusText = status;
        emit statusChanged();
    }
    emit polled();
}

} // namespace gigchain::ui
