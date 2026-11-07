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
// a keyboard (core/Practice: a count-in bar, each chord for its bars, each
// voiced from the last), at the song's tempo (the tempo playing when it has
// none). With the audio running it goes by the engine's beat clock: its bar
// lines on the click's, the tempo set on the engine (slowed with the speed,
// back after), its notes played on their sample; without, by its own timer.
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
    // Go to `beat` (a section's start: the player skips to that part of the
    // song); playing, it plays on from there. Within the loop, when one is set.
    Q_INVOKABLE void seek(double beat);
    // The clock: `ms` of real time gone by (the timer's, or a test's), when
    // the engine's beat clock is not running (no audio); with it, where it is.
    void advance(double ms);
    // On the engine's beat clock (the audio is going): the notes fall with
    // the click and are played on their sample. Tests: the clock moved on.
    void follow();
    [[nodiscard]] bool onBeatClock() const { return m_locked; }

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
    // Listen on the beat clock: notes starting or ending in [from, to) of the
    // practice's beats, timed for the engine (beat `anchorBeat` is clock
    // quarter `anchorQuarter`); chords starting before `soundFrom` are not played.
    void schedule(double from, double to, double anchorBeat, double anchorQuarter, double soundFrom);
    void releaseAll();
    void setPlaying(bool on);
    // Playing on the beat clock from m_position: bar lines line up with the click's.
    void lockToClock();
    // The tempo practised at (the song's, slowed by the speed) on the engine; back as it was.
    void applyTempo();
    void restoreTempo();
    [[nodiscard]] double beatsPerQuarter() const;

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
    // On the engine's beat clock: practice beat m_anchorBeat is clock quarter m_anchorQuarter.
    bool m_locked = false;
    double m_anchorBeat = 0.0;
    double m_anchorQuarter = 0.0;
    double m_soundFrom = 0.0;  // nothing before here is played (the lead-in to the click's bar line)
    double m_scheduledTo = 0.0; // Listen: notes are timed up to here (practice beats, this lap)
    double m_lapScheduled = 0.0; // looping: beats of the next lap already timed (from the loop's start)
    double m_restoreTempo = 0.0; // the engine's tempo before practice set its own; 0: not set
    double m_appliedTempo = 0.0; // the tempo practice set (another since: a song's own, left as it is)
};

} // namespace gigchain::ui
