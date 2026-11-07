#include "PracticeController.h"

#include "DocumentController.h"

#include "gigchain/core/Chart.h"
#include "gigchain/core/SongMap.h"
#include "gigchain/engine/IEngine.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

constexpr int kChannel = 1;     // as the player's keyboard
constexpr int kVelocity = 90;   // a firm, even touch
constexpr int kTickMs = 16;     // about 60 updates a second
constexpr double kSlowest = 0.25;
constexpr double kLookAheadSeconds = 0.2; // Listen on the beat clock: notes are timed this far ahead

} // namespace

PracticeController::PracticeController(engine::IEngine& engine, DocumentController& document, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document)
{
    m_timer.setInterval(kTickMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &PracticeController::tick);
    // Another song, another chart, other bars or tempo: the notes follow.
    connect(&m_document, &DocumentController::chartChanged, this, &PracticeController::rebuild);
    connect(&m_document, &DocumentController::sectionsChanged, this, &PracticeController::rebuild);
    connect(&m_document, &DocumentController::songChanged, this, &PracticeController::rebuild);
    connect(&m_document, &DocumentController::chordInversionsChanged, this, &PracticeController::rebuild);
    rebuild();
}

PracticeController::~PracticeController()
{
    releaseAll();
}

void PracticeController::loadExercise(const core::PracticeTimeline& timeline, double bpm)
{
    releaseAll();
    restoreTempo();
    m_locked = false;
    m_timer.stop();
    setPlaying(false);
    m_exercise = true;
    m_timeline = timeline;
    m_tempo = bpm > 0.0 ? bpm : 120.0;
    m_speed = 1.0;   // (the tempo is the exercise's)
    m_loopSection = -1;
    m_mode = PlayAlong;
    emit settingsChanged();
    publishTimeline();
}

void PracticeController::clearExercise()
{
    if (!m_exercise) return;
    m_exercise = false;
    rebuild();
}

void PracticeController::rebuild()
{
    if (m_exercise) return; // (the song's notes come back with clearExercise)
    const core::Chart chart = core::parseChordPro(m_document.currentChart());
    // (The song's flow; each chord as written: the same one twice is played twice, each for its time.)
    const core::SongMap map = core::buildSongMap(chart, m_document.currentSongFlow(), false);
    std::vector<int> bars;
    QStringList names;
    for (const QVariant& section : m_document.currentSections()) {
        const QVariantMap s = section.toMap();
        bars.push_back(s.value(u"bars"_s).toInt());
        names << s.value(u"name"_s).toString();
    }
    const int beats = m_document.songTimeNumerator() > 0 ? m_document.songTimeNumerator() : 4;
    // The song's tempo; a song without one: the tempo it was practised at
    // (while playing, the engine's is practice's own), else the click's.
    double songTempo = m_document.songTempo() > 0.0 ? m_document.songTempo() : m_engine.tempo();
    if (m_document.songTempo() <= 0.0 && m_restoreTempo > 0.0) songTempo = m_tempo;
    if (songTempo <= 0.0) songTempo = 120.0;
    core::VoicingStyle style = m_style;
    style.chosen.clear();
    const QVariantMap chosen = m_document.currentChordInversions();
    for (auto it = chosen.begin(); it != chosen.end(); ++it) style.chosen[it.key()] = it.value().toInt();
    core::PracticeTimeline timeline = core::practiceTimeline(map, bars, names, beats, style);
    // Nothing practice plays changed (another sound of the song picked, a
    // channel renamed): it plays on. (It stopped and went back to the start.)
    if (timeline == m_timeline && qFuzzyCompare(songTempo, m_tempo) && style.chosen == m_style.chosen) return;
    // (Stopped as a pause: its notes let go, the click's tempo back.)
    m_timer.stop();
    releaseAll();
    restoreTempo();
    m_locked = false;
    setPlaying(false);
    m_tempo = songTempo;
    m_style = style;
    m_timeline = std::move(timeline);
    publishTimeline();
}

