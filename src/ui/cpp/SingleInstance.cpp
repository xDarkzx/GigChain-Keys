#include "SingleInstance.h"

#include "gigchain/core/Branding.h"

#include <QLocalSocket>
#include <QLoggingCategory>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

constexpr int kWaitMs = 1000; // a running app answers in milliseconds

} // namespace

SingleInstance::SingleInstance(QString name, QObject* parent)
    : QObject(parent), m_name(std::move(name)), m_pipe(platform::instanceSocketName(m_name))
{
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::receive);
}

SingleInstance::~SingleInstance() = default;

QString SingleInstance::appName()
{
    return branding::organization() + u'-' + branding::executable();
}

bool SingleInstance::first()
{
    return m_lock.acquire(m_name);
}

bool SingleInstance::handOver(const QString& path) const
{
    QLocalSocket socket;
    socket.connectToServer(m_pipe);
    if (!socket.waitForConnected(kWaitMs)) {
        qCWarning(lcUi).noquote() << "Could not reach the running app:" << socket.errorString();
        return false;
    }
    // The path, then the end of the message: the socket closing.
    socket.write(path.toUtf8());
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(kWaitMs)) {
            qCWarning(lcUi).noquote() << "Could not hand" << path << "to the running app:" << socket.errorString();
            return false;
        }
    }
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState && !socket.waitForDisconnected(kWaitMs)) {
        qCWarning(lcUi).noquote() << "The running app did not take" << path << ":" << socket.errorString();
        return false;
    }
    return true;
}

core::Result<void> SingleInstance::listen()
{
    // Only this Windows user may reach it (Windows' default lets everyone
    // and anonymous read a pipe).
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    // A socket file left by a run that crashed (Linux, macOS) is in the way;
    // this start holds the lock, so nobody else is listening on it. (Windows
    // pipes are not files: nothing to remove.)
    // A path the system cannot take, said as such (Qt says only "name error").
    if (auto usable = platform::checkLocalSocketName(m_pipe); !usable) return usable;
    QLocalServer::removeServer(m_pipe);
    if (m_server.listen(m_pipe)) return {};
    return core::fail(core::ErrorCode::SystemRefused,
                      u"Could not listen for later starts (%1): %2"_s.arg(m_pipe, m_server.errorString()));
}

void SingleInstance::receive()
{
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        const auto done = [this, socket] {
            const QString path = QString::fromUtf8(socket->readAll());
            socket->deleteLater();
            qCInfo(lcUi).noquote() << "Started again" << (path.isEmpty() ? u"(no setlist)"_s : u"with "_s + path);
            emit opened(path);
        };
        // The message ends when the other start closes the socket (maybe already).
        if (socket->state() == QLocalSocket::UnconnectedState) {
            done();
        } else {
            connect(socket, &QLocalSocket::disconnected, this, done);
        }
    }
}

} // namespace gigchain::ui
