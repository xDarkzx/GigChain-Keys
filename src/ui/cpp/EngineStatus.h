#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace openstage::engine {
class IEngine;
}

namespace openstage::ui {

class DocumentController;

// Polls the engine ~30 times a second: CPU, MIDI activity, status line, and
// any notices (forwarded to the document's message banner). Also the UI's way
// to play notes (on-screen keyboard) and set the master volume.
class EngineStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(float cpuLoad READ cpuLoad NOTIFY statusChanged)
    Q_PROPERTY(bool midiActivity READ midiActivity NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(double masterVolumeDb READ masterVolumeDb WRITE setMasterVolumeDb NOTIFY masterVolumeDbChanged)

public:
    static constexpr int kPollIntervalMs = 33;

    EngineStatus(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);

    [[nodiscard]] float cpuLoad() const { return m_cpuLoad; }
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
    engine::IEngine& m_engine;
    DocumentController& m_document;
    QTimer m_timer;
    float m_cpuLoad = 0.0F;
    bool m_midiActivity = false;
    QString m_statusText;
};

} // namespace openstage::ui
