#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QtTest>

// A test's client of the live test control: one request, its answer (the
// event loop runs while it waits, so a control in the same thread answers).
class DevClient
{
public:
    explicit DevClient(const QString& name)
    {
        m_socket.connectToServer(name);
        m_connected = m_socket.waitForConnected(2000);
    }

    [[nodiscard]] bool connected() const { return m_connected; }
    QLocalSocket& socket() { return m_socket; }

    // Sends `line` as it is, then waits for one answer line (empty object: none came).
    QJsonObject sendRaw(const QByteArray& line, int timeoutMs = 5000)
    {
        m_socket.write(line);
        m_socket.flush();
        return answer(timeoutMs);
    }

    QJsonObject ask(const QString& cmd, const QJsonArray& args = {}, int timeoutMs = 5000)
    {
        const QJsonObject request{{QStringLiteral("cmd"), cmd}, {QStringLiteral("args"), args}};
        return sendRaw(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n', timeoutMs);
    }

    QJsonObject answer(int timeoutMs)
    {
        const bool came = QTest::qWaitFor([this] { return m_socket.canReadLine(); }, timeoutMs);
        if (!came) return {};
        return QJsonDocument::fromJson(m_socket.readLine()).object();
    }

private:
    QLocalSocket m_socket;
    bool m_connected = false;
};
