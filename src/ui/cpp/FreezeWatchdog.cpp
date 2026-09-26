#include "FreezeWatchdog.h"

#include "CrashReports.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMetaObject>

#include <chrono>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace gigchain::ui {
namespace {

// What the app was last doing, and the clock the watchdog measures with.
// Shared by every watchdog and mark(); built on first use (nothing thrown
// before main).
struct Shared
{
    std::mutex mutex;
    QString lastMark = QStringLiteral("(nothing yet)"); // guarded by mutex
    QElapsedTimer clock;
};

Shared& shared()
{
    static Shared state;
    return state;
}

} // namespace

void FreezeWatchdog::mark(const QString& action)
{
    Shared& s = shared();
    const std::scoped_lock lock(s.mutex);
    s.lastMark = action;
    CrashReports::setLastAction(action);
}

FreezeWatchdog::FreezeWatchdog(QObject* parent) : QObject(parent)
{
    shared().clock.start();
    connect(&m_thread, &QThread::started, &m_thread, [this] { watch(); }, Qt::DirectConnection);
    m_thread.setObjectName(QStringLiteral("FreezeWatchdog"));
    m_thread.start(QThread::LowPriority);
}

FreezeWatchdog::~FreezeWatchdog()
{
    m_stop = true;
    m_thread.quit();
    m_thread.wait();
}

void FreezeWatchdog::watch()
{
    constexpr auto kInterval = std::chrono::milliseconds(100);
    constexpr qint64 kFreezeMs = 250;
    qint64 reportedUpTo = 0; // one log line per freeze
    while (!m_stop) {
        // Ask the UI thread to note the time; if it is busy, the answer waits.
        QMetaObject::invokeMethod(this, [this] { m_answeredAt = shared().clock.elapsed(); }, Qt::QueuedConnection);
        QThread::sleep(kInterval);
        const qint64 now = shared().clock.elapsed();
        const qint64 blocked = now - m_answeredAt;
        if (blocked > kFreezeMs + 100) {
            reportedUpTo = m_answeredAt; // still frozen: report when it ends
        } else if (reportedUpTo != 0) {
            const qint64 lasted = m_answeredAt - reportedUpTo;
            QString after;
            {
                Shared& s = shared();
                const std::scoped_lock lock(s.mutex);
                after = s.lastMark;
            }
            qCWarning(lcUi).noquote() << "UI froze for about" << lasted << "ms, after:" << after;
            reportedUpTo = 0;
        }
    }
}

} // namespace gigchain::ui
