#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

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

public:
    static constexpr int kPollIntervalMs = 33;

    EngineStatus(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);

    [[nodiscard]] float cpuLoad() const { return m_cpuLoad; }
    // RAM this process (with its plugins) is using, in MB; 0 if Windows could not say (logged).
    [[nodiscard]] double memoryMb() const { return m_memoryMb; }
    [[nodiscard]] bool midiActivity() const { return m_midiActivity; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] double masterVolumeDb() const;
    void setMasterVolumeDb(double volumeDb);

    // On-screen keyboard: note on (velocity 100) or off, on MIDI channel 1.
    Q_INVOKABLE void playNote(int note, bool on);

public slots:
    void poll();

signals:
    void statusChanged();
    void masterVolumeDbChanged();
    void polled();

private:
    double readMemoryMb();

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QTimer m_timer;
    float m_cpuLoad = 0.0F;
    double m_memoryMb = 0.0;
    bool m_memoryErrorLogged = false;
    bool m_midiActivity = false;
    QString m_statusText;
};

} // namespace gigchain::ui