void PracticeController::publishTimeline()
{
    m_notes.clear();
    m_chords.clear();
    m_sections.clear();
    for (std::size_t i = 0; i < m_timeline.chords.size(); ++i) {
        const core::PracticeChord& chord = m_timeline.chords.at(i);
        m_chords << QVariantMap{{u"name"_s, chord.name}, {u"start"_s, chord.start}, {u"length"_s, chord.length},
                                {u"section"_s, chord.section},
                                {u"low"_s, chord.right.empty() ? chord.bass : chord.right.front()}}; // (its name sits beside it)
        const auto note = [&](int pitch, bool left) {
            const auto finger = chord.fingers.find(pitch);
            m_notes << QVariantMap{{u"pitch"_s, pitch}, {u"start"_s, chord.start}, {u"length"_s, chord.length},
                                   {u"left"_s, left}, {u"chord"_s, static_cast<int>(i)},
                                   {u"finger"_s, finger != chord.fingers.end() ? finger->second : 0}};
        };
        for (const int pitch : chord.left) note(pitch, true);
        for (const int pitch : chord.right) note(pitch, false);
    }
    for (const core::PracticeSectionMark& mark : m_timeline.sections) {
        m_sections << QVariantMap{{u"name"_s, mark.name}, {u"start"_s, mark.start}, {u"section"_s, mark.section}};
    }
    // A looped section that is no longer there: the whole song.
    if (m_loopSection >= 0 && std::ranges::none_of(m_timeline.sections, [this](const auto& s) { return s.section == m_loopSection; })) {
        m_loopSection = -1;
        emit settingsChanged();
    }
    m_position = loopStart();
    m_waiting = false;
    emit songChanged();
    emit positionChanged();
}

QString PracticeController::nowChord() const
{
    const int at = chordAt(m_position);
    return at >= 0 ? m_timeline.chords.at(static_cast<std::size_t>(at)).name : QString();
}

QString PracticeController::nextChord() const
{
    const auto next = std::ranges::find_if(m_timeline.chords, [this](const core::PracticeChord& chord) { return chord.start > m_position + 1e-9; });
    return next != m_timeline.chords.end() ? next->name : QString();
}

QVariantList PracticeController::targetNotes() const
{
    // Every chord sounding now (a warm-up's held bass note and its tune's notes too).
    QVariantList list;
    for (const core::PracticeChord& chord : m_timeline.chords) {
        if (m_position < chord.start - 1e-9 || m_position >= chord.start + chord.length - 1e-9) continue;
        for (const int pitch : chord.left) list << pitch;
        for (const int pitch : chord.right) list << pitch;
    }
    return list;
}

void PracticeController::setLeftHand(int wanted)
{
    const auto kept = static_cast<core::LeftHand>(std::clamp(wanted, 0, static_cast<int>(core::LeftHand::Full)));
    if (kept == m_style.left) return;
    m_style.left = kept;
    emit settingsChanged();
    rebuild();
}

void PracticeController::setRightHand(int wanted)
{
    const auto kept = static_cast<core::RightHand>(std::clamp(wanted, 0, static_cast<int>(core::RightHand::Chosen)));
    if (kept == m_style.right) return;
    m_style.right = kept;
    emit settingsChanged();
    rebuild();
}

void PracticeController::setMode(int wanted)
{
    const int kept = std::clamp(wanted, static_cast<int>(Listen), static_cast<int>(WaitForMe));
    if (kept == m_mode) return;
    releaseAll(); // (Listen's notes: the player plays now)
    m_mode = kept;
    m_waiting = false;
    m_soundFrom = m_position; // (back to Listen: from the next chord on)
    emit settingsChanged();
}

void PracticeController::setSpeed(double wanted)
{
    const double kept = std::clamp(wanted, kSlowest, 1.0);
    if (qFuzzyCompare(kept, m_speed)) return;
    m_speed = kept;
    if (m_playing && m_locked) applyTempo(); // (the click slows with it)
    emit settingsChanged();
}

void PracticeController::setLoopSection(int section)
{
    const bool known = section < 0 || std::ranges::any_of(m_timeline.sections, [section](const auto& s) { return s.section == section; });
    if (!known) {
        qCWarning(lcUi) << "Practice: there is no section" << section + 1 << "to loop";
        return;
    }
    m_loopSection = std::max(-1, section);
    releaseAll();
    m_position = loopStart();
    m_waiting = false;
    if (m_playing) {
        if (m_locked) lockToClock();
        else m_soundFrom = m_position;
    }
    emit settingsChanged();
    emit positionChanged();
}

