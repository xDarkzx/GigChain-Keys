#include "EngineStatus.h"

#include "DocumentController.h"

#include "gigchain/engine/IEngine.h"

#include <QLoggingCategory>

#include <cmath>

#include <windows.h>
#include <psapi.h>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace gigchain::ui {

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

double EngineStatus::readMemoryMb()
{
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        if (!m_memoryErrorLogged) { // polled 30 times a second: say it once
            m_memoryErrorLogged = true;
            qCWarning(lcUi) << "Could not read memory use: GetProcessMemoryInfo failed, error" << GetLastError();
        }
        return 0.0;
    }
    constexpr double kBytesPerMb = 1024.0 * 1024.0;
    return std::round(static_cast<double>(counters.WorkingSetSize) / kBytesPerMb); // whole MB: no flicker
}

void EngineStatus::poll()
{
    // Notices are already logged by the engine; show the latest to the user.
    const auto notices = m_engine.poll();
    if (!notices.empty()) m_document.reportMessage(notices.back());

    const float cpu = m_engine.cpuLoad();
    const bool midi = m_engine.midiActivity();
    const QString status = m_engine.statusText();
    const double memory = readMemoryMb();
    if (cpu != m_cpuLoad || midi != m_midiActivity || status != m_statusText || memory != m_memoryMb) {
        m_cpuLoad = cpu;
        m_memoryMb = memory;
        m_midiActivity = midi;
        m_statusText = status;
        emit statusChanged();
    }
    emit polled();
}

} // namespace gigchain::ui
