#pragma once

#include "InputPermission.h"

#include "gigchain/engine/EngineTypes.h"
#include "gigchain/engine/MidiControl.h"
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

    Q_PROPERTY(QString driver READ driver WRITE setDriver NOTIFY changed)         // "system", "asio", "jack", "alsa"
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY changed)
    Q_PROPERTY(QStringList devices READ devices NOTIFY changed)                  // for the chosen driver
    Q_PROPERTY(bool asioAvailable READ asioAvailable NOTIFY changed)
    // The drivers to choose from: [{id, name}], this system's own audio first,
    // then those the engine found a device on (ASIO; JACK, ALSA).
    Q_PROPERTY(QVariantList drivers READ drivers NOTIFY changed)
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate NOTIFY changed)
    Q_PROPERTY(QVariantList sampleRates READ sampleRates NOTIFY changed)          // of the chosen device
    Q_PROPERTY(int bufferFrames READ bufferFrames WRITE setBufferFrames NOTIFY changed)
    Q_PROPERTY(QVariantList bufferSizes READ bufferSizes CONSTANT)
    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY changed)                   // one buffer at the chosen rate
    Q_PROPERTY(QVariantList midiInputs READ midiInputs NOTIFY changed)            // [{name, enabled, mode (setMidiInputMode), channel}]
    Q_PROPERTY(QString running READ running NOTIFY changed)                      // the engine's status line
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // General: open the last setlist on start instead of the start screen.
    Q_PROPERTY(bool reopenLastSetlist READ reopenLastSetlist WRITE setReopenLastSetlist NOTIFY changed)
    // The Perform view's chart size (A−/A+): kept at once (not with Apply),
    // 1.0 to 3.0 times the edit view's.
    Q_PROPERTY(double chartTextSize READ chartTextSize WRITE setChartTextSize NOTIFY chartTextSizeChanged)
    // Audio: the safety limiter, last before the output.
    Q_PROPERTY(bool limiterEnabled READ limiterEnabled WRITE setLimiterEnabled NOTIFY changed)
    Q_PROPERTY(double limiterCeilingDb READ limiterCeilingDb WRITE setLimiterCeilingDb NOTIFY changed)
    Q_PROPERTY(QVariantList limiterCeilings READ limiterCeilings CONSTANT)
    // Where the click plays: 0 the mix, n the interface's outputs 2n+1-2n+2
    // (the in-ears only, not the audience).
    Q_PROPERTY(int clickOutput READ clickOutput WRITE setClickOutput NOTIFY changed)
    // The output choices the interface open now has: ["Main mix (1-2)", "Outputs 3-4", ...].
    Q_PROPERTY(QStringList outputChoices READ outputChoices NOTIFY changed)
    // Plugins: those that crashed the app while loading, switched off: [{path, name}].
    Q_PROPERTY(QVariantList blockedPlugins READ blockedPlugins NOTIFY changed)
    // MIDI: pedals/pads that switch songs: [{action, label, trigger ("" = not set)}].
    Q_PROPERTY(QVariantList controls READ controls NOTIFY changed)
    // The control waiting for a press ("Learn"), -1 when none.
    Q_PROPERTY(int learning READ learning NOTIFY changed)
    // Audio: the device whose inputs channels can play ("" = none), from
    // those of the chosen driver.
    Q_PROPERTY(QString inputDevice READ inputDevice WRITE setInputDevice NOTIFY changed)
    Q_PROPERTY(QStringList inputDevices READ inputDevices NOTIFY changed)
    // MIDI clock: the output it is sent to ("" = not sent), the outputs
    // there are, and whether the tempo follows a clock coming in.
    Q_PROPERTY(QString clockOutput READ clockOutput WRITE setClockOutput NOTIFY changed)
    Q_PROPERTY(QStringList midiOutputs READ midiOutputs NOTIFY changed)
    Q_PROPERTY(bool followClock READ followClock WRITE setFollowClock NOTIFY changed)
    // The keyboard's Play/Stop buttons start and stop the song; a quick
    // double press of the sustain pedal does too.
    Q_PROPERTY(bool transportButtons READ transportButtons WRITE setTransportButtons NOTIFY changed)
    Q_PROPERTY(bool sustainDoubleTap READ sustainDoubleTap WRITE setSustainDoubleTap NOTIFY changed)

