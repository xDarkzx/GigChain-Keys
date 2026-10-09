#pragma once

#include "gigchain/core/Ids.h"
#include "gigchain/core/KnobPickup.h"
#include "gigchain/engine/EngineTypes.h"
#include "gigchain/engine/MidiControl.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;

// Polls the engine ~30 times a second: CPU, memory, MIDI activity, status line, and
// any notices (forwarded to the document's message banner). Also the UI's way
// to play notes (on-screen keyboard) and set the master volume.
class EngineStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(float cpuLoad READ cpuLoad NOTIFY statusChanged)
    Q_PROPERTY(double memoryMb READ memoryMb NOTIFY statusChanged)
    Q_PROPERTY(bool midiActivity READ midiActivity NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(double masterVolumeDb READ masterVolumeDb WRITE setMasterVolumeDb NOTIFY masterVolumeDbChanged)
    // The master strip's meter: the output's peak (linear, 0..1) since the last poll.
    Q_PROPERTY(float masterPeak READ masterPeak NOTIFY masterLevelChanged)
    Q_PROPERTY(bool masterMuted READ masterMuted WRITE setMasterMuted NOTIFY masterMutedChanged)
    // The LIM light: the safety limiter caught a peak (lit for about half a
    // second after each catch, so a short one can be seen).
    Q_PROPERTY(bool limiting READ limiting NOTIFY limitingChanged)
    // The tempo playing (BPM), the click, and the song's backing track.
    Q_PROPERTY(double tempo READ tempo NOTIFY transportChanged)
    Q_PROPERTY(bool clickOn READ clickOn WRITE setClickOn NOTIFY transportChanged)
    Q_PROPERTY(double clickVolumeDb READ clickVolumeDb WRITE setClickVolumeDb NOTIFY transportChanged)
    Q_PROPERTY(bool trackLoaded READ trackLoaded NOTIFY transportChanged)
    Q_PROPERTY(bool trackLoading READ trackLoading NOTIFY transportChanged)
    Q_PROPERTY(bool trackPlaying READ trackPlaying NOTIFY transportChanged)
    Q_PROPERTY(double trackPosition READ trackPosition NOTIFY transportChanged) // seconds
    Q_PROPERTY(double trackLength READ trackLength NOTIFY transportChanged)     // seconds
    // Where the song is: counting its sections, the one in force (-1: it has
    // none) and the bar in it (0 while stopped or counting in).
    Q_PROPERTY(bool songPlaying READ songPlaying NOTIFY songPositionChanged)
    Q_PROPERTY(bool songCountingIn READ songCountingIn NOTIFY songPositionChanged)
    Q_PROPERTY(int songSection READ songSection NOTIFY songPositionChanged)
    Q_PROPERTY(int songBar READ songBar NOTIFY songPositionChanged)
    Q_PROPERTY(int songBars READ songBars NOTIFY songPositionChanged)
    // The song's timeline: the part playing, its place in the song's flow
    // (Perform's tiles), how far through it (0-1), and what is queued for
    // the next bar line or the part's end ("→ Bridge", "Repeat", "Hold",
    // "Stop at the end"; "" = nothing) and the queued part's place.
    Q_PROPERTY(int songPart READ songPart NOTIFY songPositionChanged)
    Q_PROPERTY(int songPlace READ songPlace NOTIFY songPositionChanged)
    Q_PROPERTY(double songProgress READ songProgress NOTIFY songPositionChanged)
    Q_PROPERTY(QString songQueued READ songQueued NOTIFY songPositionChanged)
    Q_PROPERTY(int songQueuedPlace READ songQueuedPlace NOTIFY songPositionChanged)
    // The part playing loops until released (Loop part / Hold).
    Q_PROPERTY(bool songHold READ songHold NOTIFY songPositionChanged)
    // The chord the song's timeline has come to (of DocumentController's
    // map), while it plays.
    Q_PROPERTY(bool chordStarted READ chordStarted NOTIFY chordStepChanged)
    Q_PROPERTY(int chordStep READ chordStep NOTIFY chordStepChanged)
    // Learning a knob for a plugin parameter: what has been caught so far.
    Q_PROPERTY(bool learningMapping READ learningMapping NOTIFY mappingLearnChanged)
    // The mixer control a keyboard knob is being learned for; -1: none.
    Q_PROPERTY(int learningMixerKnob READ learningMixerKnob NOTIFY mixerKnobLearnChanged)
    Q_PROPERTY(QString learnedKnob READ learnedKnob NOTIFY mappingLearnChanged)
    Q_PROPERTY(QString learnedParameter READ learnedParameter NOTIFY mappingLearnChanged)
    // Audio input channels open now (0: no input device chosen in Settings).
    Q_PROPERTY(int audioInputChannels READ audioInputChannels NOTIFY statusChanged)
    // The on-screen keyboard: how hard each key (0-127) is held (0 = up),
    // the pitch bend (-1..1, 0 = centre), mod wheel (0..1) and sustain pedal.
    Q_PROPERTY(QVariantList keyVelocities READ keyVelocities NOTIFY keyboardChanged)
    Q_PROPERTY(double pitchBend READ pitchBend NOTIFY keyboardChanged)
    Q_PROPERTY(double modWheel READ modWheel NOTIFY keyboardChanged)
    Q_PROPERTY(bool sustain READ sustain NOTIFY keyboardChanged)

public:
    static constexpr int kPollIntervalMs = 33;

    EngineStatus(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);

    [[nodiscard]] float cpuLoad() const { return m_cpuLoad; }
    // RAM this process (with its plugins) is using, in MB; 0 if Windows could not say (logged).
    [[nodiscard]] double memoryMb() const { return m_memoryMb; }
    [[nodiscard]] bool midiActivity() const { return m_midiActivity; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] double masterVolumeDb() const;
    [[nodiscard]] float masterPeak() const { return m_masterPeak; }
    [[nodiscard]] bool masterMuted() const;
    [[nodiscard]] bool limiting() const { return m_limitingPolls > 0; }
    void setMasterMuted(bool muted);
    void setMasterVolumeDb(double volumeDb);

    // Stops every sound now (every plugin reset, held notes released).
    Q_INVOKABLE void panic();

    // On-screen keyboard: note on (velocity 100) or off, on MIDI channel 1.
    Q_INVOKABLE void playNote(int note, bool on);

    [[nodiscard]] double tempo() const { return m_tempo; }
    // Sets the tempo playing now (not the song's; see DocumentController::setSongTempo).
    Q_INVOKABLE void setTempo(double bpm);
    // Each tap is a beat: after two, the tempo is their pace (the last few
    // taps averaged). A pause of two seconds starts counting again.
    Q_INVOKABLE void tapTempo();
    [[nodiscard]] bool clickOn() const { return m_clickOn; }
    void setClickOn(bool on);
    [[nodiscard]] double clickVolumeDb() const { return m_clickVolumeDb; }
    void setClickVolumeDb(double volumeDb);

    [[nodiscard]] bool trackLoaded() const { return m_track.loaded; }
    [[nodiscard]] bool trackLoading() const { return m_track.loading; }
    [[nodiscard]] bool trackPlaying() const { return m_track.playing; }
    [[nodiscard]] double trackPosition() const { return m_track.position; }
    [[nodiscard]] double trackLength() const { return m_track.length; }
    [[nodiscard]] bool songPlaying() const { return m_song.playing; }
    [[nodiscard]] bool songCountingIn() const { return m_song.countingIn; }
    [[nodiscard]] bool songHold() const { return m_song.playing && m_song.hold; }
    [[nodiscard]] int songSection() const { return m_song.section; }
    [[nodiscard]] int songBar() const { return m_song.bar; }
    [[nodiscard]] int songBars() const { return m_song.bars; }
    [[nodiscard]] bool chordStarted() const { return m_timelineStep >= 0; }
    [[nodiscard]] int chordStep() const { return m_timelineStep; }
    [[nodiscard]] int songPart() const { return m_song.part; }
    [[nodiscard]] int songPlace() const;
    [[nodiscard]] double songProgress() const;
    [[nodiscard]] QString songQueued() const;
    [[nodiscard]] int songQueuedPlace() const;
    Q_INVOKABLE void playPauseTrack();
    Q_INVOKABLE void rewindTrack();

    // Learning a knob: move a knob on the keyboard and the control in the
    // plugin's window (either order); the mapping is added to the channel
    // of the current patch when both are caught.
    Q_INVOKABLE void startMappingLearn(int channel, int target);
    Q_INVOKABLE void cancelMappingLearn();
    // While learning: the parameter picked from the list instead of moved in
    // the plugin's window.
    Q_INVOKABLE void setLearnParameter(quint32 id, const QString& name);
    // Learning a keyboard knob for a mixer control (right-click > Learn on
    // a fader, a pan knob, the master; DocumentController::setMixerKnob's
    // slots): the next knob moved is it.
    Q_INVOKABLE void learnMixerKnob(int slot);
    Q_INVOKABLE void cancelMixerKnobLearn();
    [[nodiscard]] int learningMixerKnob() const { return m_learnKnobSlot; }
    [[nodiscard]] int audioInputChannels() const { return m_audioInputs; }
    [[nodiscard]] QVariantList keyVelocities() const;
    [[nodiscard]] double pitchBend() const { return (m_keyboard.pitchBend - 8192) / 8192.0; }
    [[nodiscard]] double modWheel() const { return m_keyboard.modWheel / 127.0; }
    [[nodiscard]] bool sustain() const { return m_keyboard.sustain; }
    [[nodiscard]] bool learningMapping() const { return m_learnChannel >= 0; }
    [[nodiscard]] QString learnedKnob() const;
    [[nodiscard]] QString learnedParameter() const { return m_learnedParameter ? m_learnedParameter->name : QString(); }
    // The parameters a knob can move on a channel's instrument (-1) or
    // effect: [{id, name}]; empty when that plugin is not loaded.
    Q_INVOKABLE QVariantList parameters(int channel, int target) const;

public slots:
    void poll();

signals:
    void statusChanged();
    void masterVolumeDbChanged();
    void masterLevelChanged();
    void masterMutedChanged();
    void limitingChanged();
    void transportChanged();
    void songPositionChanged();
    void chordStepChanged();
    void mappingLearnChanged();
    void mixerKnobLearnChanged();
    void mappingLearned(int channel); // a knob was mapped
    void keyboardChanged();
    void polled();

private:
    double readMemoryMb();
    void pollTransport();
    void pollMappingLearn();
    // The mixer's keyboard knobs: learning one, and moving what they drive.
    void pollMixerKnobs();
    [[nodiscard]] std::optional<core::ChannelId> channelId(int channel) const;

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QTimer m_timer;
    float m_cpuLoad = 0.0F;
    float m_masterPeak = 0.0F;
    int m_limitingPolls = 0; // polls left with the LIM light on
    double m_memoryMb = 0.0;
    bool m_memoryErrorLogged = false;
    bool m_midiActivity = false;
    QString m_statusText;

    int m_audioInputs = 0;
    engine::MidiActivity m_keyboard;
    double m_tempo = 120.0;
    bool m_clickOn = false;
    double m_clickVolumeDb = -6.0;
    engine::BackingTrackState m_track;
    engine::SongPosition m_song;
    int m_timelineStep = -1; // the chord lit by the timeline; -1 = not playing on it
    QElapsedTimer m_tapClock;
    std::vector<qint64> m_taps; // ms, the last few taps
    int m_learnChannel = -1;
    int m_learnTarget = -1;
    int m_learnKnobSlot = -1; // learning a mixer knob for this slot
    // The learned mixer knobs' pickup (a knob takes over a fader only once
    // it reaches it), fresh for each sound.
    std::array<core::KnobPickup, engine::kAppKnobCount> m_knobPickups{};
    std::optional<core::PatchId> m_knobPickupsFor;
    std::optional<std::pair<int, int>> m_learnedKnob;
    std::optional<engine::PluginParameter> m_learnedParameter;
};

} // namespace gigchain::ui
