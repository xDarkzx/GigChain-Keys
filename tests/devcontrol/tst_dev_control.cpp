// The live test control against the real Main.qml and the demo engine, over a
// real local socket (docs/testing/live-test-control.md).
#include "DevClient.h"
#include "DevControl.h"

#include "OfficialArtwork.h"
#include "Session.h"

#include "gigchain/core/Branding.h"
#include "gigchain/engine/FakeEngineFactory.h"

#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

#include <memory>

using namespace gigchain;
using namespace gigchain::devcontrol;
using namespace Qt::StringLiterals;

class TestDevControl : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<engine::IEngine> m_engine;
    std::unique_ptr<ui::Session> m_session;
    std::unique_ptr<QQmlApplicationEngine> m_qml;
    std::unique_ptr<DevControl> m_control;
    QString m_name;

    [[nodiscard]] QQuickWindow* window() const { return qobject_cast<QQuickWindow*>(m_qml->rootObjects().value(0)); }

    DevControl::Context context(const QString& name) const
    {
        return DevControl::Context{.engine = m_engine.get(),
                                   .objects = m_session->initialProperties(),
                                   .window = window(),
                                   .logFile = m_dir->filePath(u"app.log"_s),
                                   .dataFolder = m_dir->path(),
                                   .socketName = name};
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle(u"Basic"_s);
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"s.ini"_s), QSettings::IniFormat);
        m_settings->setValue(u"community/welcomed"_s, true);
        m_settings->setValue(u"community/lastVersion"_s, branding::version());
        m_engine = engine::createFakeEngine();
        m_session = std::make_unique<ui::Session>(*m_engine, *m_settings);
        m_session->document().newSetlist();
        QVERIFY(m_session->document().addSong());
        m_qml = std::make_unique<QQmlApplicationEngine>();
        ui::PluginIconProvider::install(*m_qml);
        m_qml->setInitialProperties(m_session->initialProperties());
        m_qml->loadFromModule(u"GigChain.Ui"_s, u"Main"_s);
        QVERIFY(window() != nullptr);
        m_name = u"gck-test-"_s + QUuid::createUuid().toString(QUuid::Id128).left(12);
        m_control = std::make_unique<DevControl>(context(m_name));
        addAppCommands(*m_control);
        const auto listening = m_control->listen();
        QVERIFY2(listening.has_value(), listening ? "" : qPrintable(listening.error().message));
    }

    void cleanup()
    {
        m_control.reset();
        m_qml.reset();
        m_session.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    // Lock 2: even in a dev build it listens only when the app is started with --dev-control.
    void nothingListensWithoutTheFlag()
    {
        const QString other = m_name + u"-flag"_s;
        QVERIFY(startIfAsked({u"app"_s}, context(other)) == nullptr);
        QVERIFY(!DevClient(other).connected());
        const auto started = startIfAsked({u"app"_s, u"--dev-control"_s}, context(other));
        QVERIFY(started != nullptr);
        QVERIFY(DevClient(other).connected());
    }

    void aRequestIsAnsweredAndLogged()
    {
        DevClient client(m_name);
        QVERIFY(client.connected());
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(u"Dev control: .*help"_s));
        const QJsonObject answer = client.ask(u"help"_s);
        QVERIFY(answer.value(u"ok"_s).toBool());
        const QJsonArray commands = answer.value(u"commands"_s).toArray();
        QVERIFY(std::ranges::any_of(commands, [](const QJsonValue& c) { return c.toObject().value(u"name"_s) == u"status"_s; }));
    }

    void unknownAndBadRequestsSayWhy()
    {
        DevClient client(m_name);
        const QJsonObject unknown = client.ask(u"jump"_s);
        QVERIFY(!unknown.value(u"ok"_s).toBool());
        QVERIFY2(unknown.value(u"error"_s).toString().contains(u"help"_s), qPrintable(unknown.value(u"error"_s).toString()));
        const QJsonObject bad = client.sendRaw("not json\n");
        QVERIFY(!bad.value(u"ok"_s).toBool());
        QVERIFY(bad.value(u"error"_s).toString().contains(u"JSON"_s));
    }

    // Review focus 3: an endless line is cut, its connection closed; others go on.
    void aTooLongLineClosesTheConnection()
    {
        DevClient client(m_name);
        const QJsonObject answer = client.sendRaw(QByteArray(70 * 1024, 'x'));
        QVERIFY(!answer.value(u"ok"_s).toBool());
        QVERIFY(answer.value(u"error"_s).toString().contains(u"long"_s));
        QTRY_VERIFY(client.socket().state() == QLocalSocket::UnconnectedState);
        QVERIFY(DevClient(m_name).ask(u"help"_s).value(u"ok"_s).toBool());
    }

    // Review focus 2: a client gone before its answer breaks nothing.
    void aClientLeavingMidCommandBreaksNothing()
    {
        bool answered = false;
        m_control->add(u"slow"_s, u"slow: answers after half a second"_s, [&answered](const DevRequest&, const Reply& reply) {
            QTimer::singleShot(500, [reply, &answered] {
                answered = true;
                reply.ok();
            });
        });
        {
            DevClient leaving(m_name);
            leaving.socket().write("{\"cmd\":\"slow\"}\n");
            leaving.socket().flush();
            QTest::qWait(100);
        } // gone
        QTRY_VERIFY(answered);
        QVERIFY(DevClient(m_name).ask(u"help"_s).value(u"ok"_s).toBool());
    }

    // Review focus 4: a name in use is said, and nothing else breaks.
    void aTakenNameIsSaidNotFatal()
    {
        DevControl second(context(m_name));
        const auto listening = second.listen();
        QVERIFY(!listening.has_value());
        QVERIFY2(listening.error().message.contains(m_name), qPrintable(listening.error().message));
        QVERIFY(DevClient(m_name).ask(u"help"_s).value(u"ok"_s).toBool());
    }

    // The app's own actions, through the controllers the screen uses.
    void callReachesTheControllers()
    {
        DevClient client(m_name);
        QVERIFY(client.ask(u"call"_s, {u"document"_s, u"addPatch"_s, 0}).value(u"result"_s).toBool());
        QVERIFY(client.ask(u"call"_s, {u"document"_s, u"renamePatch"_s, 0, 1, u"Strings"_s}).value(u"result"_s).toBool());
        QVERIFY(client.ask(u"call"_s, {u"document"_s, u"selectPatch"_s, 0, 1}).value(u"ok"_s).toBool());
        QCOMPARE(client.ask(u"get"_s, {u"document"_s, u"currentPatchName"_s}).value(u"value"_s).toString(), u"Strings"_s);
        const QJsonObject wrong = client.ask(u"call"_s, {u"document"_s, u"renamePatch"_s, 0});
        QVERIFY(!wrong.value(u"ok"_s).toBool());
        QVERIFY2(wrong.value(u"error"_s).toString().contains(u"renamePatch("_s), qPrintable(wrong.value(u"error"_s).toString()));
        const QJsonObject methods = client.ask(u"methods"_s, {u"document"_s});
        QVERIFY(methods.value(u"methods"_s).toArray().contains(u"bool renamePatch(int song, int patch, QString name)"_s));
        // Nested objects by path; a model's rows.
        QVERIFY(client.ask(u"get"_s, {u"document.notifications"_s, u"objectName"_s}).value(u"ok"_s).toBool());
        QVERIFY(client.ask(u"model"_s, {u"setlistModel"_s}).value(u"rows"_s).toArray().size() >= 1);
        const QJsonObject status = client.ask(u"status"_s);
        QCOMPARE(status.value(u"sound"_s).toString(), u"Strings"_s);
        QVERIFY(status.contains(u"channels"_s) && status.contains(u"blockedPlugins"_s) && status.contains(u"audio"_s));
    }

    void guardBlockSwitchesAPluginOff()
    {
        DevClient client(m_name);
        const QString plugin = u"C:/Plugins/Test Plugin A.vst3"_s;
        QVERIFY(client.ask(u"guard-block"_s, {plugin}).value(u"ok"_s).toBool());
        QVERIFY(client.ask(u"read"_s, {u"blocked"_s}).value(u"text"_s).toString().contains(u"Test Plugin A"_s));
        QVERIFY(client.ask(u"paths"_s).value(u"log"_s).toString().endsWith(u"app.log"_s));
        QVERIFY(!client.ask(u"read"_s, {u"elsewhere"_s}).value(u"ok"_s).toBool());
    }
};

QTEST_MAIN(TestDevControl)
#include "tst_dev_control.moc"
