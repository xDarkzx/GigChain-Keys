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

void PracticeController::rebuild()
{
    releaseAll();
    setPlaying(false);
    const core::Chart chart = core::parseChordPro(m_document.currentChart());
    const core::SongMap map = core::buildSongMap(chart);
    std::vector<int> bars;
    QStringList names;
    for (const QVariant& section : m_document.currentSections()) {
        const QVariantMap s = section.toMap();
        bars.push_back(s.value(u"bars"_s).toInt());
        names << s.value(u"name"_s).toString();
    }
    const int beats = m_document.songTimeNumerator() > 0 ? m_document.songTimeNumerator() : 4;
    m_tempo = m_document.songTempo() > 0.0 ? m_document.songTempo() : 120.0;
    m_style.chosen.clear();
    const QVariantMap chosen = m_document.currentChordInversions();
    for (auto it = chosen.begin(); it != chosen.end(); ++it) m_style.chosen[it.key()] = it.value().toInt();
    m_timeline = core::practiceTimeline(map, bars, names, beats, m_style);

    m_notes.clear();
    m_chords.clear();
    m_sections.clear();
    for (std::size_t i = 0; i < m_timeline.chords.size(); ++i) {
        const core::PracticeChord& chord = m_timeline.chords.at(i);
        m_chords << QVariantMap{{u"name"_s, chord.name}, {u"start"_s, chord.start}, {u"length"_s, chord.length},
                                {u"section"_s, chord.section},
                                {u"low"_s, chord.right.empty() ? chord.bass : chord.right.front()}}; // (its name sits beside it)
        const auto note = [&](int pitch, bool left) {
            m_notes << QVariantMap{{u"pitch"_s, pitch}, {u"start"_s, chord.start}, {u"length"_s, chord.length},
                                   {u"left"_s, left}, {u"chord"_s, static_cast<int>(i)}};
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
    QVariantList list;
    const int at = chordAt(m_position);
    if (at < 0) return list;
    const core::PracticeChord& chord = m_timeline.chords.at(static_cast<std::size_t>(at));
    for (const int pitch : chord.left) list << pitch;
    for (const int pitch : chord.right) list << pitch;
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
    emit settingsChanged();
}

void PracticeController::setSpeed(double wanted)
{
    const double kept = std::clamp(wanted, kSlowest, 1.0);
    if (qFuzzyCompare(kept, m_speed)) return;
    m_speed = kept;
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
    emit settingsChanged();
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
    m_timer.start();
    setPlaying(true);
}

void PracticeController::pause()
{
    m_timer.stop();
    releaseAll();
    setPlaying(false);
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
    advance(static_cast<double>(m_clock.restart()));
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
    for (const int pitch : m_sounding) m_engine.injectNote(kChannel, pitch, 0);
    m_sounding.clear();
}

void PracticeController::setPlaying(bool on)
{
    if (on == m_playing) return;
    m_playing = on;
    emit playingChanged();
}

} // namespace gigchain::ui
