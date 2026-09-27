#pragma once

#include "gigchain/core/Model.h"
#include "gigchain/engine/EngineTypes.h"
#include "gigchain/engine/MidiControl.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

class QSettings;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;

// The loop pedal, as the screen, the keyboard's buttons and (later) the
// tablet remote see it: one set of commands for every way of pressing, and
// what the loops are doing, refreshed as the engine is polled.
//
// Record and Loop act on a channel of the current patch (by index); from
// the keyboard, on the channel selected in the mixer, which a learned knob
// or next/previous buttons choose. The learned controls are kept with the
// setlist.
class LoopController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    // One per channel of the current patch: {state, progress, bar, bars, layers}.
    // state: "empty", "armed", "recording", "closing", "playing",
    // "overdubArmed", "overdubbing", "stopped", "startArmed", "stopArmed".
    Q_PROPERTY(QVariantList channelLoops READ channelLoops NOTIFY loopsChanged)
    // Every loop of the song (any patch): [{id, name, state, progress, bar, bars}].
    Q_PROPERTY(QVariantList allLoops READ allLoops NOTIFY loopsChanged)
    Q_PROPERTY(int loopCount READ loopCount NOTIFY loopsChanged)
    Q_PROPERTY(int playingCount READ playingCount NOTIFY loopsChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY loopsChanged)
    // The looper strip above the mixer (remembered).
    Q_PROPERTY(bool stripVisible READ stripVisible WRITE setStripVisible NOTIFY settingsChanged)
    // Free loops: the first also sets the tempo (remembered).
    Q_PROPERTY(bool tempoFromFirstLoop READ tempoFromFirstLoop WRITE setTempoFromFirstLoop NOTIFY settingsChanged)
    // The learned keyboard controls: [{button, label, trigger}] and the knob.
    Q_PROPERTY(QVariantList controls READ controls NOTIFY controlsChanged)
    Q_PROPERTY(QString selectorText READ selectorText NOTIFY controlsChanged)
    Q_PROPERTY(int selectorMode READ selectorMode WRITE setSelectorMode NOTIFY controlsChanged)
    // What is being learned: -1 nothing, 0-5 a button, knobControl the instrument knob.
    Q_PROPERTY(int learning READ learning NOTIFY learningChanged)
    Q_PROPERTY(int knobControl READ knobControl CONSTANT)

public:
    static constexpr int kLearnSelector = engine::kLoopButtonCount;

    LoopController(engine::IEngine& engine, DocumentController& document, QSettings& settings, QObject* parent = nullptr);

    [[nodiscard]] QVariantList channelLoops() const;
    [[nodiscard]] QVariantList allLoops() const;
    [[nodiscard]] int loopCount() const { return static_cast<int>(m_loops.size()); }
    [[nodiscard]] int playingCount() const;
    [[nodiscard]] bool recording() const;
    [[nodiscard]] bool stripVisible() const;
    void setStripVisible(bool visible);
    [[nodiscard]] bool tempoFromFirstLoop() const;
    void setTempoFromFirstLoop(bool take);
    [[nodiscard]] QVariantList controls() const;
    [[nodiscard]] QString selectorText() const;
    [[nodiscard]] int selectorMode() const;
    void setSelectorMode(int mode);
    [[nodiscard]] int learning() const { return m_learning; }
    [[nodiscard]] static int knobControl() { return kLearnSelector; }

    // The looper buttons, for a channel of the current patch.
    Q_INVOKABLE void record(int channel);
    Q_INVOKABLE void playStop(int channel);
    Q_INVOKABLE void undo(int channel);
    Q_INVOKABLE void clear(int channel);
    // A loop of any patch of the song (the loops list), by channel id.
    Q_INVOKABLE void playStopLoop(const QString& id);
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void clearAll();

    // Learning from the keyboard: a button (0-5) or the knob (kLearnSelector).
    Q_INVOKABLE void learn(int what);
    Q_INVOKABLE void cancelLearning();
    Q_INVOKABLE void forget(int what);

    // Each engine poll: the loops' state, the keyboard's presses and knob,
    // and what is being learned.
    void poll();

signals:
    void loopsChanged();
    void settingsChanged();
    void controlsChanged();
    void learningChanged();

private:
    [[nodiscard]] std::optional<core::ChannelId> channelId(int channel) const;
    [[nodiscard]] int channelCount() const;
    // The channel the keyboard acts on: the selected one, else the first.
    [[nodiscard]] int actingChannel() const;
    void send(int channel, engine::LoopCommand what);
    void act(engine::LoopAction action);
    void select(int channel);
    void pollLearning();
    void setLearning(int what);

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QSettings& m_settings;
    std::vector<engine::ChannelLoop> m_loops;
    int m_learning = -1;
    // Learning the knob: which controller moved, and the values it sent.
    std::optional<std::pair<int, int>> m_knob;
    std::vector<int> m_knobValues;
};

// The screen's name for a loop state ("playing"...).
QString loopStateName(engine::LoopState state);

} // namespace gigchain::ui
