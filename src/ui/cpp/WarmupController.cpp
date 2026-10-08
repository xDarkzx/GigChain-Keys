#include "WarmupController.h"

#include "PracticeController.h"

#include "gigchain/engine/IEngine.h"

#include <QLoggingCategory>
#include <QSettings>

#include <algorithm>
#include <chrono>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

const QString kLevelKey = u"warmup/level"_s;
const QString kProgressGroup = u"warmup/progress"_s;

int64_t steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

WarmupController::WarmupController(engine::IEngine& engine, PracticeController& practice, QSettings& settings, QObject* parent)
    : QObject(parent), m_engine(engine), m_practice(practice), m_settings(settings), m_now(steadyNs)
{
    load();
    // The falling notes move: the keys pressed meanwhile are read against where they are.
    connect(&m_practice, &PracticeController::positionChanged, this, [this] {
        if (!m_playing) return;
        collectKeys();
        m_anchor = {m_now(), m_practice.position()};
    });
    connect(&m_practice, &PracticeController::finished, this, [this] {
        if (m_playing) finishRun();
    });
}

void WarmupController::load()
{
    const int saved = m_settings.value(kLevelKey, 0).toInt();
    m_settings.beginGroup(kProgressGroup);
    for (const QString& id : m_settings.childKeys()) {
        const QVariantMap p = m_settings.value(id).toMap();
        m_progress[id] = core::WarmupProgress{.tempo = p.value(u"tempo"_s).toDouble(),
                                              .cleanRuns = p.value(u"cleanRuns"_s).toInt(),
                                              .passed = p.value(u"passed"_s).toBool(),
                                              .bestStars = p.value(u"stars"_s).toInt()};
    }
    m_settings.endGroup();
    m_level = static_cast<core::WarmupLevel>(std::clamp(saved, 0, unlockedLevel()));
}

void WarmupController::save(const QString& id) const
{
    const auto found = m_progress.find(id);
    if (found == m_progress.end()) return;
    const core::WarmupProgress& p = found->second;
    m_settings.setValue(kProgressGroup + u'/' + id,
                        QVariantMap{{u"tempo"_s, p.tempo}, {u"cleanRuns"_s, p.cleanRuns}, {u"passed"_s, p.passed}, {u"stars"_s, p.bestStars}});
}

void WarmupController::setActive(bool on)
{
    if (on == m_active) return;
    m_active = on;
    if (!on) {
        stop();
        m_routine = false;
        m_exercise = -1;
        m_practice.clearExercise(); // the song's notes again
    }
    emit stateChanged();
}

void WarmupController::setLevel(int wanted)
{
    if (wanted < 0 || wanted > unlockedLevel()) {
        qCInfo(lcUi) << "Warm-up: level" << wanted << "is not open yet (pass the level before it)";
        return;
    }
    if (wanted == level()) return;
    stop();
    m_routine = false;
    m_level = static_cast<core::WarmupLevel>(wanted);
    m_exercise = -1;
    m_result.clear();
    m_settings.setValue(kLevelKey, wanted);
    emit stateChanged();
    emit resultChanged();
}

int WarmupController::unlockedLevel() const
{
    return static_cast<int>(core::unlockedWarmupLevel(m_progress));
}

QVariantList WarmupController::exercises() const
{
    QVariantList list;
    const std::vector<core::WarmupExercise> all = levelExercises();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const core::WarmupExercise& e = all.at(i);
        const auto found = m_progress.find(e.id);
        const core::WarmupProgress p = found != m_progress.end() ? found->second : core::WarmupProgress{};
        list << QVariantMap{{u"index"_s, static_cast<int>(i)},
                            {u"id"_s, e.id},
                            {u"name"_s, e.name},
                            {u"tip"_s, e.tip},
                            {u"tempo"_s, core::warmupTempo(p, e)},
                            {u"target"_s, e.targetTempo},
                            {u"cleanRuns"_s, p.cleanRuns},
                            {u"passed"_s, p.passed},
                            {u"stars"_s, p.bestStars}};
    }
    return list;
}

double WarmupController::tempo() const
{
    const std::vector<core::WarmupExercise> all = levelExercises();
    if (m_exercise < 0 || std::cmp_greater_equal(m_exercise, all.size())) return 0.0;
    const core::WarmupExercise& e = all.at(static_cast<std::size_t>(m_exercise));
    const auto found = m_progress.find(e.id);
    return core::warmupTempo(found != m_progress.end() ? found->second : core::WarmupProgress{}, e);
}

std::vector<std::pair<int, core::WarmupHands>> WarmupController::steps() const
{
    std::vector<std::pair<int, core::WarmupHands>> list;
    const auto count = static_cast<int>(levelExercises().size());
    for (int i = 0; i < count; ++i) {
        for (const core::WarmupHands h : {core::WarmupHands::Right, core::WarmupHands::Left, core::WarmupHands::Both}) list.emplace_back(i, h);
    }
    return list;
}

int WarmupController::routineMinutes() const
{
    // Each run's length at its tempo, with the count-in, and a moment between runs.
    double seconds = 0.0;
    const std::vector<core::WarmupExercise> all = levelExercises();
    for (const auto& [index, side] : steps()) {
        const core::WarmupExercise& e = all.at(static_cast<std::size_t>(index));
        const auto found = m_progress.find(e.id);
        const double bpm = core::warmupTempo(found != m_progress.end() ? found->second : core::WarmupProgress{}, e);
        const core::PracticeTimeline timeline = core::warmupTimeline(core::warmupNotes(e, side));
        seconds += (timeline.length * 60.0 / bpm) + 8.0;
    }
    return std::max(1, static_cast<int>(std::ceil(seconds / 60.0)));
}

