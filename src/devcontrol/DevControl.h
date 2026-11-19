#pragma once

#include "DevProtocol.h"

#include "gigchain/core/Error.h"

#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>

class QLocalSocket;
class QQuickWindow;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::devcontrol {

// The answer to one request: given at most once; a client that has gone
// makes it a no-op (a long command outliving its client breaks nothing).
// Copies share it (a command may answer later, from a timer).
class Reply
{
public:
    Reply(QLocalSocket* socket, QString cmd);
    void ok(const QJsonObject& fields = {}) const;
    void fail(const QString& why) const;
    [[nodiscard]] bool alive() const;

private:
    struct State
    {
        QPointer<QLocalSocket> socket;
        QString cmd;
        bool done = false;
    };
    std::shared_ptr<State> m_state;
};

// The live test control (docs/testing/live-test-control.md): drives the
// running app from a terminal. A developer build only (GIGCHAIN_DEV_CONTROL),
// started only with --dev-control (startIfAsked). Listens on this user's local
// socket; one JSON request per line, one JSON answer line each. Main thread.
class DevControl : public QObject
{
    Q_OBJECT

public:
    struct Context
    {
        engine::IEngine* engine = nullptr;
        QVariantMap objects;            // the screen's controllers by name (Session::initialProperties)
        QQuickWindow* window = nullptr; // the main window
        QString logFile;                // the app's log
        QString dataFolder;             // AppLocalData: settings, plugin guard, crash reports
        QString socketName;             // where it listens
    };
    using Handler = std::function<void(const DevRequest&, const Reply&)>;

    explicit DevControl(Context context, QObject* parent = nullptr);
    ~DevControl() override;
    DevControl(const DevControl&) = delete;
    DevControl& operator=(const DevControl&) = delete;
    DevControl(DevControl&&) = delete;
    DevControl& operator=(DevControl&&) = delete;

    // Starts listening; an error names the socket and why (another app using it).
    core::Result<void> listen();
    // A command: its name, a one-line usage for `help`, and what it does.
    void add(const QString& cmd, const QString& usage, Handler handler);
    [[nodiscard]] const Context& context() const { return m_context; }
    // The socket name the app listens on: the app's name + "-dev-control".
    [[nodiscard]] static QString defaultSocketName();

private:
    void onConnection();
    void onReadable(QLocalSocket* socket);
    void dispatch(QLocalSocket* socket, const QByteArray& line);

    Context m_context;
    QLocalServer m_server;
    std::map<QString, std::pair<QString, Handler>> m_commands;
};

// Groups of commands (each in its own file).
void addAppCommands(DevControl& control);

// main(): a control listening on defaultSocketName() when `arguments` has
// --dev-control (with every command group added); nullptr otherwise.
[[nodiscard]] std::unique_ptr<DevControl> startIfAsked(const QStringList& arguments, DevControl::Context context);

} // namespace gigchain::devcontrol