void PracticeController::seek(double beat)
{
    if (!std::isfinite(beat)) return;
    const double kept = std::clamp(beat, loopStart(), std::max(loopStart(), loopEnd() - 1e-6));
    releaseAll();
    m_position = kept;
    m_waiting = false;
    if (m_playing) {
        if (m_locked) lockToClock(); // (on from there, its bar lines on the click's)
        else m_soundFrom = m_position;
    }
    emit positionChanged();
}

double PracticeController::loopStart() const
{
    if (m_loopSection < 0) return 0.0;
    const auto mark = std::ranges::find_if(m_timeline.sections, [this](const core::PracticeSectionMark& s) { return s.section == m_loopSection; });
    return mark != m_timeline.sections.end() ? mark->start : 0.0;
}

double PracticeController::loopEnd() const
{
    if (m_loopSection < 0) return m_timeline.length;
    for (std::size_t i = 0; i < m_timeline.sections.size(); ++i) {
        if (m_timeline.sections.at(i).section != m_loopSection) continue;
        return i + 1 < m_timeline.sections.size() ? m_timeline.sections.at(i + 1).start : m_timeline.length;
    }
    return m_timeline.length;
}

int PracticeController::chordAt(double beat) const
{
    for (std::size_t i = 0; i < m_timeline.chords.size(); ++i) {
        const core::PracticeChord& chord = m_timeline.chords.at(i);
        if (beat >= chord.start - 1e-9 && beat < chord.start + chord.length - 1e-9) return static_cast<int>(i);
    }
    return -1;
}

bool PracticeController::held(const core::PracticeChord& chord) const
{
    const engine::MidiActivity keys = m_engine.keyboardActivity();
    const auto down = [&keys](int pitch) { return pitch >= 0 && pitch < 128 && keys.velocity.at(static_cast<std::size_t>(pitch)) > 0; };
    return std::ranges::all_of(chord.left, down) && std::ranges::all_of(chord.right, down);
}

void PracticeController::play()
{
    if (m_timeline.chords.empty()) {
        qCInfo(lcUi) << "Practice: this song has no chords to play";
        return;
    }
    if (m_position >= loopEnd() - 1e-9) m_position = loopStart();
    m_clock.restart();
    // A warm-up keeps its own tempo and clock; the song goes with the click.
    if (!m_exercise) {
        lockToClock();
        if (m_locked) applyTempo();
    } else {
        m_locked = false;
        m_soundFrom = m_position;
    }
    m_timer.start();
    setPlaying(true);
    emit positionChanged();
}

void PracticeController::pause()
{
    m_timer.stop();
    releaseAll();
    restoreTempo();
    m_locked = false;
    m_position = std::max(m_position, m_soundFrom); // (paused in the lead-in: back where it was)
    setPlaying(false);
    emit positionChanged();
}

void PracticeController::stop()
{
    pause();
    m_position = loopStart();
    m_waiting = false;
    emit positionChanged();
}

void PracticeController::tick()
{
    const auto ms = static_cast<double>(m_clock.restart());
    if (m_locked) follow();
    else advance(ms);
}

double PracticeController::beatsPerQuarter() const
{
    const engine::BeatClock clock = m_engine.beatClock();
    return clock.quartersPerBar > 0.0 ? clock.beatsPerBar / clock.quartersPerBar : 1.0;
}

void PracticeController::lockToClock()
{
    const engine::BeatClock clock = m_engine.beatClock();
    m_locked = clock.running;
    m_soundFrom = m_position;
    m_scheduledTo = m_position;
    m_lapScheduled = 0.0;
    if (!m_locked) {
        qCInfo(lcUi) << "Practice: no audio running, so it keeps its own time (no click to follow)";
        return;
    }
    // Its bar lines on the click's: it starts a little before (less than a
    // bar), nothing played until it is back where it was.
    const double perBar = std::max(1, m_timeline.beatsPerBar);
    const double clockPhase = std::fmod(clock.quarter * beatsPerQuarter(), perBar);
    const double ourPhase = std::fmod(m_position, perBar);
    double lead = std::fmod(ourPhase - clockPhase + perBar, perBar);
    if (lead > perBar - 1e-6) lead = 0.0;
    m_anchorBeat = m_position - lead;
    m_anchorQuarter = clock.quarter;
    m_position = m_anchorBeat;
}

