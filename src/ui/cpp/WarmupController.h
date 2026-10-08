#pragma once

#include "gigchain/core/Warmup.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

class QSettings;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class PracticeController;

// The Practice tab's warm-up (core/Warmup): exercises at three levels, each
// played right hand, left hand, then both, as falling notes (the Practice
// tab's player, an exercise loaded in place of the song). Each run is
// scored from the keys' exact times (IEngine::takeKeyPresses); three clean
// both-hands runs move the tempo up 5 BPM; passing a level's exercises
// opens the next. Progress is kept in the settings (the player's, not the
// setlist's).
class WarmupController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    // The Practice tab shows the warm-up (else the song).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY stateChanged)
    // core::WarmupLevel; levels above unlockedLevel cannot be chosen.
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY stateChanged)
    Q_PROPERTY(int unlockedLevel READ unlockedLevel NOTIFY stateChanged)
    // The level's exercises: [{index, id, name, tip, tempo (now), target, cleanRuns, passed, stars}].
    Q_PROPERTY(QVariantList exercises READ exercises NOTIFY stateChanged)
    // The exercise loaded (an index into exercises; -1: none) and the hands (core::WarmupHands).
    Q_PROPERTY(int exercise READ exercise NOTIFY stateChanged)
    Q_PROPERTY(int hands READ hands NOTIFY stateChanged)
    Q_PROPERTY(double tempo READ tempo NOTIFY stateChanged)
    // A run is being played.
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    // Today's warm-up: running, at which step of how many, how long it takes.
    Q_PROPERTY(bool routine READ routine NOTIFY stateChanged)
    Q_PROPERTY(int routineStep READ routineStep NOTIFY stateChanged)
    Q_PROPERTY(int routineSteps READ routineSteps NOTIFY stateChanged)
    Q_PROPERTY(int routineMinutes READ routineMinutes NOTIFY stateChanged)
    Q_PROPERTY(bool finishedToday READ finishedToday NOTIFY stateChanged)
    // The last run's score: {stars, notes, right, missed, extra, timingMs,
    // driftMs, evennessMs, touchSpread, handsApartMs (-1: one hand), clean,
    // tip, tempoUp, passed (just now), cleanRuns, hands}; empty: none yet.
    Q_PROPERTY(QVariantMap result READ result NOTIFY resultChanged)

public:
    WarmupController(engine::IEngine& engine, PracticeController& practice, QSettings& settings, QObject* parent = nullptr);
    ~WarmupController() override = default;
    WarmupController(const WarmupController&) = delete;
    WarmupController& operator=(const WarmupController&) = delete;
    WarmupController(WarmupController&&) = delete;
    WarmupController& operator=(WarmupController&&) = delete;

    [[nodiscard]] bool active() const { return m_active; }
    void setActive(bool on);
    [[nodiscard]] int level() const { return static_cast<int>(m_level); }
    void setLevel(int wanted);
    [[nodiscard]] int unlockedLevel() const;
    [[nodiscard]] QVariantList exercises() const;
    [[nodiscard]] int exercise() const { return m_exercise; }
    [[nodiscard]] int hands() const { return static_cast<int>(m_hands); }
    [[nodiscard]] double tempo() const;
    [[nodiscard]] bool playing() const { return m_playing; }
    [[nodiscard]] bool routine() const { return m_routine; }
    [[nodiscard]] int routineStep() const { return m_routineStep; }
    [[nodiscard]] int routineSteps() const { return static_cast<int>(steps().size()); }
    [[nodiscard]] int routineMinutes() const;
    [[nodiscard]] bool finishedToday() const { return m_finishedToday; }
    [[nodiscard]] QVariantMap result() const { return m_result; }

    // Today's warm-up from its first step.
    Q_INVOKABLE void startRoutine();
    // One exercise of the level (its index) with `side`: the hands (core::WarmupHands).
    Q_INVOKABLE void startExercise(int index, int side);
    // The same run again; the next one (the routine's next step, else the
    // next hands, then the next exercise); stop playing.
    Q_INVOKABLE void again();
    Q_INVOKABLE void next();
    Q_INVOKABLE void stop();

    // The clock the keys' times are read against (steady clock, ns); tests set their own.
    void setClock(std::function<int64_t()> now) { m_now = std::move(now); }

signals:
    void stateChanged();
    void resultChanged();

private:
    [[nodiscard]] std::vector<core::WarmupExercise> levelExercises() const { return core::warmupExercises(m_level); }
    // Today's steps: each exercise right hand, left hand, both.
    [[nodiscard]] std::vector<std::pair<int, core::WarmupHands>> steps() const;
    void startRun();
    // The keys pressed since the last look, as beats of the exercise.
    void collectKeys();
    void finishRun();
    void load();
    void save(const QString& id) const;

    engine::IEngine& m_engine;
    PracticeController& m_practice;
    QSettings& m_settings;
    std::function<int64_t()> m_now;
    bool m_active = false;
    core::WarmupLevel m_level = core::WarmupLevel::Beginner;
    std::map<QString, core::WarmupProgress> m_progress; // by exercise id
    int m_exercise = -1;
    core::WarmupHands m_hands = core::WarmupHands::Right;
    bool m_playing = false;
    bool m_routine = false;
    int m_routineStep = 0;
    bool m_finishedToday = false;
    std::vector<core::WarmupNote> m_notes; // the run's notes
    double m_runTempo = 60.0;
    std::vector<core::PlayedKey> m_keys;   // the run's keys
    // Where the falling notes were (beats) when the clock read `ns`: keys convert from it.
    std::pair<int64_t, double> m_anchor{0, 0.0};
    QVariantMap m_result;
};

} // namespace gigchain::ui
