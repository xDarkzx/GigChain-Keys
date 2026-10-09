#include "EngineStatus.h"

#include "DocumentController.h"
#include "FreezeWatchdog.h"

#include "gigchain/core/Branding.h"
#include "gigchain/engine/IEngine.h"
#include "gigchain/platform/MemoryUse.h"

#include <QDateTime>
#include <QDir>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <utility>

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

void EngineStatus::toggleRecording()
{
    if (m_engine.recording()) {
        const QString path = m_recordingPath;
        auto stopped = m_engine.stopRecording();
        if (stopped) {
            m_document.reportMessage(tr("Recorded %1 to %2").arg(QTime(0, 0).addSecs(static_cast<int>(*stopped)).toString(u"m:ss"_s),
                                                                   QDir::toNativeSeparators(path)),
                                     Notifications::Info);
        } else {
            m_document.reportMessage(stopped.error().message, Notifications::Error); // logged by the engine
        }
    } else {
        // In the Music folder: "<app> Recordings/2026-10-09 21-30 Saturday Gig.wav".
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + u'/' + branding::name() + u" Recordings"_s;
        QString setlist = m_document.displayName();
        setlist.remove(QRegularExpression(u"[\\\\/:*?\"<>|]"_s)); // not allowed in a file name
        m_recordingPath = folder + u'/' + QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH-mm"_s) + u' ' + setlist + u".wav"_s;
        if (auto started = m_engine.startRecording(m_recordingPath); !started) {
            m_document.reportMessage(started.error().message, Notifications::Error); // logged by the engine
        }
    }
    if (m_recording != m_engine.recording()) {
        m_recording = m_engine.recording();
        emit recordingChanged();
    }
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
    if (m_document.hasSections()) {
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

void EngineStatus::seekTrack(double seconds)
{
    m_engine.seekBackingTrack(seconds);
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
    // On the timeline the chart lights the chord the time has come to.
    const int step = m_song.playing && m_document.onTimeline() ? m_document.timelineStep(m_song.part, m_song.quarter) : -1;
    if (step != m_timelineStep) {
        m_timelineStep = step;
        emit chordStepChanged();
    }
}

int EngineStatus::songPlace() const
{
    return m_document.onTimeline() ? m_document.timelinePlace(m_song.part) : -1;
}

double EngineStatus::songProgress() const
{
    const int numerator = m_document.songTimeNumerator();
    const int denominator = m_document.songTimeDenominator();
    const double quartersPerBar = numerator > 0 && denominator > 0 ? numerator * 4.0 / denominator : 4.0;
    if (!m_song.playing || m_song.bars <= 0) return 0.0;
    return std::clamp(m_song.quarter / (m_song.bars * quartersPerBar), 0.0, 1.0);
}

int EngineStatus::songQueuedPlace() const
{
    return m_song.queuedPart >= 0 ? m_document.timelinePlace(m_song.queuedPart) : -1;
}

QString EngineStatus::songQueued() const
{
    if (!m_song.playing) return {};
    if (m_song.stopAtEnd) return tr("Stop at the end");
    if (m_song.queuedPart >= 0) {
        const QVariantList flow = m_document.songFlow();
        const int place = songQueuedPlace();
        return place >= 0 && place < flow.size() ? tr("→ %1").arg(flow.at(place).toMap().value(u"label"_s).toString()) : tr("→ the end");
    }
    if (m_song.hold) return tr("Hold");
    if (m_song.repeats > 1) return tr("Repeat ×%1").arg(m_song.repeats);
    if (m_song.repeats == 1) return tr("Repeat");
    return {};
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
            // Said, so the player knows it took.
            const core::Patch* patch = m_document.currentPatch();
            const core::Channel* c = patch != nullptr && std::cmp_less(channel, patch->channels.size())
                                         ? &patch->channels.at(static_cast<std::size_t>(channel))
                                         : nullptr;
            QString plugin;
            if (c != nullptr && m_learnTarget < 0 && c->instrument) plugin = c->instrument->displayName;
            else if (c != nullptr && m_learnTarget >= 0 && std::cmp_less(m_learnTarget, c->effects.size())) {
                plugin = c->effects.at(static_cast<std::size_t>(m_learnTarget)).displayName;
            }
            const QString what = plugin.isEmpty() ? m_learnedParameter->name : tr("%1 on %2").arg(m_learnedParameter->name, plugin);
            m_document.reportMessage(tr("MIDI learnt: CC %1 (channel %2) now moves %3").arg(m_learnedKnob->second).arg(m_learnedKnob->first).arg(what),
                                     Notifications::Info);
            emit mappingLearned(channel);
        }
        cancelMappingLearn();
        return;
    }
    if (changed) emit mappingLearnChanged();
}

