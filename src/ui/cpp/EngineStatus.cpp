#include "EngineStatus.h"

#include "DocumentController.h"

#include "openstage/engine/IEngine.h"

namespace openstage::ui {

EngineStatus::EngineStatus(engine::IEngine& engine, DocumentController& document, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document), m_statusText(engine.statusText())
{
    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &EngineStatus::poll);
    m_timer.start();
}

double EngineStatus::masterVolumeDb() const
{
    return m_engine.masterVolume();
}

void EngineStatus::setMasterVolumeDb(double volumeDb)
{
    const double before = m_engine.masterVolume();
    m_engine.setMasterVolume(volumeDb);
    if (m_engine.masterVolume() != before) emit masterVolumeDbChanged();
}

void EngineStatus::playNote(int note, bool on)
{
    m_engine.injectNote(1, note, on ? 100 : 0);
}

void EngineStatus::poll()
{
    // Notices are already logged by the engine; show the latest to the user.
    const auto notices = m_engine.poll();
    if (!notices.empty()) m_document.reportMessage(notices.back());

    const float cpu = m_engine.cpuLoad();
    const bool midi = m_engine.midiActivity();
    const QString status = m_engine.statusText();
    if (cpu != m_cpuLoad || midi != m_midiActivity || status != m_statusText) {
        m_cpuLoad = cpu;
        m_midiActivity = midi;
        m_statusText = status;
        emit statusChanged();
    }
    emit polled();
}

} // namespace openstage::ui
