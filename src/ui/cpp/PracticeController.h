#pragma once

#include "gigchain/core/Practice.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <set>

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;

// The Practice tab's player: the current song's chart as notes falling onto
// a keyboard (core/Practice: a count-in bar, each section's bars shared
// between its chords, each chord voiced from the last), in time with the
// song's tempo (120 BPM and 4/4 when it has none).
//   Listen: it plays the notes through the patch, as the keyboard would.
//   PlayAlong: the notes fall at the tempo; the player plays.
//   WaitForMe: each chord waits at the line until all its notes are held.
// Slower down to 25 %; a section loops; Stop goes back to the start (of the
// loop). Notes it plays are always let go: pause, stop, a song change.
// Positions are in beats.
class PracticeController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    // [{pitch, start, length, left (the bass), chord (its index), finger (1-5; 0: not shown)}]
    Q_PROPERTY(QVariantList notes READ notes NOTIFY songChanged)
    // A warm-up exercise is loaded (loadExercise) in place of the song.
    Q_PROPERTY(bool exercise READ exercise NOTIFY songChanged)
    // [{name, start, length, section, low (its right hand's lowest note)}]
    Q_PROPERTY(QVariantList chords READ chords NOTIFY songChanged)
    // [{name, start, section}]
    Q_PROPERTY(QVariantList sections READ sections NOTIFY songChanged)
    Q_PROPERTY(double length READ length NOTIFY songChanged)
    Q_PROPERTY(int beatsPerBar READ beatsPerBar NOTIFY songChanged)
    Q_PROPERTY(double tempo READ tempo NOTIFY songChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool waiting READ waiting NOTIFY positionChanged)
    Q_PROPERTY(QString nowChord READ nowChord NOTIFY positionChanged)
    Q_PROPERTY(QString nextChord READ nextChord NOTIFY positionChanged)
    // The notes to play now (pitches).
    Q_PROPERTY(QVariantList targetNotes READ targetNotes NOTIFY positionChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY settingsChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY settingsChanged)
    // -1: the whole song, once; a section: looped.
    Q_PROPERTY(int loopSection READ loopSection WRITE setLoopSection NOTIFY settingsChanged)
    // What each hand plays (core::LeftHand, core::RightHand): 0 the bass alone,
    // 1 octave, 2 root and fifth, 3 the full chord; 0 smooth, 1 root position,
    // 2 the inversions chosen in the song (its chord diagrams).
    Q_PROPERTY(int leftHand READ leftHand WRITE setLeftHand NOTIFY settingsChanged)
    Q_PROPERTY(int rightHand READ rightHand WRITE setRightHand NOTIFY settingsChanged)

public:
    enum Mode
    {
        Listen,
        PlayAlong,
        WaitForMe,
    };
    Q_ENUM(Mode)

    PracticeController(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);
    ~PracticeController() override;
    PracticeController(const PracticeController&) = delete;
    PracticeController& operator=(const PracticeController&) = delete;
    PracticeController(PracticeController&&) = delete;
    PracticeController& operator=(PracticeController&&) = delete;

    [[nodiscard]] QVariantList notes() const { return m_notes; }
    [[nodiscard]] QVariantList chords() const { return m_chords; }
    [[nodiscard]] QVariantList sections() const { return m_sections; }
    [[nodiscard]] double length() const { return m_timeline.length; }
    [[nodiscard]] int beatsPerBar() const { return m_timeline.beatsPerBar; }
    [[nodiscard]] double tempo() const { return m_tempo; }
    [[nodiscard]] double position() const { return m_position; }
    [[nodiscard]] bool playing() const { return m_playing; }
    [[nodiscard]] bool waiting() const { return m_waiting; }
    [[nodiscard]] QString nowChord() const;
    [[nodiscard]] QString nextChord() const;
    [[nodiscard]] QVariantList targetNotes() const;
    [[nodiscard]] int mode() const { return m_mode; }
    void setMode(int wanted);
    [[nodiscard]] double speed() const { return m_speed; }
    void setSpeed(double wanted);
    [[nodiscard]] int loopSection() const { return m_loopSection; }
    void setLoopSection(int section);
    [[nodiscard]] int leftHand() const { return static_cast<int>(m_style.left); }
    void setLeftHand(int wanted);
    [[nodiscard]] int rightHand() const { return static_cast<int>(m_style.right); }
    void setRightHand(int wanted);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    // The clock: `ms` of real time gone by (the timer's, or a test's).
    void advance(double ms);

    // A warm-up exercise (core::warmupTimeline) at `bpm`, played in place
    // of the song until clearExercise(); the song's changes wait till then.
    void loadExercise(const core::PracticeTimeline& timeline, double bpm);
    void clearExercise();
    [[nodiscard]] bool exercise() const { return m_exercise; }

signals:
    // Played to the end (not looping): stopped there.
    void finished();
    void songChanged();
    void positionChanged();
    void playingChanged();
    void settingsChanged();

private:
    void rebuild();
    // m_notes, m_chords and m_sections from m_timeline.
    void publishTimeline();
    void tick();
    [[nodiscard]] double loopStart() const;
    [[nodiscard]] double loopEnd() const;
    // The chord sounding at `beat` (-1: none).
    [[nodiscard]] int chordAt(double beat) const;
    [[nodiscard]] bool held(const core::PracticeChord& chord) const;
    // Listen: notes starting or ending in [from, to) played or let go.
    void sound(double from, double to);
    void releaseAll();
    void setPlaying(bool on);

    engine::IEngine& m_engine;
    DocumentController& m_document;
    core::PracticeTimeline m_timeline;
    core::VoicingStyle m_style; // (its chosen inversions: the song's, at each rebuild)
    QVariantList m_notes;
    QVariantList m_chords;
    QVariantList m_sections;
    double m_tempo = 120.0;
    bool m_exercise = false; // a warm-up exercise is loaded in place of the song
    double m_position = 0.0;
    bool m_playing = false;
    bool m_waiting = false;
    int m_mode = Listen;
    double m_speed = 1.0;
    int m_loopSection = -1;
    std::set<int> m_sounding; // notes played through the patch, still down
    QTimer m_timer;
    QElapsedTimer m_clock;
};

} // namespace gigchain::ui
