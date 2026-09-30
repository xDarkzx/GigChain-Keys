#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/platform/InstanceLock.h"

#include <QLocalServer>
#include <QObject>
#include <QString>

namespace gigchain::ui {

// One app at a time. A second start (a setlist double-clicked while the app
// runs) hands its setlist to the running app and ends: two apps would fight
// over the audio interface and the keyboard.
//
// The first start holds the lock named `name` (platform::InstanceLock: on
// Windows a mutex the installer looks for) and listens on this user's local
// socket of that name (platform::instanceSocketName); later starts write
// their setlist's path to it.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QString name, QObject* parent = nullptr);
    ~SingleInstance() override;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    // The app's name for it (organization-executable, from branding.cmake).
    [[nodiscard]] static QString appName();

    // Whether this is the first start (then it stays the one until it ends).
    // Any thread; once.
    [[nodiscard]] bool first();
    // A later start: hands `path` (empty: none) to the first one. True when
    // it took it. Any thread; blocks for at most a second or two.
    [[nodiscard]] bool handOver(const QString& path) const;
    // The first start: listens for later ones (opened()), and for files the
    // system opens with it (the Mac's Finder sends those to the running app
    // instead of starting another). Main thread. An error with the reason
    // when it cannot listen (files from the system still arrive).
    core::Result<void> listen();

signals:
    // A later start handed this over, or the system opened it with the app
    // (empty: it only asked to come to the front).
    void opened(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void receive();

    QString m_name;
    QString m_pipe;
    platform::InstanceLock m_lock; // held while this is the first start
    QLocalServer m_server;
};

} // namespace gigchain::ui