public:
    SettingsController(engine::IEngine& engine, DocumentController& document, QSettings& settings,
                       QObject* parent = nullptr);

    // What the engine should start with (saved by apply()).
    [[nodiscard]] static engine::RealEngineOptions engineOptions(QSettings& settings);

    [[nodiscard]] QString driver() const;
    void setDriver(const QString& name);
    [[nodiscard]] QString device() const { return m_pending.device; }
    void setDevice(const QString& name);
    [[nodiscard]] QStringList devices() const;
    [[nodiscard]] bool asioAvailable() const;
    [[nodiscard]] QVariantList drivers() const;
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
    [[nodiscard]] double chartTextSize() const;
    void setChartTextSize(double size);
    void setReopenLastSetlist(bool reopen);
    [[nodiscard]] bool limiterEnabled() const { return m_limiterOn; }
    void setLimiterEnabled(bool on);
    [[nodiscard]] double limiterCeilingDb() const { return m_limiterCeilingDb; }
    void setLimiterCeilingDb(double ceilingDb);
    [[nodiscard]] static QVariantList limiterCeilings();
    [[nodiscard]] int clickOutput() const { return m_clickOutput; }
    [[nodiscard]] QStringList outputChoices() const;
    void setClickOutput(int pair);
    [[nodiscard]] QVariantList blockedPlugins() const;
    [[nodiscard]] QVariantList controls() const;
    [[nodiscard]] int learning() const { return m_learning; }
    // "Learn": the next pedal, pad or button pressed is taken for `action`.
    Q_INVOKABLE void learnControl(int action);
    Q_INVOKABLE void clearControl(int action);
    // Called as the engine is polled: takes a press while learning.
    void pollLearning();
    // "Try again": the plugin may load next time it is used (straight away, not on OK).
    Q_INVOKABLE void unblockPlugin(const QString& path);
    [[nodiscard]] QString inputDevice() const { return m_pending.inputDevice; }
    void setInputDevice(const QString& name);
    [[nodiscard]] QStringList inputDevices() const;
    [[nodiscard]] QString clockOutput() const { return m_clockOutput; }
    void setClockOutput(const QString& name);
    [[nodiscard]] QStringList midiOutputs() const { return m_midiOutputs; }
    [[nodiscard]] bool followClock() const { return m_followClock; }
    void setFollowClock(bool follow);
    [[nodiscard]] bool transportButtons() const { return m_transportButtons; }
    void setTransportButtons(bool on);
    [[nodiscard]] bool sustainDoubleTap() const { return m_sustainDoubleTap; }
    void setSustainDoubleTap(bool on);

    // Probes the devices (ASIO drivers can take a moment) and shows what runs now.
    Q_INVOKABLE void load();
    Q_INVOKABLE void setMidiInputEnabled(const QString& name, bool enabled);
    // 0: it plays; 1: its buttons and knobs only (a keyboard's DAW port, a
    // controller); 2: off.
    Q_INVOKABLE void setMidiInputMode(const QString& name, int mode);
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

    // With an audio input in use: whether the system lets the app hear it
    // (the Mac asks the player once); refused, said with where to allow it;
    // allowed just now, the input is opened again. At start and on Apply.
    void checkInputPermission();
    void setInputPermission(InputPermission permission) { m_inputPermission = std::move(permission); } // (tests)

signals:
    void changed();
    void chartTextSizeChanged();

private:
    [[nodiscard]] const engine::AudioOutput* chosenOutput() const;
    void keepRateValid();
    [[nodiscard]] engine::MidiSetup pendingMidi() const;
    static void saveMidi(QSettings& settings, const engine::MidiSetup& midi);

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QSettings& m_settings;
    std::vector<engine::AudioOutput> m_outputs;
    engine::AudioSetup m_pending;
    engine::AudioSetup m_loaded; // what ran when the window opened
    std::vector<engine::MidiPort> m_midi; // as shown, with this page's changes
    bool m_midiTouched = false;           // changed on this page since load()
    bool m_reopenLast = false;
    engine::ControlTriggers m_controls{};
    int m_learning = -1;
    bool m_controlsTouched = false; // changed on the MIDI page since load()
    bool m_limiterOn = true;
    double m_limiterCeilingDb = -1.0;
    int m_clickOutput = 0;
    QString m_running;
    QString m_error;
    std::vector<engine::AudioInputDevice> m_inputs;
    QStringList m_midiOutputs;
    QString m_clockOutput;
    bool m_followClock = false;
    bool m_transportButtons = true;
    bool m_sustainDoubleTap = false;
    InputPermission m_inputPermission;
};

} // namespace gigchain::ui
