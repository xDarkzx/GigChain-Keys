#include "FreezeWatchdog.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMetaObject>

#include <chrono>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace gigchain::ui {
namespace {

std::mutex g_markMutex;
QString g_lastMark = QStringLiteral("(nothing yet)");
QElapsedTimer g_clock;

} // namespace

void FreezeWatchdog::mark(const QString& action)
{
    const std::lock_guard lock(g_markMutex);
    g_lastMark = action;
}

FreezeWatchdog::FreezeWatchdog(QObject* parent) : QObject(parent)
{
    g_clock.start();
    m_answeredAt = 0;
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
        QMetaObject::invokeMethod(this, [this] { m_answeredAt = g_clock.elapsed(); }, Qt::QueuedConnection);
        QThread::sleep(kInterval);
        const qint64 now = g_clock.elapsed();
        const qint64 blocked = now - m_answeredAt;
        if (blocked > kFreezeMs + 100) {
            reportedUpTo = m_answeredAt; // still frozen: report when it ends
        } else if (reportedUpTo != 0) {
            const qint64 lasted = m_answeredAt - reportedUpTo;
            QString after;
            {
                const std::lock_guard lock(g_markMutex);
                after = g_lastMark;
            }
            qCWarning(lcUi).noquote() << "UI froze for about" << lasted << "ms, after:" << after;
            reportedUpTo = 0;
        }
    }
}

} // namespace gigchain::ui