void PracticeController::applyTempo()
{
    if (m_restoreTempo <= 0.0) m_restoreTempo = m_engine.tempo();
    const double wanted = std::clamp(m_tempo * m_speed, 20.0, 400.0);
    if (std::abs(m_engine.tempo() - wanted) > 1e-6) m_engine.setTempo(wanted);
    m_appliedTempo = wanted;
}

void PracticeController::restoreTempo()
{
    if (m_restoreTempo <= 0.0) return;
    // Only practice's own tempo is put back: another song picked meanwhile
    // set its own (a pedal, while practising), and keeps it. (It was put
    // back to the last song's.)
    const bool stillOurs = std::abs(m_engine.tempo() - m_appliedTempo) <= 1e-6;
    if (stillOurs && std::abs(m_engine.tempo() - m_restoreTempo) > 1e-6) m_engine.setTempo(m_restoreTempo);
    m_restoreTempo = 0.0;
    m_appliedTempo = 0.0;
}

void PracticeController::follow()
{
    if (!m_playing || !m_locked) return;
    const engine::BeatClock clock = m_engine.beatClock();
    if (!clock.running) { // the audio stopped (a device lost): on by the screen's clock
        qCWarning(lcUi) << "Practice: the beat clock stopped (the audio is not running); it goes on by its own time";
        releaseAll();
        restoreTempo();
        m_locked = false;
        m_soundFrom = m_position;
        m_clock.restart();
        return;
    }
    const double perQuarter = beatsPerQuarter();
    double target = m_anchorBeat + (clock.quarter - m_anchorQuarter) * perQuarter;
    // The clock jumped (a song played from its top, a MIDI clock's Start): on from here.
    if (target < m_position - m_timeline.beatsPerBar || target > m_position + 2.0 * m_timeline.beatsPerBar) {
        qCInfo(lcUi) << "Practice: the beat clock jumped; it goes on from where it is";
        m_anchorBeat = m_position;
        m_anchorQuarter = clock.quarter;
        target = m_position;
    }
    target = std::max(target, m_position); // (the clock's reading wobbles a hair: never back)

    // Wait for me: the next chord stays at the line until all of it is held
    // (then it goes on from there, at once).
    if (m_mode == WaitForMe) {
        for (const core::PracticeChord& chord : m_timeline.chords) {
            if (chord.start < m_position - 1e-9 || chord.start > target) continue;
            if (!held(chord)) {
                m_position = chord.start;
                m_anchorBeat = chord.start;
                m_anchorQuarter = clock.quarter;
                m_waiting = true;
                emit positionChanged();
                return;
            }
            break;
        }
        m_waiting = false;
    }

    const double end = loopEnd();
    const double quarterAtEnd = m_anchorQuarter + (end - m_anchorBeat) / perQuarter;
    // Listen: the notes a little ahead, timed for the engine (round the loop too).
    if (m_mode == Listen) {
        const double ahead = target + kLookAheadSeconds * clock.tempo / 60.0 * perQuarter;
        if (m_scheduledTo < end) {
            schedule(m_scheduledTo, std::min(ahead, end), m_anchorBeat, m_anchorQuarter, m_soundFrom);
            m_scheduledTo = std::min(ahead, end);
        }
        if (ahead > end && m_loopSection >= 0 && m_scheduledTo >= end) {
            const double start = loopStart();
            const double into = std::min(ahead - end, end - start);
            if (m_lapScheduled < into) {
                schedule(start + m_lapScheduled, start + into, start, quarterAtEnd, start);
                m_lapScheduled = into;
            }
        }
    }
    if (target >= end) {
        if (m_loopSection < 0) { // the whole song: done
            releaseAll();
            restoreTempo();
            m_locked = false;
            m_position = end;
            m_timer.stop();
            setPlaying(false);
            emit positionChanged();
            emit finished();
            return;
        }
        // A section: round again, on the clock (its notes already timed).
        const double start = loopStart();
        const double span = std::max(end - start, 1e-6);
        m_anchorBeat = start;
        m_anchorQuarter = quarterAtEnd;
        target = start + std::fmod(target - end, span);
        m_scheduledTo = start + m_lapScheduled;
        m_lapScheduled = 0.0;
        m_soundFrom = start;
    }
    m_position = target;
    emit positionChanged();
}

