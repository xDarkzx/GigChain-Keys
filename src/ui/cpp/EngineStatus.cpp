#include "EngineStatus.h"

#include "DocumentController.h"
#include "FreezeWatchdog.h"

#include "gigchain/engine/IEngine.h"

#include <QLoggingCategory>

#include <cmath>

#include <windows.h>
#include <psapi.h>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

Notifications::Level toLevel(engine::Notice::Level level)
{
    switch (level) {
    case engine::Notice::Level::Info: return Notifications::Info;
    case engine::Notice::Level::Warning: return Notifications::Warning;
    case engine::Notice::Level::Error: return Notifications::Error;
    }
    return Notifications::Error; // a level added later is shown as the worst until mapped
}

} // namespace

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

bool EngineStatus::masterMuted() const
{
    return m_engine.masterMuted();
}

void EngineStatus::setMasterMuted(bool muted)
{
    if (m_engine.masterMuted() == muted) return;
    m_engine.setMasterMute(muted);
    emit masterMutedChanged();
}

void EngineStatus::panic()
{
    FreezeWatchdog::mark(u"panic"_s);
    m_engine.panic(); // logged by the engine
    m_document.reportMessage(tr("Panic: every sound stopped"), Notifications::Info);
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
    // Notices are already logged by the engine; each is shown at its level.
    for (const engine::Notice& notice : m_engine.poll()) m_document.reportMessage(notice.text, toLevel(notice.level));
    if (m_engine.takePluginEdits()) m_document.markPluginSettingsChanged();
    // Pedals, pads and buttons learned in Settings.
    for (const engine::ControlAction action : m_engine.takeControlActions()) {
        switch (action) {
        case engine::ControlAction::NextSong: m_document.nextSong(); break;
        case engine::ControlAction::PreviousSong: m_document.previousSong(); break;
        case engine::ControlAction::NextPatch: m_document.nextPatch(); break;
        case engine::ControlAction::PreviousPatch: m_document.previousPatch(); break;
        case engine::ControlAction::Panic: panic(); break;
        }
    }

    if (const float peak = m_engine.masterLevel().peak; peak != m_masterPeak) {
        m_masterPeak = peak;
        emit masterLevelChanged();
    }

    const bool wasLimiting = limiting();
    if (m_engine.takeLimiterActivity()) m_limitingPolls = 500 / kPollIntervalMs;
    else if (m_limitingPolls > 0) --m_limitingPolls;
    if (limiting() != wasLimiting) emit limitingChanged();

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
