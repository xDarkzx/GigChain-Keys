// The live test control against the real Main.qml and the demo engine, over a
// real local socket (docs/testing/live-test-control.md).
#include "DevClient.h"
#include "DevControl.h"

#include "OfficialArtwork.h"
#include "Session.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/FileLog.h"
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
        QVERIFY(core::FileLog::install(m_dir->filePath(u"app.log"_s)).has_value()); // (log and wait read it)
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
        addUiCommands(*m_control);
        addMidiCommands(*m_control);
        addMeasureCommands(*m_control);
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
        core::FileLog::uninstall();
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

    // ---- the screen ----

    void theSettingsDialogIsDrivenFromOutside()
    {
        engine::MidiSetup setup;
        setup.enabled = {u"KeyLab mkII 88 MIDI 40:0"_s};
        setup.configured = true;
        QVERIFY(m_engine->setMidiSetup(setup).has_value());
        DevClient client(m_name);
        QVERIFY(client.ask(u"ui-call"_s, {u"settingsDialog"_s, u"open"_s}).value(u"ok"_s).toBool());
        QVERIFY(client.ask(u"ui-set"_s, {u"settingsDialog"_s, u"page"_s, 2}).value(u"ok"_s).toBool());
        QTest::qWait(300);
        const QJsonArray tree = client.ask(u"ui-tree"_s, {u"settingsDialog"_s}).value(u"items"_s).toArray();
        const auto name = std::ranges::find_if(tree, [](const QJsonValue& v) { return v.toObject().value(u"name"_s) == u"midiName0"_s; });
        QVERIFY2(name != tree.end(), "the MIDI page's first input is not in the tree");
        QCOMPARE(name->toObject().value(u"text"_s).toString(), u"KeyLab mkII 88 MIDI 40:0"_s);
        const QString picture = m_dir->filePath(u"settings.png"_s);
        QVERIFY(client.ask(u"shot"_s, {picture}).value(u"ok"_s).toBool());
        QVERIFY(QFileInfo(picture).size() > 1000);
        const QString part = m_dir->filePath(u"name.png"_s);
        QVERIFY(client.ask(u"shot"_s, {part, u"midiName0"_s}).value(u"ok"_s).toBool());
        QVERIFY(QImage(part).width() < QImage(picture).width()); // just the item
        QVERIFY(client.ask(u"ui-call"_s, {u"settingsDialog"_s, u"close"_s}).value(u"ok"_s).toBool());
    }

    void aClickPressesTheControl()
    {
        window()->resize(1440, 1000);
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        DevClient client(m_name);
        const QJsonObject clicked = client.ask(u"ui-click"_s, {u"settingsButton"_s});
        QVERIFY2(clicked.value(u"ok"_s).toBool(), qPrintable(clicked.value(u"error"_s).toString()));
        QTRY_VERIFY(client.ask(u"ui-get"_s, {u"settingsDialog"_s, u"opened"_s}).value(u"value"_s).toBool());
        QVERIFY(!client.ask(u"ui-click"_s, {u"noSuchThing"_s}).value(u"ok"_s).toBool());
    }

    // Review focus 5: a control behind an open dialog is said to be covered, not pressed.
    void aCoveredOrHiddenItemIsSaid()
    {
        window()->resize(1440, 1000);
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        DevClient client(m_name);
        QVERIFY(client.ask(u"ui-call"_s, {u"settingsDialog"_s, u"open"_s}).value(u"ok"_s).toBool());
        QTRY_VERIFY(client.ask(u"ui-get"_s, {u"settingsDialog"_s, u"opened"_s}).value(u"value"_s).toBool());
        const QJsonObject covered = client.ask(u"ui-click"_s, {u"undoButton"_s});
        QVERIFY(!covered.value(u"ok"_s).toBool());
        QVERIFY2(covered.value(u"error"_s).toString().contains(u"covered"_s), qPrintable(covered.value(u"error"_s).toString()));
        QVERIFY(client.ask(u"key"_s, {u"Escape"_s}).value(u"ok"_s).toBool()); // closes it
        QTRY_VERIFY(!client.ask(u"ui-get"_s, {u"settingsDialog"_s, u"opened"_s}).value(u"value"_s).toBool());
    }

    void hoverShowsATooltip()
    {
        const QString longName = u"A MIDI input whose name is far too long to fit on one line of the Settings window, "
                                 u"even on a line of its own, so it shows in full when pointed at 40:0"_s;
        engine::MidiSetup setup;
        setup.enabled = {longName};
        setup.configured = true;
        QVERIFY(m_engine->setMidiSetup(setup).has_value());
        DevClient client(m_name);
        QVERIFY(client.ask(u"resize"_s, {880, 640}).value(u"ok"_s).toBool());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QVERIFY(client.ask(u"ui-call"_s, {u"settingsDialog"_s, u"open"_s}).value(u"ok"_s).toBool());
        QVERIFY(client.ask(u"ui-set"_s, {u"settingsDialog"_s, u"page"_s, 2}).value(u"ok"_s).toBool());
        QTest::qWait(300);
        const QJsonObject hovered = client.ask(u"ui-hover"_s, {u"midiName0"_s});
        QVERIFY2(hovered.value(u"ok"_s).toBool(), qPrintable(hovered.value(u"error"_s).toString()));
        const auto shown = [&client, &longName] {
            const QJsonArray tree = client.ask(u"ui-tree"_s, {}).value(u"items"_s).toArray();
            return std::ranges::count_if(tree, [&longName](const QJsonValue& v) { return v.toObject().value(u"text"_s) == longName; }) >= 2;
        };
        QTRY_VERIFY_WITH_TIMEOUT(shown(), 3000); // the name, and its tooltip
    }

    // ---- MIDI, measuring, what the app said ----

    void aNoteReachesTheEngineAsFromAKeyboard()
    {
        DevClient client(m_name);
        QVERIFY(client.ask(u"note"_s, {60, 100}).value(u"ok"_s).toBool());
        QTRY_VERIFY(client.ask(u"keys"_s).value(u"down"_s).toArray().contains(QJsonObject{{u"key"_s, 60}, {u"velocity"_s, 100}}));
        QVERIFY(client.ask(u"note"_s, {60, 0}).value(u"ok"_s).toBool());
        QTRY_VERIFY(client.ask(u"keys"_s).value(u"down"_s).toArray().isEmpty());
        QVERIFY(client.ask(u"cc"_s, {64, 127}).value(u"ok"_s).toBool());
        QVERIFY(client.ask(u"midi"_s, {u"B0 40 00"_s}).value(u"ok"_s).toBool());
        const QJsonObject bad = client.ask(u"midi"_s, {u"GG"_s});
        QVERIFY(!bad.value(u"ok"_s).toBool());
        QVERIFY(!client.ask(u"note"_s, {200, 100}).value(u"ok"_s).toBool());
    }

    void aPlayScriptKeepsItsTiming()
    {
        DevClient client(m_name);
        const QJsonObject played = client.ask(u"play"_s, {u"on 64 90; wait 150; off 64"_s});
        QVERIFY2(played.value(u"ok"_s).toBool(), qPrintable(played.value(u"error"_s).toString()));
        QVERIFY2(played.value(u"ms"_s).toInt() >= 150, qPrintable(QString::number(played.value(u"ms"_s).toInt())));
        QTRY_VERIFY(client.ask(u"keys"_s).value(u"down"_s).toArray().isEmpty());
        QVERIFY(!client.ask(u"play"_s, {u"on 64 90; jump"_s}).value(u"ok"_s).toBool());
    }

    void levelsAreMeasuredOverTime()
    {
        DevClient client(m_name);
        const QJsonObject level = client.ask(u"level"_s, {200});
        QVERIFY2(level.value(u"ok"_s).toBool(), qPrintable(level.value(u"error"_s).toString()));
        QVERIFY(level.contains(u"peak"_s) && level.contains(u"rms"_s) && level.contains(u"peakDb"_s));
        QVERIFY(level.value(u"ms"_s).toInt() >= 200);
        QVERIFY(client.ask(u"cpu"_s, {100}).contains(u"highest"_s));
        QVERIFY(client.ask(u"process"_s).value(u"memoryMb"_s).toDouble() > 1.0);
        QVERIFY(!client.ask(u"level"_s, {200, u"no-such-channel"_s}).value(u"ok"_s).toBool());
    }

    void waitFindsALineOrSaysItNeverCame()
    {
        DevClient client(m_name);
        const QString mark = client.ask(u"mark"_s).value(u"mark"_s).toString();
        QVERIFY(!mark.isEmpty());
        QVERIFY(client.ask(u"midi"_s, {u"90 3C 64"_s, u"Backup Keys"_s}).value(u"ok"_s).toBool()); // the demo engine logs it
        const QJsonObject found = client.ask(u"wait"_s, {u"Played in Backup Keys"_s, 3000, mark});
        QVERIFY2(found.value(u"ok"_s).toBool(), qPrintable(found.value(u"error"_s).toString()));
        QVERIFY(found.value(u"line"_s).toString().contains(u"Backup Keys"_s));
        QVERIFY(client.ask(u"log"_s, {mark, u"Played in"_s}).value(u"lines"_s).toArray().size() >= 1);
        const QJsonObject never = client.ask(u"wait"_s, {u"nothing like this at all"_s, 200, mark});
        QVERIFY(!never.value(u"ok"_s).toBool());
        QVERIFY(never.value(u"error"_s).toString().contains(u"200 ms"_s));
    }

    void noticesShowWhatThePlayerSaw()
    {
        DevClient client(m_name);
        const QString mark = client.ask(u"mark"_s).value(u"mark"_s).toString();
        m_session->document().reportMessage(u"Something the player should see"_s);
        const QJsonArray notices = client.ask(u"notices"_s, {mark}).value(u"notices"_s).toArray();
        QVERIFY(std::ranges::any_of(notices, [](const QJsonValue& v) { return v.toObject().value(u"text"_s).toString().contains(u"Something the player"_s); }));
        QVERIFY(client.ask(u"wait"_s, {u"Something the player"_s, 1000, mark}).value(u"ok"_s).toBool());
    }

    void recordingSaysWhyItCannot()
    {
        DevClient client(m_name);
        const QJsonObject recorded = client.ask(u"record"_s, {1, m_dir->filePath(u"take.wav"_s)});
        QVERIFY(!recorded.value(u"ok"_s).toBool()); // the demo engine has no audio to record
        QVERIFY(!recorded.value(u"error"_s).toString().isEmpty());
        QVERIFY(!client.ask(u"record"_s, {500, m_dir->filePath(u"long.wav"_s)}).value(u"ok"_s).toBool()); // at most 120 s
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