void PracticeController::schedule(double from, double to, double anchorBeat, double anchorQuarter, double soundFrom)
{
    if (to <= from) return;
    const double perQuarter = beatsPerQuarter();
    const auto quarterOf = [&](double beat) { return anchorQuarter + (beat - anchorBeat) / perQuarter; };
    // Let go first: a note ending where the same note starts again sounds again.
    for (const core::PracticeChord& chord : m_timeline.chords) {
        const double end = chord.start + chord.length;
        if (end <= from || end > to || chord.start < soundFrom - 1e-9) continue;
        for (const int pitch : chord.right) m_engine.scheduleNote(kChannel, pitch, 0, quarterOf(end));
        for (const int pitch : chord.left) m_engine.scheduleNote(kChannel, pitch, 0, quarterOf(end));
    }
    for (const core::PracticeChord& chord : m_timeline.chords) {
        if (chord.start < from || chord.start >= to || chord.start < soundFrom - 1e-9) continue;
        for (const int pitch : chord.left) {
            m_engine.scheduleNote(kChannel, pitch, kVelocity, quarterOf(chord.start));
            m_sounding.insert(pitch); // (let go for sure at a pause: releaseAll)
        }
        for (const int pitch : chord.right) {
            m_engine.scheduleNote(kChannel, pitch, kVelocity, quarterOf(chord.start));
            m_sounding.insert(pitch);
        }
    }
}

void PracticeController::advance(double ms)
{
    if (!m_playing || ms <= 0.0) return;
    double target = m_position + ms / 1000.0 * m_tempo / 60.0 * m_speed;

    // Wait for me: the next chord stays at the line until all of it is held.
    if (m_mode == WaitForMe) {
        for (const core::PracticeChord& chord : m_timeline.chords) {
            if (chord.start < m_position - 1e-9 || chord.start > target) continue;
            if (!held(chord)) {
                m_position = chord.start;
                m_waiting = true;
                emit positionChanged();
                return;
            }
            break;
        }
        m_waiting = false;
    }

    const double end = loopEnd();
    if (target >= end) {
        if (m_mode == Listen) sound(m_position, end);
        releaseAll();
        if (m_loopSection < 0) { // the whole song: done
            m_position = end;
            m_timer.stop();
            setPlaying(false);
            emit positionChanged();
            emit finished();
            return;
        }
        // A section: round again.
        const double start = loopStart();
        const double span = std::max(end - start, 1e-6);
        m_position = start;
        target = start + std::fmod(target - end, span);
    }
    if (m_mode == Listen) sound(m_position, target);
    m_position = target;
    emit positionChanged();
}

void PracticeController::sound(double from, double to)
{
    // Let go first: a note ending where the same note starts again sounds again.
    for (const core::PracticeChord& chord : m_timeline.chords) {
        const double end = chord.start + chord.length;
        if (end <= from || end > to) continue;
        for (const int pitch : chord.right) {
            if (m_sounding.erase(pitch) > 0) m_engine.injectNote(kChannel, pitch, 0);
        }
        for (const int pitch : chord.left) {
            if (m_sounding.erase(pitch) > 0) m_engine.injectNote(kChannel, pitch, 0);
        }
    }
    for (const core::PracticeChord& chord : m_timeline.chords) {
        if (chord.start < from || chord.start >= to) continue;
        const auto press = [this](int pitch) {
            if (m_sounding.insert(pitch).second) m_engine.injectNote(kChannel, pitch, kVelocity);
        };
        for (const int pitch : chord.left) press(pitch);
        for (const int pitch : chord.right) press(pitch);
    }
}

void PracticeController::releaseAll()
{
    // Notes timed for later forgotten first, then every note it played let go now.
    if (m_locked) m_engine.clearScheduledNotes();
    for (const int pitch : m_sounding) m_engine.injectNote(kChannel, pitch, 0);
    m_sounding.clear();
    m_scheduledTo = m_position;
    m_lapScheduled = 0.0;
}

void PracticeController::setPlaying(bool on)
{
    if (on == m_playing) return;
    m_playing = on;
    m_engine.setPractising(on); // (a click set to play only while playing counts along)
    emit playingChanged();
}

} // namespace gigchain::ui
