#pragma once

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <mutex>

namespace gigchain::ui {

// On stage the window must never stop responding. A background thread asks
// the UI thread to answer every 100 ms; when it has not answered for more
// than 250 ms the freeze is logged with how long it lasted and what the user
// did just before (the last "mark"), so every freeze can be traced.
class FreezeWatchdog : public QObject
{
    Q_OBJECT

public:
    explicit FreezeWatchdog(QObject* parent = nullptr);
    ~FreezeWatchdog() override;
    FreezeWatchdog(const FreezeWatchdog&) = delete;
    FreezeWatchdog& operator=(const FreezeWatchdog&) = delete;
    FreezeWatchdog(FreezeWatchdog&&) = delete;
    FreezeWatchdog& operator=(FreezeWatchdog&&) = delete;

    // What the user just did ("switch to Funk Tune"); any thread.
    static void mark(const QString& action);

private:
    void watch();

    QThread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<qint64> m_answeredAt{0}; // ms since start, set by the UI thread
};

} // namespace gigchain::ui
