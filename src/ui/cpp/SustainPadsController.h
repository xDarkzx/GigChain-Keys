#pragma once

#include "gigchain/engine/EngineTypes.h"

#include <QObject>
#include <QRect>
#include <QStringList>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

class QSettings;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;
class EffectWindows;

// Sustain Pads, as Sunday Keys' Tonic: a pad held on one key under the
// song, from Pads Pro or a VST instrument loaded into it (its patch chosen
// in its own window, kept). Tap a key to choose the key; Activate starts and
// stops it (a key never does; playing, it fades over to a new key); the filter, shimmer, motion, fade time, volume
// and output are the player's, remembered. It follows the song's key (from
// its chart) when a song is picked, unless that is switched off. It never
// starts by itself when the app opens. Its panel is shown under the main
// area, or in a window of its own (a second screen), remembered.
class SustainPadsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY settingsChanged)
    Q_PROPERTY(int key READ key WRITE setKey NOTIFY settingsChanged)             // 0 C .. 11 B
    Q_PROPERTY(int preset READ preset WRITE setPreset NOTIFY settingsChanged)    // a Pads Pro pad
    Q_PROPERTY(int octave READ octave WRITE setOctave NOTIFY settingsChanged)    // -1, 0, +1
    Q_PROPERTY(double fadeSeconds READ fadeSeconds WRITE setFadeSeconds NOTIFY settingsChanged)
    Q_PROPERTY(double tone READ tone WRITE setTone NOTIFY settingsChanged)       // 0-1
    Q_PROPERTY(double shimmer READ shimmer WRITE setShimmer NOTIFY settingsChanged)
    Q_PROPERTY(double motion READ motion WRITE setMotion NOTIFY settingsChanged)
    Q_PROPERTY(double volumeDb READ volumeDb WRITE setVolumeDb NOTIFY settingsChanged) // -60..0
    Q_PROPERTY(int outputPair READ outputPair WRITE setOutputPair NOTIFY settingsChanged)
    Q_PROPERTY(bool followSongKey READ followSongKey WRITE setFollowSongKey NOTIFY settingsChanged)
    Q_PROPERTY(QStringList keyNames READ keyNames CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)
    // The song's key as its chart says ("" none), and as a key here (-1 none).
    Q_PROPERTY(QString songKey READ songKey NOTIFY songKeyChanged)
    Q_PROPERTY(int songKeyIndex READ songKeyIndex NOTIFY songKeyChanged)
    // Its meter (0-1), polled with the engine.
    Q_PROPERTY(double peak READ peak NOTIFY levelChanged)
    Q_PROPERTY(double rms READ rms NOTIFY levelChanged)
    // Its panel: shown in the main window, or in a window of its own.
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY placeChanged)
    Q_PROPERTY(bool detached READ detached WRITE setDetached NOTIFY placeChanged)
    // Its strip's mute (the volume kept for unmute).
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY settingsChanged)
    // What plays the pad: a VST instrument loaded into it ("" = Pads Pro), and its name.
    Q_PROPERTY(QString instrumentId READ instrumentId NOTIFY instrumentChanged)
    Q_PROPERTY(QString instrumentName READ instrumentName NOTIFY instrumentChanged)

