// Loads the real Main.qml with the demo engine and fails on any QML warning.
#include "Session.h"
#include "StartupProgress.h"

#include "gigchain/engine/FakeEngineFactory.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(GigChain_UiPlugin)

using namespace gigchain;
using namespace Qt::StringLiterals;

class TestQmlSmoke : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<engine::IEngine> m_engine;
    std::unique_ptr<ui::Session> m_session;
    std::unique_ptr<QQmlApplicationEngine> m_qml;
    QStringList m_warnings;

    QQuickWindow* window() const { return qobject_cast<QQuickWindow*>(m_qml->rootObjects().value(0)); }

    void settle()
    {
        QTest::qWait(150);
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(u'\n')));
    }

private slots:
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"s.ini"_s), QSettings::IniFormat);
        m_engine = engine::createFakeEngine();
        m_session = std::make_unique<ui::Session>(*m_engine, *m_settings);
        m_qml = std::make_unique<QQmlApplicationEngine>();
        m_warnings.clear();
        connect(m_qml.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& warnings) {
            for (const QQmlError& w : warnings) m_warnings << w.toString();
        });
        m_qml->setInitialProperties(m_session->initialProperties());
        m_qml->loadFromModule(u"GigChain.Ui"_s, u"Main"_s);
    }

    void cleanup()
    {
        m_qml.reset(); // QML goes first: it binds to the session's objects
        m_session.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    void loadsWithoutWarnings()
    {
        QCOMPARE(m_qml->rootObjects().size(), 1);
        settle();
    }

    void performModeToggles()
    {
        QObject* root = m_qml->rootObjects().value(0);
        QVERIFY(root->setProperty("performMode", true));
        settle();
        auto* name = root->findChild<QObject*>(u"performPatchName"_s);
        QVERIFY(name != nullptr);
        QCOMPARE(name->property("text").toString(), u"Patch 1"_s);
        QVERIFY(root->setProperty("performMode", false));
        settle();
    }

    void addingAChannelShowsAStrip()
    {
        QVERIFY(m_session->document().addChannel(u"fake.grand-piano"_s, u"Grand Piano"_s));
        QVERIFY(m_session->document().addEffect(0, u"fake.reverb"_s, u"Reverb"_s));
        settle();
        auto* strips = m_qml->rootObjects().value(0)->findChild<QObject*>(u"mixerStrips"_s);
        QVERIFY(strips != nullptr);
        QCOMPARE(strips->property("count").toInt(), 1);
        auto* plugins = m_qml->rootObjects().value(0)->findChild<QObject*>(u"pluginList"_s);
        QVERIFY(plugins != nullptr);
        QVERIFY(plugins->property("count").toInt() > 0);
    }

    void settingsOpensEveryPage()
    {
        auto* dialog = m_qml->rootObjects().value(0)->findChild<QObject*>(u"settingsDialog"_s);
        QVERIFY(dialog != nullptr);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        for (int page = 0; page < 3; ++page) {
            QVERIFY(dialog->setProperty("page", page));
            settle(); // no warnings from any page
        }
        auto* device = dialog->findChild<QObject*>(u"deviceBox"_s);
        QVERIFY(device != nullptr);
        QCOMPARE(device->property("currentText").toString(), u"Demo output"_s); // loaded from the engine
        auto* ok = dialog->findChild<QObject*>(u"settingsOk"_s);
        QVERIFY(ok != nullptr);
        QVERIFY(QMetaObject::invokeMethod(ok, "clicked"));
        settle();
        QVERIFY(!dialog->property("visible").toBool()); // nothing failed: closed
    }

    void splashShowsStartupProgress()
    {
        ui::StartupProgress startup;
        QQmlApplicationEngine splash;
        QStringList warnings;
        connect(&splash, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& list) {
            for (const QQmlError& w : list) warnings << w.toString();
        });
        splash.setInitialProperties({{u"startup"_s, QVariant::fromValue(&startup)}});
        splash.loadFromModule(u"GigChain.Ui"_s, u"Splash"_s);
        QCOMPARE(splash.rootObjects().size(), 1);
        startup.report(u"Scanning plugins (3 of 63)"_s, u"Piano V2"_s, 2.0 / 63.0);
        auto* step = splash.rootObjects().value(0)->findChild<QObject*>(u"splashStep"_s);
        QVERIFY(step != nullptr);
        QCOMPARE(step->property("text").toString(), u"Scanning plugins (3 of 63)"_s);
        auto* detail = splash.rootObjects().value(0)->findChild<QObject*>(u"splashDetail"_s);
        QVERIFY(detail != nullptr);
        QCOMPARE(detail->property("text").toString(), u"Piano V2"_s); // the plugin being scanned
        auto* image = splash.rootObjects().value(0)->findChild<QObject*>(u"splashImage"_s);
        QVERIFY(image != nullptr);
        QCOMPARE(image->property("status").toInt(), 1); // Image.Ready: the branding picture is built in
        QVERIFY(splash.rootObjects().value(0)->property("flags").toInt() & Qt::WindowStaysOnTopHint); // in front

        startup.finish(300); // loaded early: "Ready", the bar glides to full
        QCOMPARE(step->property("text").toString(), u"Ready"_s);
        QCOMPARE(startup.progress(), 1.0);
        QCOMPARE(startup.glideMs(), 300);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
    }

    void spaceNavigatesButNotWhileTyping()
    {
        QVERIFY(m_session->document().addPatch(0));
        QVERIFY(m_session->document().selectPatch(0, 0));
        QQuickWindow* w = window();
        QVERIFY(w != nullptr);
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));

        auto* tabs = m_qml->rootObjects().value(0)->findChild<QObject*>(u"sidePanelTabs"_s);
        QVERIFY(tabs != nullptr);
        tabs->setProperty("currentIndex", 1); // Plugins tab
        QTest::qWait(20);
        auto* field = m_qml->rootObjects().value(0)->findChild<QQuickItem*>(u"pluginSearch"_s);
        QVERIFY(field != nullptr);
        field->forceActiveFocus();
        QTest::keyClick(w, Qt::Key_Space);
        QCOMPARE(m_session->document().patchIndex(), 0); // typed a space, did not navigate

        QTest::keyClick(w, Qt::Key_Return); // finish editing: focus leaves the field
        QVERIFY(!field->hasActiveFocus());
        QTest::keyClick(w, Qt::Key_Space);
        QCOMPARE(m_session->document().patchIndex(), 1);
        settle();
    }
};

QTEST_MAIN(TestQmlSmoke)
#include "tst_qml_smoke.moc"
