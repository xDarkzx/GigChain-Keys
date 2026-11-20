#include "DevControl.h"

#include "DevLog.h"

#include <QJsonArray>
#include <QLocalSocket>

using namespace Qt::StringLiterals;

namespace gigchain::devcontrol {

Q_LOGGING_CATEGORY(lcDev, "gigchain.devcontrol")

Reply::Reply(QLocalSocket* socket, QString cmd) : m_state(std::make_shared<State>(State{.socket = socket, .cmd = std::move(cmd), .done = false, .took = {}}))
{
    m_state->took.start();
}

void Reply::ok(const QJsonObject& fields) const
{
    if (m_state->done) return;
    m_state->done = true;
    if (!m_state->socket) return; // (its client went away)
    QJsonObject answer = fields;
    answer.insert(u"ms"_s, m_state->took.elapsed());
    m_state->socket->write(okLine(answer));
    m_state->socket->flush();
}

void Reply::fail(const QString& why) const
{
    if (m_state->done) return;
    m_state->done = true;
    qCWarning(lcDev).noquote() << "Dev control:" << m_state->cmd << "failed:" << why;
    if (!m_state->socket) return;
    m_state->socket->write(errorLine(why));
    m_state->socket->flush();
}

bool Reply::alive() const
{
    return !m_state->done && m_state->socket && m_state->socket->state() == QLocalSocket::ConnectedState;
}

DevControl::DevControl(Context context, QObject* parent) : QObject(parent), m_context(std::move(context))
{
    connect(&m_server, &QLocalServer::newConnection, this, &DevControl::onConnection);
    add(u"help"_s, u"help: every command and how to call it"_s, [this](const DevRequest&, const Reply& reply) {
        QJsonArray commands;
        for (const auto& [name, entry] : m_commands) commands.append(QJsonObject{{u"name"_s, name}, {u"usage"_s, entry.first}});
        reply.ok({{u"commands"_s, commands}});
    });
}

DevControl::~DevControl() = default;

core::Result<void> DevControl::listen()
{
    const QString name = m_context.socketName.isEmpty() ? defaultSocketName() : m_context.socketName;
    const auto refused = [&name](const QString& why) {
        return core::fail(core::ErrorCode::SystemRefused, u"The live test control could not listen on %1: %2"_s.arg(name, why));
    };
    // Something answering there already: another copy (Windows would let both listen on one name).
    {
        QLocalSocket probe;
        probe.connectToServer(name);
        if (probe.waitForConnected(300)) return refused(u"another running copy of the app uses it"_s);
    }
    m_server.setSocketOptions(QLocalServer::UserAccessOption); // this user only; never the network
    if (!m_server.listen(name)) {
        // A socket file left behind by a copy that crashed (not Windows): nothing answers on it, so it is taken over.
        if (!QLocalServer::removeServer(name) || !m_server.listen(name)) return refused(m_server.errorString());
    }
    qCWarning(lcDev).noquote() << "Live test control listening on" << m_server.fullServerName() << "(a developer build: never released)";
    return {};
}

void DevControl::add(const QString& cmd, const QString& usage, Handler handler)
{
    m_commands.insert_or_assign(cmd, std::pair{usage, std::move(handler)});
}

QString DevControl::defaultSocketName()
{
    return socketName();
}

void DevControl::onConnection()
{
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadable(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void DevControl::onReadable(QLocalSocket* socket)
{
    while (socket->canReadLine()) {
        QByteArray line = socket->readLine(kMaxLine + 2);
        if (!line.endsWith('\n')) { // longer than any request may be
            socket->write(errorLine(u"The request is too long (at most %1 bytes): the connection is closed"_s.arg(kMaxLine)));
            socket->flush();
            socket->disconnectFromServer();
            return;
        }
        line.chop(1);
        if (line.endsWith('\r')) line.chop(1);
        if (!line.trimmed().isEmpty()) dispatch(socket, line);
    }
    // A line with no end in sight: refused before it fills the memory.
    if (socket->bytesAvailable() > kMaxLine) {
        socket->write(errorLine(u"The request is too long (at most %1 bytes): the connection is closed"_s.arg(kMaxLine)));
        socket->flush();
        socket->disconnectFromServer();
    }
}

void DevControl::dispatch(QLocalSocket* socket, const QByteArray& line)
{
    qCInfo(lcDev).noquote() << "Dev control:" << QString::fromUtf8(line.left(500));
    const auto request = parseRequest(line);
    if (!request) {
        Reply(socket, u"(unreadable)"_s).fail(request.error().message);
        return;
    }
    const auto command = m_commands.find(request->cmd);
    const Reply reply(socket, request->cmd);
    if (command == m_commands.end()) {
        reply.fail(u"No such command: %1 (help lists them)"_s.arg(request->cmd));
        return;
    }
    command->second.second(*request, reply);
}

std::unique_ptr<DevControl> startIfAsked(const QStringList& arguments, DevControl::Context context)
{
    if (!arguments.contains(u"--dev-control"_s)) return nullptr;
    auto control = std::make_unique<DevControl>(std::move(context));
    addAppCommands(*control);
    addUiCommands(*control);
    addMidiCommands(*control);
    addMeasureCommands(*control);
    if (auto listening = control->listen(); !listening) {
        qCWarning(lcDev).noquote() << listening.error().message; // (the app goes on without it)
        return nullptr;
    }
    return control;
}

} // namespace gigchain::devcontrol