void WarmupController::startRoutine()
{
    m_routine = true;
    m_finishedToday = false;
    m_routineStep = 0;
    const auto [index, side] = steps().front();
    m_exercise = index;
    m_hands = side;
    startRun();
}

void WarmupController::startExercise(int index, int side)
{
    const auto count = static_cast<int>(levelExercises().size());
    if (index < 0 || index >= count || side < 0 || side > static_cast<int>(core::WarmupHands::Both)) {
        qCWarning(lcUi) << "Warm-up: there is no exercise" << index + 1 << "with hands" << side;
        return;
    }
    m_routine = false;
    m_exercise = index;
    m_hands = static_cast<core::WarmupHands>(side);
    startRun();
}

void WarmupController::again()
{
    if (m_exercise < 0) return;
    startRun();
}

void WarmupController::next()
{
    if (m_routine) {
        const auto all = steps();
        if (m_routineStep + 1 >= static_cast<int>(all.size())) {
            stop();
            m_routine = false;
            m_finishedToday = true;
            qCInfo(lcUi) << "Warm-up: today's routine done";
            emit stateChanged();
            return;
        }
        ++m_routineStep;
        m_exercise = all.at(static_cast<std::size_t>(m_routineStep)).first;
        m_hands = all.at(static_cast<std::size_t>(m_routineStep)).second;
        startRun();
        return;
    }
    if (m_exercise < 0) {
        startExercise(0, static_cast<int>(core::WarmupHands::Right));
        return;
    }
    // Right, left, both; then the next exercise.
    if (m_hands != core::WarmupHands::Both) {
        m_hands = static_cast<core::WarmupHands>(static_cast<int>(m_hands) + 1);
    } else {
        m_hands = core::WarmupHands::Right;
        m_exercise = (m_exercise + 1) % static_cast<int>(levelExercises().size());
    }
    startRun();
}

void WarmupController::stop()
{
    if (!m_playing) return;
    m_playing = false;
    m_practice.stop();
    emit stateChanged();
}

void WarmupController::startRun()
{
    const std::vector<core::WarmupExercise> all = levelExercises();
    const core::WarmupExercise& e = all.at(static_cast<std::size_t>(m_exercise));
    m_notes = core::warmupNotes(e, m_hands);
    m_runTempo = tempo();
    m_keys.clear();
    m_result.clear();
    m_playing = false; // (loading moves the notes: nothing to read yet)
    m_practice.loadExercise(core::warmupTimeline(m_notes), m_runTempo);
    (void)m_engine.takeKeyPresses(); // only keys from now on count
    m_anchor = {m_now(), m_practice.position()};
    m_playing = true;
    m_practice.play();
    qCInfo(lcUi).noquote() << "Warm-up:" << e.name << "hands" << static_cast<int>(m_hands) << "at" << m_runTempo << "BPM";
    emit stateChanged();
    emit resultChanged();
}

void WarmupController::collectKeys()
{
    const double beatsPerNs = m_runTempo / 60.0 / 1e9;
    for (const engine::KeyPress& press : m_engine.takeKeyPresses()) {
        const double beat = m_anchor.second + (static_cast<double>(press.timeNs - m_anchor.first) * beatsPerNs);
        m_keys.push_back(core::PlayedKey{.pitch = press.note, .beat = beat - core::kWarmupCountIn, .velocity = press.velocity});
    }
}

void WarmupController::finishRun()
{
    collectKeys();
    m_playing = false;
    const std::vector<core::WarmupExercise> all = levelExercises();
    const core::WarmupExercise& e = all.at(static_cast<std::size_t>(m_exercise));
    const core::WarmupScore score = core::scoreWarmup(m_notes, m_keys, m_runTempo, m_level);
    core::WarmupProgress& progress = m_progress[e.id];
    const int levelBefore = unlockedLevel();
    const core::WarmupStep step = core::recordWarmupRun(progress, e, score, m_hands);
    save(e.id);
    m_result = QVariantMap{{u"stars"_s, score.stars},
                           {u"notes"_s, score.notes},
                           {u"right"_s, score.right},
                           {u"missed"_s, score.missed},
                           {u"extra"_s, score.extra},
                           {u"timingMs"_s, score.timingMs},
                           {u"driftMs"_s, score.driftMs},
                           {u"evennessMs"_s, score.evennessMs},
                           {u"touchSpread"_s, score.touchSpread},
                           {u"handsApartMs"_s, score.handsApartMs},
                           {u"clean"_s, score.clean},
                           {u"tip"_s, score.tip},
                           {u"tempoUp"_s, step.tempoUp},
                           {u"passed"_s, step.justPassed},
                           {u"levelUp"_s, unlockedLevel() > levelBefore},
                           {u"cleanRuns"_s, progress.cleanRuns},
                           {u"hands"_s, static_cast<int>(m_hands)}};
    qCInfo(lcUi).noquote() << "Warm-up:" << e.name << "scored" << score.stars << "stars," << score.right << "of" << score.notes
                           << "notes, timing" << std::lround(score.timingMs) << "ms" << (score.clean ? "(clean)" : "");
    emit stateChanged();
    emit resultChanged();
}

} // namespace gigchain::ui