void EngineStatus::learnMixerKnob(int slot)
{
    if (slot < 0 || slot >= engine::kAppKnobCount) {
        qCWarning(lcUi) << "Ignored: no mixer control" << slot << "to learn a knob for";
        return;
    }
    cancelMappingLearn(); // one learning at a time
    (void)m_engine.takeMovedController(); // only a knob moved from now on counts
    m_learnKnobSlot = slot;
    emit mixerKnobLearnChanged();
}

void EngineStatus::cancelMixerKnobLearn()
{
    if (m_learnKnobSlot < 0) return;
    m_learnKnobSlot = -1;
    emit mixerKnobLearnChanged();
}

void EngineStatus::pollMixerKnobs()
{
    if (m_learnKnobSlot >= 0) {
        if (const auto moved = m_engine.takeMovedController()) {
            const int slot = std::exchange(m_learnKnobSlot, -1);
            if (m_document.setMixerKnob(slot, moved->first, moved->second)) { // (a refusal is reported)
                // Said, so the player knows it took.
                constexpr int kStrips = engine::kAppKnobStrips;
                const int strip = slot <= kStrips ? slot - 1 : slot - 1 - kStrips;
                const core::Patch* playing = m_document.currentPatch();
                const QString name = playing != nullptr && slot > 0 && std::cmp_less(strip, playing->channels.size())
                                         ? playing->channels.at(static_cast<std::size_t>(strip)).name
                                         : QString();
                const QString stripText = name.isEmpty() ? tr("strip %1").arg(strip + 1) : tr("strip %1 (%2)").arg(strip + 1).arg(name);
                const QString what = slot == 0 ? tr("the master volume")
                                     : slot <= kStrips ? tr("the volume of %1").arg(stripText)
                                                       : tr("the pan of %1").arg(stripText);
                m_document.reportMessage(tr("MIDI learnt: CC %1 (channel %2) now moves %3").arg(moved->second).arg(moved->first).arg(what),
                                         Notifications::Info);
            }
            emit mixerKnobLearnChanged();
        }
    }
    // Where the learned knobs are: the fader's range (-60 to +12 dB, as on
    // screen), the pan's (-1 to +1, 64 the middle).
    constexpr int kStrips = engine::kAppKnobStrips;
    const auto volumeOf = [](int value) { return -60.0 + (72.0 * value / 127.0); };
    const engine::AppKnobValues values = m_engine.takeAppKnobValues();
    const core::Patch* patch = m_document.currentPatch();
    const int channels = patch != nullptr ? static_cast<int>(patch->channels.size()) : 0;
    // A new sound: each knob picks its fader up again (the faders are the new sound's).
    const std::optional<core::PatchId> sound = patch != nullptr ? std::optional(patch->id) : std::nullopt;
    if (sound != m_knobPickupsFor) {
        m_knobPickups.fill({});
        m_knobPickupsFor = sound;
    }
    // Fader and pan, as the knob's travel (0-1).
    const auto faderAt = [](double volumeDb) { return std::clamp((volumeDb + 60.0) / 72.0, 0.0, 1.0); };
    const auto controlAt = [&](int slot) {
        const int strip = slot <= kStrips ? slot - 1 : slot - 1 - kStrips;
        const core::Patch* now = m_document.currentPatch();
        if (slot == 0) return faderAt(masterVolumeDb());
        const core::Channel& channel = now->channels.at(static_cast<std::size_t>(strip)); // (checked by the caller)
        return slot <= kStrips ? faderAt(channel.volumeDb) : (channel.pan + 1.0) / 2.0;
    };
    for (int slot = 0; slot < engine::kAppKnobCount; ++slot) {
        const int value = values.at(static_cast<std::size_t>(slot));
        if (value < 0) continue;
        const int strip = slot <= kStrips ? slot - 1 : slot - 1 - kStrips;
        if (slot > 0 && (patch == nullptr || strip >= channels)) continue;
        const double now = controlAt(slot);
        core::KnobPickup& pickup = m_knobPickups.at(static_cast<std::size_t>(slot));
        double& setTo = m_knobSetTo.at(static_cast<std::size_t>(slot));
        // Moved since by something else (the mouse, undo): the knob picks it up again.
        if (pickup.caught && std::abs(now - setTo) > core::KnobPickup::kNear) pickup = {};
        // It moves the fader only once it gets to where the fader is: no jump.
        if (!pickup.take(value / 127.0, now)) continue;
        if (slot == 0) {
            setMasterVolumeDb(volumeOf(value));
        } else if (slot <= kStrips) {
            (void)m_document.setChannelVolume(strip, volumeOf(value));
        } else {
            (void)m_document.setChannelPan(strip, std::clamp((value - 64) / 63.0, -1.0, 1.0));
        }
        setTo = controlAt(slot); // where it put it (to see a move by something else)
    }
}

