// Loads the real Main.qml with the demo engine and fails on any QML warning.
#include "Session.h"

#include "openstage/engine/FakeEngineFactory.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(OpenStage_UiPlugin)

using namespace openstage;
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
        m_qml->loadFromModule(u"OpenStage.Ui"_s, u"Main"_s);
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

    void spaceNavigatesButNotWhileTyping()
    {
        QVERIFY(m_session->document().addPatch(0));
        QVERIFY(m_session->document().selectPatch(0, 0));
        QQuickWindow* w = window();
        QVERIFY(w != nullptr);
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));

        auto* field = m_qml->rootObjects().value(0)->findChild<QQuickItem*>(u"inspectorPatchName"_s);
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
