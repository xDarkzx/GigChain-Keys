// Loads the real Main.qml with the demo engine and fails on any QML warning.
#include "Notifications.h"
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

// List delegates are children in the visual tree, not the QObject tree, so
// findChild() cannot see them.
QQuickItem* findItem(QQuickItem* item, const QString& name)
{
    if (item == nullptr) return nullptr;
    if (item->objectName() == name) return item;
    for (QQuickItem* child : item->childItems()) {
        if (QQuickItem* found = findItem(child, name)) return found;
    }
    return nullptr;
}

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

    // An item inside the first channel strip (delegates are not QObject
    // children of the window).
    QQuickItem* stripChild(const QString& name) const
    {
        auto* strips = window()->findChild<QObject*>(u"mixerStrips"_s);
        QQuickItem* strip = nullptr;
        if (strips == nullptr
            || !QMetaObject::invokeMethod(strips, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, strip), Q_ARG(int, 0))
            || strip == nullptr) {
            return nullptr;
        }
        return strip->findChild<QQuickItem*>(name);
    }

    QQuickItem* item(const QString& name) const
    {
        if (auto* found = window()->findChild<QQuickItem*>(name)) return found;
        return stripChild(name);
    }

    void click(const QString& name)
    {
        QQuickItem* target = item(name);
        QVERIFY2(target != nullptr, qPrintable(name));
        QTest::mouseClick(window(), Qt::LeftButton, {},
                          target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
        QTest::qWait(20);
    }

    // Clicks a value box, types, presses Enter.
    void type(const QString& name, const QString& text)
    {
        click(name);
        for (const QChar c : text) QTest::keyClick(window(), c.toLatin1()); // keyClicks is widgets-only
        QTest::keyClick(window(), Qt::Key_Return);
        QTest::qWait(20);
    }

private slots:
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"s.ini"_s), QSettings::IniFormat);
        m_engine = engine::createFakeEngine();
        m_session = std::make_unique<ui::Session>(*m_engine, *m_settings);
        m_session->document().newSetlist();
        QVERIFY(m_session->document().addSong());
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
        // The chart, big, on stage.
        QVERIFY(m_session->document().setSongChart(0, u"{comment: Verse}\n[C]Hello [G]world\n"_s));
        settle();
        auto* song = root->findChild<QObject*>(u"performSongName"_s);
        QVERIFY(song != nullptr);
        QCOMPARE(song->property("text").toString(), u"Song 1"_s);
        auto* chart = root->findChild<QQuickItem*>(u"performChart"_s);
        QVERIFY(chart != nullptr);
        QVERIFY(chart->property("contentHeight").toDouble() > 40); // the lines are there
        auto* panic = root->findChild<QObject*>(u"performPanic"_s);
        QVERIFY(panic != nullptr);
        QVERIFY(QMetaObject::invokeMethod(panic, "clicked"));
        const ui::Notifications& shown = *m_session->document().notifications();
        QVERIFY(shown.rowCount() > 0 && shown.text(shown.rowCount() - 1).contains(u"Panic"_s));
        QVERIFY(root->setProperty("performMode", false));
        settle();
    }

    // The master strip, used with the mouse and keyboard as a person would.
    void masterFaderDragsTypesAndMutes()
    {
        window()->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        auto* fader = window()->findChild<QQuickItem*>(u"masterFader"_s);
        QVERIFY(fader != nullptr);
        auto* slider = fader->findChild<QQuickItem*>(u"faderSlider"_s);
        QVERIFY(slider != nullptr);

        // Drag the fader cap down: the output gets quieter.
        const QPoint cap = slider->mapToScene(QPointF(slider->width() / 2, slider->height() * (1 - 60.0 / 72))).toPoint();
        QTest::mousePress(window(), Qt::LeftButton, {}, cap);
        for (int dy = 4; dy <= 40; dy += 4) QTest::mouseMove(window(), cap + QPoint(0, dy));
        QTest::mouseRelease(window(), Qt::LeftButton, {}, cap + QPoint(0, 40));
        QVERIFY2(m_engine->masterVolume() < -1.0, qPrintable(QString::number(m_engine->masterVolume())));

        // Click the readout, type 0, Enter: back to 0 dB, and the fader follows.
        type(u"masterVolumeReadout"_s, u"0"_s);
        QCOMPARE(m_engine->masterVolume(), 0.0);
        QCOMPARE(slider->property("value").toDouble(), 0.0);
        type(u"masterVolumeReadout"_s, u"-12.5"_s);
        QCOMPARE(m_engine->masterVolume(), -12.5);
        QCOMPARE(slider->property("value").toDouble(), -12.5);
        type(u"masterVolumeReadout"_s, u"loud"_s); // not a number: nothing changes
        QCOMPARE(m_engine->masterVolume(), -12.5);

        // The speaker button mutes everything, and unmutes.
        click(u"masterMuteButton"_s);
        QVERIFY(m_engine->masterMuted());
        click(u"masterMuteButton"_s);
        QVERIFY(!m_engine->masterMuted());
        settle();
    }

    void masterStripTakesEffectsAndShowsTheLimiter()
    {
        auto* bus = window()->property("masterBus").value<QObject*>();
        QVERIFY(bus != nullptr);
        bool added = false;
        QVERIFY(QMetaObject::invokeMethod(bus, "addEffect", Q_RETURN_ARG(bool, added), Q_ARG(QString, u"fake.reverb"_s),
                                          Q_ARG(QString, u"Reverb"_s)));
        QVERIFY(added);
        settle();
        auto* list = item(u"masterEffectList"_s);
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("count").toInt(), 1);
        QVERIFY(item(u"masterAddEffect"_s) != nullptr);
        QVERIFY(item(u"limiterLight"_s) != nullptr);
        auto* dialog = window()->findChild<QObject*>(u"settingsDialog"_s);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QVERIFY(dialog->setProperty("page", 1)); // Audio: the limiter's settings
        settle();
        QVERIFY(dialog->findChild<QObject*>(u"limiterCeiling"_s) != nullptr);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        settle();
    }

    void channelVolumeTypesAndPanDragsSideways()
    {
        QVERIFY(m_session->document().addChannel(u"fake.grand-piano"_s, u"Grand Piano"_s));
        window()->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        settle();

        type(u"volumeReadout"_s, u"-6"_s);
        QCOMPARE(m_session->document().currentPatch()->channels[0].volumeDb, -6.0);
        auto* channelSlider = stripChild(u"channelFader"_s)->findChild<QQuickItem*>(u"faderSlider"_s);
        QCOMPARE(channelSlider->property("value").toDouble(), -6.0);

        // Drag the pan knob to the right, then to the left.
        auto* knob = stripChild(u"panKnob"_s);
        const QPoint centre = knob->mapToScene(QPointF(knob->width() / 2, knob->height() / 2)).toPoint();
        QTest::mousePress(window(), Qt::LeftButton, {}, centre);
        for (int dx = 5; dx <= 40; dx += 5) QTest::mouseMove(window(), centre + QPoint(dx, 0));
        QTest::mouseRelease(window(), Qt::LeftButton, {}, centre + QPoint(40, 0));
        const double right = m_session->document().currentPatch()->channels[0].pan;
        QVERIFY2(right > 0.2, qPrintable(QString::number(right)));
        QTest::mousePress(window(), Qt::LeftButton, {}, centre);
        for (int dx = 5; dx <= 80; dx += 5) QTest::mouseMove(window(), centre - QPoint(dx, 0));
        QTest::mouseRelease(window(), Qt::LeftButton, {}, centre - QPoint(80, 0));
        QVERIFY(m_session->document().currentPatch()->channels[0].pan < -0.2);
        QTest::mouseDClick(window(), Qt::LeftButton, {}, centre); // double-click centres
        QCOMPARE(m_session->document().currentPatch()->channels[0].pan, 0.0);
        settle();
    }

    void manyEffectsScrollInsteadOfSqueezingTheFader()
    {
        QVERIFY(m_session->document().addChannel(u"fake.grand-piano"_s, u"Grand Piano"_s));
        for (int i = 0; i < 6; ++i) QVERIFY(m_session->document().addEffect(0, u"fake.reverb"_s, u"Reverb"_s));
        settle();
        auto* strips = m_qml->rootObjects().value(0)->findChild<QObject*>(u"mixerStrips"_s);
        QVERIFY(strips != nullptr);
        QQuickItem* strip = nullptr;
        QVERIFY(QMetaObject::invokeMethod(strips, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, strip), Q_ARG(int, 0)));
        QVERIFY(strip != nullptr);
        auto* list = strip->findChild<QObject*>(u"effectList"_s);
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("count").toInt(), 6);
        QVERIFY(list->property("height").toDouble() <= 4 * 23.0); // four shown, the rest scroll
        QVERIFY(list->property("interactive").toBool());
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

    // A message is shown in its level's colour, in its own window (above a
    // plugin's window), and goes away by itself; worse news stays longer.
    void notificationsShowInTheirColourAndGoAwayByThemselves()
    {
        auto* toasts = window()->findChild<QQuickWindow*>(u"notificationWindow"_s);
        QVERIFY(toasts != nullptr);
        QVERIFY(!toasts->isVisible()); // nothing to say yet
        ui::Notifications& n = *m_session->document().notifications();
        n.post(u"MIDI input connected: Impact GXP61"_s, ui::Notifications::Info);
        n.post(u"Could not load Broken Synth"_s, ui::Notifications::Error);
        QTRY_VERIFY2_WITH_TIMEOUT(toasts->isVisible(), qPrintable(m_warnings.join(u'\n')), 2000);
        QCOMPARE(toasts->transientParent(), window()); // stays with the main window
        auto* list = toasts->findChild<QObject*>(u"notificationList"_s);
        QVERIFY(list != nullptr);
        QTRY_COMPARE(list->property("count").toInt(), 2);
        auto colourOf = [list](int row) {
            QQuickItem* item = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
            return item != nullptr ? item->property("levelColour").value<QColor>() : QColor();
        };
        auto* theme = m_qml->singletonInstance<QObject*>("GigChain.Ui", "Theme");
        QVERIFY(theme != nullptr);
        QTRY_COMPARE(colourOf(0), theme->property("info").value<QColor>()); // once the rows are laid out
        QTRY_COMPARE(colourOf(1), theme->property("danger").value<QColor>());
        // The info goes first, by itself; the error is still there then.
        QTRY_COMPARE_WITH_TIMEOUT(n.rowCount(), 1, ui::Notifications::shownFor(ui::Notifications::Info) + 1000);
        QCOMPARE(n.level(0), ui::Notifications::Error);
        QTRY_COMPARE_WITH_TIMEOUT(n.rowCount(), 0, ui::Notifications::shownFor(ui::Notifications::Error) + 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!toasts->isVisible(), 2000);
        QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(u'\n')));
    }

    // The old red bar is gone: messages are notifications now.
    void thereIsNoRedBar() { QVERIFY(window()->findChild<QQuickItem*>(u"messageBanner"_s) == nullptr); }

    // Dropping an instrument on the mixer loads it and shows it.
    void droppingAnInstrumentOnTheMixerShowsTheInstrumentTab()
    {
        auto* tabs = window()->findChild<QObject*>(u"mainTabs"_s);
        auto* drop = window()->findChild<QObject*>(u"mixerDrop"_s);
        QVERIFY(tabs != nullptr);
        QVERIFY(drop != nullptr);
        tabs->setProperty("currentIndex", 0); // on the chart
        const QVariantMap payload{{u"pluginId"_s, u"fake.grand-piano"_s}, {u"name"_s, u"Grand Piano"_s}};
        QVERIFY(QMetaObject::invokeMethod(drop, "acceptDrop", Q_ARG(QVariant, payload)));
        settle();
        QCOMPARE(m_session->document().currentPatch()->channels.size(), std::size_t{1});
        QCOMPARE(tabs->property("currentIndex").toInt(), 1); // the Instrument tab
    }

    void settingsOpensEveryPage()
    {
        auto* dialog = m_qml->rootObjects().value(0)->findChild<QObject*>(u"settingsDialog"_s);
        QVERIFY(dialog != nullptr);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        for (int page = 0; page < 4; ++page) {
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
        startup.report(u"Scanning plugins (3 of 63)"_s, u"Test Plugin C"_s, 2.0 / 63.0);
        auto* step = splash.rootObjects().value(0)->findChild<QObject*>(u"splashStep"_s);
        QVERIFY(step != nullptr);
        QCOMPARE(step->property("text").toString(), u"Scanning plugins (3 of 63)"_s);
        auto* detail = splash.rootObjects().value(0)->findChild<QObject*>(u"splashDetail"_s);
        QVERIFY(detail != nullptr);
        QCOMPARE(detail->property("text").toString(), u"Test Plugin C"_s); // the plugin being scanned
        auto* image = splash.rootObjects().value(0)->findChild<QObject*>(u"splashImage"_s);
        QVERIFY(image != nullptr);
        QCOMPARE(image->property("status").toInt(), 1); // Image.Ready: the branding picture is built in
        QVERIFY(splash.rootObjects().value(0)->property("flags").toInt() & Qt::WindowStaysOnTopHint); // in front

        // Loaded early: while it stays up, the splash names each plugin the
        // scanner reported (made-up names here), the bar filling from the
        // left, then says "Ready". The position in the list is set directly
        // so the test does not depend on animation timing.
        startup.addPlugin(u"Test Plugin A"_s);
        startup.addPlugin(u"Test Plugin B"_s);
        startup.finish(600, u"Test line check"_s, u"Test ready"_s);
        QObject* root = splash.rootObjects().value(0);
        auto* fill = root->findChild<QObject*>(u"splashFill"_s);
        QVERIFY(fill != nullptr);
        QVERIFY(root->setProperty("playhead", 0.5));
        QCOMPARE(detail->property("text").toString(), u"Test Plugin A"_s);
        QCOMPARE(step->property("text").toString(), u"Test line check"_s);
        QCOMPARE(fill->property("fraction").toDouble(), 0.25);
        QVERIFY(root->setProperty("playhead", 1.5));
        QCOMPARE(detail->property("text").toString(), u"Test Plugin B"_s);
        QCOMPARE(fill->property("fraction").toDouble(), 0.75);
        QVERIFY(root->setProperty("playhead", 2.0));
        QCOMPARE(step->property("text").toString(), u"Test ready"_s);
        QCOMPARE(fill->property("fraction").toDouble(), 1.0);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
    }

    void instrumentCardShowsMakerDetailsAndFavourite()
    {
        QObject* root = m_qml->rootObjects().value(0);
        auto* tabs = root->findChild<QObject*>(u"sidePanelTabs"_s);
        QVERIFY(tabs != nullptr);
        tabs->setProperty("currentIndex", 1); // Instruments
        settle();
        QQuickItem* scene = window()->contentItem();
        auto* maker = findItem(scene, u"cardMaker"_s);
        QVERIFY(maker != nullptr);
        QVERIFY(!maker->property("text").toString().isEmpty()); // the maker, up front

        auto* word = findItem(scene, u"ratingWord"_s);
        QVERIFY(word != nullptr);
        QCOMPARE(word->property("text").toString(), u"Rate it"_s); // not rated yet

        auto* details = findItem(scene, u"cardDetails"_s);
        auto* info = findItem(scene, u"infoButton"_s);
        QVERIFY(details != nullptr && info != nullptr);
        QVERIFY(!details->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(info, "clicked"));
        settle();
        QVERIFY(details->property("visible").toBool()); // ⓘ expands the card

        auto* favorite = findItem(scene, u"favoriteButton"_s);
        QVERIFY(favorite != nullptr);
        QVERIFY(QMetaObject::invokeMethod(favorite, "clicked"));
        settle();
        auto* star = findItem(scene, u"favoriteButton"_s); // the list is re-sorted
        QVERIFY(star != nullptr);
        QCOMPARE(star->property("glyph").toString(), u"★"_s); // a favourite is listed first
    }

    void chartTabShowsThePastedChart()
    {
        QQuickItem* scene = window()->contentItem();
        auto* empty = findItem(scene, u"chartEmpty"_s);
        QVERIFY(empty != nullptr);
        QVERIFY(empty->isVisible()); // no chart yet: says how to add one
        QVERIFY(m_session->document().pasteChart(0, u"C        G\nHello my   friend\n"_s));
        settle();
        QVERIFY(!empty->isVisible());
        auto* lines = findItem(scene, u"chartLines"_s);
        QVERIFY(lines != nullptr);
        QCOMPARE(lines->property("count").toInt(), 1);
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