double EngineStatus::readMemoryMb()
{
    const auto bytes = platform::residentBytes();
    if (!bytes) {
        if (!m_memoryErrorLogged) { // polled 30 times a second: say it once
            m_memoryErrorLogged = true;
            qCWarning(lcUi).noquote() << "Could not read memory use:" << bytes.error().message;
        }
        return 0.0;
    }
    constexpr double kBytesPerMb = 1024.0 * 1024.0;
    return std::round(static_cast<double>(*bytes) / kBytesPerMb); // whole MB: no flicker
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
        case engine::ControlAction::NextSection: m_document.nextPart(); break;
        case engine::ControlAction::RepeatPart: m_document.repeatPart(); break;
        case engine::ControlAction::HoldPart: m_document.holdPart(); break;
        case engine::ControlAction::PlaySong:
            if (!songPlaying()) m_document.playSong();
            break;
        case engine::ControlAction::StopSong:
            if (songPlaying()) m_document.stopSong();
            break;
        case engine::ControlAction::PreviousPart: m_document.previousPart(); break;
        case engine::ControlAction::ToggleClick: setClickOn(!m_clickOn); break;
        }
    }
    // The keyboard's transport buttons and the sustain pedal's double press.
    if (const uint32_t asked = m_engine.takeTransportRequests(); asked != 0) {
        if ((asked & engine::transport::kStop) != 0) {
            if (songPlaying()) m_document.stopSong();
        } else if ((asked & engine::transport::kStart) != 0) {
            m_document.playSongFromTop();
        } else if ((asked & engine::transport::kContinue) != 0) {
            if (!songPlaying()) m_document.playSong();
        } else if ((asked & engine::transport::kToggle) != 0) {
            playPauseTrack();
        }
        // The keyboard's other buttons (MMC, Mackie Control).
        using namespace engine::transport;
        if ((asked & kNextPart) != 0) m_document.nextPart();
        if ((asked & kPreviousPart) != 0) m_document.previousPart();
        if ((asked & kLoopPart) != 0) m_document.holdPart();
        if ((asked & kClick) != 0) setClickOn(!m_clickOn);
        if ((asked & kNextSong) != 0) m_document.nextSong();
        if ((asked & kPreviousSong) != 0) m_document.previousSong();
        if ((asked & kNextSound) != 0) m_document.nextPatch();
        if ((asked & kPreviousSound) != 0) m_document.previousPatch();
    }
    // A keyboard's patch buttons (Program Change).
    if (const int program = m_engine.takeProgramChange(); program >= 0) m_document.selectProgram(program);
    pollTransport();
    pollMappingLearn();
    pollMixerKnobs();
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
    const int outputs = m_engine.outputChannels();
    if (cpu != m_cpuLoad || midi != m_midiActivity || status != m_statusText || memory != m_memoryMb ||
        inputs != m_audioInputs || outputs != m_audioOutputs) {
        if (m_recording != m_engine.recording()) { // (stopped by itself: the disk failed, said by the engine)
            m_recording = m_engine.recording();
            emit recordingChanged();
        }
        m_audioInputs = inputs;
        m_audioOutputs = outputs;
        m_cpuLoad = cpu;
        m_memoryMb = memory;
        m_midiActivity = midi;
        m_statusText = status;
        emit statusChanged();
    }
    emit polled();
}

} // namespace gigchain::ui