public:
    SustainPadsController(engine::IEngine& engine, DocumentController& document, QSettings& settings, EffectWindows& windows,
                          QObject* parent = nullptr);
    ~SustainPadsController() override;
    SustainPadsController(const SustainPadsController&) = delete;
    SustainPadsController& operator=(const SustainPadsController&) = delete;
    SustainPadsController(SustainPadsController&&) = delete;
    SustainPadsController& operator=(SustainPadsController&&) = delete;

    [[nodiscard]] bool muted() const { return m_settings.mute; }
    void setMuted(bool on);
    [[nodiscard]] QString instrumentId() const { return m_instrumentId; }
    [[nodiscard]] QString instrumentName() const { return m_instrumentId.isEmpty() ? QStringLiteral("Pads Pro") : m_instrumentName; }

    // A VST instrument plays the pad from now on (and next time, with the
    // patch chosen in its window). False when it cannot (said why); what
    // played stays.
    Q_INVOKABLE bool loadInstrument(const QString& pluginId, const QString& name);
    // Pads Pro plays it again.
    Q_INVOKABLE void usePadsPro();
    // The VST's own window (to choose its patch), above `owner`.
    Q_INVOKABLE bool openInstrumentWindow(QWindow* owner);
    // Keeps the VST's settings now (its window closed, the app closing).
    void keepInstrumentState();

    [[nodiscard]] bool active() const { return m_settings.active; }
    void setActive(bool on);
    [[nodiscard]] int key() const { return m_settings.key; }
    void setKey(int key);
    [[nodiscard]] int preset() const { return m_settings.preset; }
    void setPreset(int preset);
    [[nodiscard]] int octave() const { return m_settings.octave; }
    void setOctave(int octave);
    [[nodiscard]] double fadeSeconds() const { return m_settings.fadeSeconds; }
    void setFadeSeconds(double seconds);
    [[nodiscard]] double tone() const { return m_settings.tone; }
    void setTone(double tone);
    [[nodiscard]] double shimmer() const { return m_settings.shimmer; }
    void setShimmer(double shimmer);
    [[nodiscard]] double motion() const { return m_settings.motion; }
    void setMotion(double motion);
    [[nodiscard]] double volumeDb() const { return m_settings.volumeDb; }
    void setVolumeDb(double db);
    [[nodiscard]] int outputPair() const { return m_settings.outputPair; }
    void setOutputPair(int pair);
    [[nodiscard]] bool followSongKey() const { return m_followSongKey; }
    void setFollowSongKey(bool follow);
    [[nodiscard]] static QStringList keyNames();
    [[nodiscard]] static QStringList presetNames();
    [[nodiscard]] QString songKey() const;
    [[nodiscard]] int songKeyIndex() const { return keyOf(songKey()); }
    [[nodiscard]] double peak() const { return m_peak; }
    [[nodiscard]] double rms() const { return m_rms; }
    [[nodiscard]] bool shown() const { return m_shown; }
    void setShown(bool on);
    [[nodiscard]] bool detached() const { return m_detached; }
    void setDetached(bool on);

    // A key tapped: the key the pad plays in. It never starts or stops the
    // pad (Activate does): off, the key is ready for when it is switched on;
    // playing, the pad fades over to it.
    Q_INVOKABLE void pickKey(int key);
    // Its window: where it was last, when that is on a screen now (empty otherwise).
    Q_INVOKABLE [[nodiscard]] QRect restoredGeometry() const;
    Q_INVOKABLE void rememberGeometry(const QRect& normal);

    // A key as a chart writes it ("D", "F#m", "Bb", "Ebmaj7") as 0 C .. 11
    // B; -1 when it is not one.
    [[nodiscard]] static int keyOf(const QString& written);

    // With the engine's poll: the meter.
    void poll();

    // Sent to the engine as they are now (also on every change).
    [[nodiscard]] const engine::SustainPadsSettings& settings() const { return m_settings; }

signals:
    void settingsChanged();
    void songKeyChanged();
    void levelChanged();
    void placeChanged();
    void instrumentChanged();

private:
    void apply(); // to the engine, remembered
    void songPicked();

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QSettings& m_store;
    engine::SustainPadsSettings m_settings;
    bool m_followSongKey = true;
    bool m_shown = false;
    bool m_detached = false;
    double m_peak = 0.0;
    double m_rms = 0.0;
    int m_lastSong = -1;
    QString m_lastSongKey;
    EffectWindows& m_windows;
    QString m_instrumentId; // "" = Pads Pro
    QString m_instrumentName;
    QTimer m_keepSoon; // its VST's settings kept once its edits settle
};

} // namespace gigchain::ui
