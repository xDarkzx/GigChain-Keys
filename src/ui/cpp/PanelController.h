#pragma once

#include "gigchain/core/Model.h"
#include "gigchain/engine/MidiControl.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;

// The panel bar, as a Yamaha's registration and part buttons or a Nord's
// program and section buttons: the song's sounds as buttons (the one
// playing lit) and the playing sound's parts (its instruments, lit when
// on). Each can be assigned a pad, button, footswitch or spare key on the
// keyboard (ASSIGN: tap the button, press it), which then picks the sound
// or turns the part on or off, by its place, in every song.
class PanelController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    // The song's sounds: [{name, active, assigned, pad}] (assigned: what on the keyboard
    // picks it, or ""; pad: the same in a few letters, "PAD C1").
    Q_PROPERTY(QVariantList sounds READ sounds NOTIFY panelChanged)
    // The playing sound's parts: [{name, on, zone, assigned}] (zone: its keys, "" for all).
    Q_PROPERTY(QVariantList parts READ parts NOTIFY panelChanged)
    // ASSIGN: tapping a button arms it; the next pad pressed is its.
    Q_PROPERTY(bool assigning READ assigning WRITE setAssigning NOTIFY assigningChanged)
    Q_PROPERTY(QVariantMap armed READ armed NOTIFY assigningChanged) // {kind, index}: index -1 = none
    // A part's split set by playing: {active, part, step (0: the lowest key
    // or pad, 1: the highest), othersHearIt (other parts of the sound
    // listen to every channel, so they hear these keys or pads too)}.
    Q_PROPERTY(QVariantMap zoneLearning READ zoneLearning NOTIFY zoneLearningChanged)
    Q_PROPERTY(int soundButton READ soundButton CONSTANT)
    Q_PROPERTY(int partButton READ partButton CONSTANT)

public:
    enum Button : int { SoundButton = 0, PartButton = 1 };
    Q_ENUM(Button)

    PanelController(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);

    [[nodiscard]] QVariantList sounds() const;
    [[nodiscard]] QVariantList parts() const;
    [[nodiscard]] bool assigning() const { return m_assigning; }
    void setAssigning(bool on);
    [[nodiscard]] QVariantMap armed() const;
    [[nodiscard]] static int soundButton() { return SoundButton; }
    [[nodiscard]] static int partButton() { return PartButton; }

    // A click on the bar: the sound picked, the part on or off.
    Q_INVOKABLE void pickSound(int index);
    Q_INVOKABLE void togglePart(int index);

    // The song's sounds, from the panel. + adds one after the others,
    // playing at once: a copy of the sound playing (its instruments, to
    // change) or an empty one; returns its place (-1: refused, said).
    Q_INVOKABLE int addSound(bool copyPlaying);
    Q_INVOKABLE bool renameSound(int index, const QString& name); // a name, not blank
    Q_INVOKABLE bool duplicateSound(int index);
    Q_INVOKABLE bool deleteSound(int index); // never the song's last
    // ASSIGN: the button waiting for its pad (tapped again: not any more).
    Q_INVOKABLE void arm(int kind, int index);
    Q_INVOKABLE void clearAssignment(int kind, int index);

    // Set by playing: part `part`'s keys are the two pressed next (the
    // lowest and highest, either order), from the channel they come on.
    Q_INVOKABLE void startZoneLearning(int part);
    Q_INVOKABLE void cancelZoneLearning();
    [[nodiscard]] QVariantMap zoneLearning() const;
    // The sound's other parts that listen to every channel listen to
    // `keysChannel` (1-16: the keys') only, so `part`'s pads are its alone.
    Q_INVOKABLE void keepOthersOnTheKeys(int part, int keysChannel);

    // Polled with the engine: a pad pressed for the armed button, and the
    // panel's pads pressed on the keyboard.
    void poll();

signals:
    void panelChanged();
    void assigningChanged();
    void zoneLearningChanged();

private:
    // Whether the panel has that button; the button set to `value` (false: there is none).
    [[nodiscard]] static bool hasSlot(int kind, int index);
    static bool setSlot(core::PanelControls& controls, int kind, int index, const core::LearnedControl& value);
    void press(const engine::PanelPress& press);

    engine::IEngine& m_engine;
    DocumentController& m_document;
    bool m_assigning = false;
    int m_armedKind = SoundButton;
    int m_armedIndex = -1;
    // Set by playing (part -1: not now): the first key or pad pressed (-1: none yet).
    int m_zonePart = -1;
    int m_zoneFirst = -1;
    int m_zoneChannel = 0;   // 1-16
    int m_lastZonePart = -1; // the part set by playing last (for othersHearIt)
    [[nodiscard]] bool othersHear(int part) const;
    void takeZonePress(const engine::MidiTrigger& pressed);
};

} // namespace gigchain::ui
