#include "SingleInstance.h"

#include "gigchain/core/Branding.h"

#include <QLocalSocket>
#include <QLoggingCategory>

#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

constexpr int kWaitMs = 1000; // a running app answers in milliseconds

} // namespace

SingleInstance::SingleInstance(QString name, QObject* parent)
    : QObject(parent), m_name(std::move(name)),
      // Pipe names are machine-wide: each Windows user has an app of their own.
      m_pipe(m_name + u'-' + qEnvironmentVariable("USERNAME"))
{
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::receive);
}

SingleInstance::~SingleInstance()
{
    if (m_mutex != nullptr) CloseHandle(m_mutex);
}

QString SingleInstance::appName()
{
    return branding::organization() + u'-' + branding::executable();
}

bool SingleInstance::first()
{
    // "Local\": this Windows session (another user signed in has their own).
    const std::wstring name = (u"Local\\"_s + m_name).toStdWString();
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    const DWORD error = GetLastError();
    if (mutex == nullptr) {
        // Cannot tell: start as the first rather than not at all.
        qCWarning(lcUi).noquote() << "Could not check for a running app (CreateMutex failed, error" << error << ")";
        return true;
    }
    if (error == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return false;
    }
    m_mutex = mutex;
    return true;
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
