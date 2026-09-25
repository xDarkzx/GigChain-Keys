#pragma once

#include "gigchain/engine/EngineTypes.h"
#include "gigchain/engine/RealEngineFactory.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QSettings;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;

// The Settings window's Audio and MIDI pages, Audacity 4 style: load() fills
// the pages from what is running, edits stay pending, apply() (OK) changes
// the engine and saves; closing without OK changes nothing. A failure is
// shown in the window (error) and the banner, logged by the engine, and the
// failed choice is not saved.
class SettingsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(QString driver READ driver WRITE setDriver NOTIFY changed)         // "system" or "asio"
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY changed)
    Q_PROPERTY(QStringList devices READ devices NOTIFY changed)                  // for the chosen driver
    Q_PROPERTY(bool asioAvailable READ asioAvailable NOTIFY changed)
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate NOTIFY changed)
    Q_PROPERTY(QVariantList sampleRates READ sampleRates NOTIFY changed)          // of the chosen device
    Q_PROPERTY(int bufferFrames READ bufferFrames WRITE setBufferFrames NOTIFY changed)
    Q_PROPERTY(QVariantList bufferSizes READ bufferSizes CONSTANT)
    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY changed)                   // one buffer at the chosen rate
    Q_PROPERTY(QVariantList midiInputs READ midiInputs NOTIFY changed)            // [{name, enabled, channel}]
    Q_PROPERTY(QString running READ running NOTIFY changed)                      // the engine's status line
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // General: open the last setlist on start instead of the start screen.
    Q_PROPERTY(bool reopenLastSetlist READ reopenLastSetlist WRITE setReopenLastSetlist NOTIFY changed)
    // Audio: the safety limiter, last before the output.
    Q_PROPERTY(bool limiterEnabled READ limiterEnabled WRITE setLimiterEnabled NOTIFY changed)
    Q_PROPERTY(double limiterCeilingDb READ limiterCeilingDb WRITE setLimiterCeilingDb NOTIFY changed)
    Q_PROPERTY(QVariantList limiterCeilings READ limiterCeilings CONSTANT)
    // Plugins: those that crashed the app while loading, switched off: [{path, name}].
    Q_PROPERTY(QVariantList blockedPlugins READ blockedPlugins NOTIFY changed)

public:
    SettingsController(engine::IEngine& engine, DocumentController& document, QSettings& settings,
                       QObject* parent = nullptr);

    // What the engine should start with (saved by apply()).
    [[nodiscard]] static engine::RealEngineOptions engineOptions(QSettings& settings);

    [[nodiscard]] QString driver() const;
    void setDriver(const QString& driver);
    [[nodiscard]] QString device() const { return m_pending.device; }
    void setDevice(const QString& device);
    [[nodiscard]] QStringList devices() const;
    [[nodiscard]] bool asioAvailable() const;
    [[nodiscard]] int sampleRate() const { return static_cast<int>(m_pending.sampleRate); }
    void setSampleRate(int rate);
    [[nodiscard]] QVariantList sampleRates() const;
    [[nodiscard]] int bufferFrames() const { return static_cast<int>(m_pending.bufferFrames); }
    void setBufferFrames(int frames);
    [[nodiscard]] static QVariantList bufferSizes();
    [[nodiscard]] double latencyMs() const;
    [[nodiscard]] QVariantList midiInputs() const;
    [[nodiscard]] QString running() const { return m_running; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] bool reopenLastSetlist() const { return m_reopenLast; }
    void setReopenLastSetlist(bool reopen);
    [[nodiscard]] bool limiterEnabled() const { return m_limiterOn; }
    void setLimiterEnabled(bool on);
    [[nodiscard]] double limiterCeilingDb() const { return m_limiterCeilingDb; }
    void setLimiterCeilingDb(double ceilingDb);
    [[nodiscard]] static QVariantList limiterCeilings();
    [[nodiscard]] QVariantList blockedPlugins() const;
    // "Try again": the plugin may load next time it is used (straight away, not on OK).
    Q_INVOKABLE void unblockPlugin(const QString& path);

    // Probes the devices (ASIO drivers can take a moment) and shows what runs now.
    Q_INVOKABLE void load();
    Q_INVOKABLE void setMidiInputEnabled(const QString& name, bool enabled);
    // 0 = all channels, 1-16 = only that one.
    Q_INVOKABLE void setMidiInputChannel(const QString& name, int channel);
    // While the window is open: picks up keyboards plugged in or pulled out,
    // keeping the choices already made on this page.
    Q_INVOKABLE void refreshMidi();
    // Windows default output at its own rate, 256 frames, only the first MIDI
    // input on, the start screen on start, the safety limiter on at -1 dB.
    Q_INVOKABLE void resetToDefaults();
    // OK: true when everything took effect (and was saved).
    Q_INVOKABLE bool apply();

signals:
    void changed();

private:
    [[nodiscard]] const engine::AudioOutput* chosenOutput() const;
    void keepRateValid();
    [[nodiscard]] engine::MidiSetup pendingMidi() const;

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QSettings& m_settings;
    std::vector<engine::AudioOutput> m_outputs;
    engine::AudioSetup m_pending;
    engine::AudioSetup m_loaded; // what ran when the window opened
    std::vector<engine::MidiPort> m_midi; // as shown, with this page's changes
    bool m_midiTouched = false;           // changed on this page since load()
    bool m_reopenLast = false;
    bool m_limiterOn = true;
    double m_limiterCeilingDb = -1.0;
    QString m_running;
    QString m_error;
};

} // namespace gigchain::ui
