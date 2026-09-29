#pragma once

#include <QLoggingCategory>
#include <QString>

#include <mutex>

// Log category for the engine module. Main thread only: the audio thread
// never logs (it counts problems in atomics that the main thread reports).
Q_DECLARE_LOGGING_CATEGORY(lcEngine)

namespace gigchain::engine {

// For a check that runs again and again (listing MIDI ports every few
// seconds): a failure is logged as a warning the first time; the same failure
// again is not repeated, until the check works again (ok()) or fails another
// way. Said once, never hidden. Any thread.
class RepeatedWarning
{
public:
    void fail(const QString& message)
    {
        const std::scoped_lock lock(m_mutex);
        if (message == m_last) return;
        m_last = message;
        qCWarning(lcEngine).noquote() << message;
    }
    void ok()
    {
        const std::scoped_lock lock(m_mutex);
        m_last.clear();
    }

private:
    std::mutex m_mutex;
    QString m_last; // the failure said last; empty: working
};

} // namespace gigchain::engine
