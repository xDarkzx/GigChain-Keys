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
    // The item itself is wanted, not whether one exists: a plain search.
    for (QQuickItem* child : item->childItems()) {
        // cppcheck-suppress useStlAlgorithm
        if (QQuickItem* found = findItem(child, name)) return found;
    }
    return nullptr;
}

// Every item with that name, in the visual tree's order.
void findAll(QQuickItem* item, const QString& name, QList<QQuickItem*>& found)
{
    if (item == nullptr) return;
    if (item->objectName() == name) found << item;
    for (QQuickItem* child : item->childItems()) findAll(child, name, found);
}
QList<QQuickItem*> findAll(QQuickItem* item, const QString& name)
{
    QList<QQuickItem*> found;
    findAll(item, name, found);
    return found;
}

// Scrolls whatever flickable holds `item` so that the item is in view (as
// a player would scroll to it).
void scrollIntoView(QQuickItem* item)
{
    for (QQuickItem* parent = item->parentItem(); parent != nullptr; parent = parent->parentItem()) {
        if (!parent->inherits("QQuickFlickable")) continue;
        auto* content = parent->property("contentItem").value<QQuickItem*>();
        const QPointF at = item->mapToItem(content, QPointF(0, 0));
        const qreal most = std::max(0.0, parent->property("contentHeight").toReal() - parent->height());
        parent->setProperty("contentY", std::clamp(at.y() - (parent->height() / 3), 0.0, most));
        return;
    }
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

    // For looking at the screens: with GIGCHAIN_SCREENSHOTS set to a folder,
    // the window as it is now is saved there as <name>.png. Off otherwise.
    void shoot(const QString& name) const
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_SCREENSHOTS");
        if (folder.isEmpty()) return;
        QVERIFY(window()->grabWindow().save(folder + u'/' + name + u".png"_s));
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
        ui::PluginIconProvider::install(*m_qml); // as main() does
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
        shoot(u"perform"_s);
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

    // A channel's keyboard zone and knobs, from its menu, as a person uses them.
    void zoneAndKnobDialogsEditTheChannel()
    {
        window()->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addChannel(u"fake.grand-piano"_s, u"Grand Piano"_s));
        settle();
        QVERIFY(stripChild(u"zoneText"_s) != nullptr);
        QVERIFY(!stripChild(u"zoneText"_s)->isVisible()); // the whole keyboard: nothing to say

        doc.editChannel(0, u"zone"_s);
        settle();
        auto* zone = window()->findChild<QObject*>(u"zoneDialog"_s);
        QVERIFY(zone != nullptr);
        QTRY_VERIFY(zone->property("visible").toBool());
        auto* high = window()->findChild<QQuickItem*>(u"velocityHighBox"_s);
        QVERIFY(high != nullptr);
        high->forceActiveFocus();
        QTest::keyClick(window(), Qt::Key_Down); // one step down: a velocity layer of 1-126
        settle();
        QCOMPARE(doc.currentPatch()->channels.at(0).velocityHigh, 126);
        shoot(u"zone-dialog"_s);
        QVERIFY(QMetaObject::invokeMethod(zone, "close"));
        settle();
        QTRY_VERIFY(stripChild(u"zoneText"_s)->isVisible());
        QVERIFY(stripChild(u"zoneText"_s)->property("text").toString().contains(u"vel 1–126"_s));

        doc.editChannel(0, u"knobs"_s);
        settle();
        auto* knobs = window()->findChild<QObject*>(u"knobDialog"_s);
        QVERIFY(knobs != nullptr);
        QTRY_VERIFY(knobs->property("visible").toBool());
        auto* parameters = window()->findChild<QObject*>(u"parameterList"_s);
        QVERIFY(parameters != nullptr);
        QTRY_COMPARE(parameters->property("count").toInt(), 2); // the demo plugin's Cutoff and Resonance
        {
            // Each setting's name is on screen: its row's text has room (the
            // row's own padding once left it no height at all).
            QQuickItem* row = nullptr;
            QVERIFY(QMetaObject::invokeMethod(parameters, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, 0)));
            QVERIFY(row != nullptr);
            auto* name = row->property("contentItem").value<QQuickItem*>();
            QVERIFY(name != nullptr);
            QCOMPARE(name->property("text").toString(), u"Cutoff"_s);
            QVERIFY2(name->height() >= 14, qPrintable(QString::number(name->height())));
        }
        shoot(u"knob-dialog"_s);
        QVERIFY(QMetaObject::invokeMethod(knobs, "close"));
        settle();
    }

    // No Windows frame: the toolbar is the title bar, with mac-style lights
    // that zoom (within the screen's work area, not over the taskbar),
    // minimise and close.
    void theWindowHasItsOwnTitleBar()
    {
        QQuickWindow* w = window();
        QVERIFY(w->flags().testFlag(Qt::FramelessWindowHint));
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(w));

        auto* title = w->findChild<QQuickItem*>(u"windowTitle"_s);
        QVERIFY(title != nullptr);
        const QString shown = title->property("text").toString();
        QVERIFY2(shown.contains(m_session->document().displayName()), qPrintable(shown));
        QVERIFY2(shown.contains(u"Song 1"_s) && shown.contains(u"Patch 1"_s), qPrintable(shown));
        QVERIFY(m_session->document().renameSong(0, u"<b>A & B</b>"_s));
        QTRY_VERIFY2(title->property("text").toString().contains(u"&lt;b&gt;A &amp; B&lt;/b&gt;"_s),
                     qPrintable(title->property("text").toString())); // shown as typed, not as markup

        // Status line: the audio setup sits in the middle.
        auto* audio = w->findChild<QQuickItem*>(u"statusAudio"_s);
        QVERIFY(audio != nullptr);
        const double middle = audio->mapToScene(QPointF(audio->width() / 2, 0)).x();
        QVERIFY2(qAbs(middle - w->width() / 2.0) <= 1, qPrintable(QString::number(middle)));
        QVERIFY(!audio->property("text").toString().isEmpty());

        // The lights sit at the right end, close last, as on Windows.
        auto* minimise = item(u"windowMinimise"_s);
        auto* zoom = item(u"windowZoom"_s);
        auto* close = item(u"windowClose"_s);
        QVERIFY(minimise != nullptr && zoom != nullptr && close != nullptr);
        const auto left = [](const QQuickItem* i) { return i->mapToScene(QPointF(0, 0)).x(); };
        QVERIFY(left(minimise) < left(zoom) && left(zoom) < left(close));
        QVERIFY2(left(close) + close->width() > w->width() - 40, qPrintable(QString::number(left(close))));
        auto* settingsButton = w->findChild<QQuickItem*>(u"settingsButton"_s);
        QVERIFY(settingsButton != nullptr);
        QVERIFY(left(minimise) > left(settingsButton)); // after the last toolbar button

        // The edges resize while windowed, and step aside when maximised.
        auto* edges = w->findChild<QQuickItem*>(u"resizeEdges"_s);
        QVERIFY(edges != nullptr);
        QVERIFY(edges->isVisible());

        // Green: maximised inside the work area, then back.
        const QRect normal = w->geometry();
        click(u"windowZoom"_s);
        QTRY_COMPARE(w->visibility(), QWindow::Maximized);
        const QRect work = w->screen()->availableGeometry();
        QTRY_VERIFY2(work.contains(w->geometry()), qPrintable(u"%1,%2 %3x%4 in %5,%6 %7x%8"_s
            .arg(w->x()).arg(w->y()).arg(w->width()).arg(w->height())
            .arg(work.x()).arg(work.y()).arg(work.width()).arg(work.height())));
        QVERIFY(!edges->isVisible());
        shoot(u"maximised"_s);
        click(u"windowZoom"_s);
        QTRY_COMPARE(w->visibility(), QWindow::Windowed);
        QVERIFY(edges->isVisible());
        QTRY_COMPARE(w->geometry().size(), normal.size());

        // Yellow: minimised.
        click(u"windowMinimise"_s);
        QTRY_COMPARE(w->visibility(), QWindow::Minimized);
        w->showNormal();
        QTRY_COMPARE(w->visibility(), QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(w));

        // Red: asks the window to close (caught here so the test app stays up;
        // the unsaved-changes guard in onClosing is what runs for real).
        struct CloseCatcher : QObject {
            int closes = 0;
            bool eventFilter(QObject*, QEvent* event) override
            {
                if (event->type() != QEvent::Close) return false;
                ++closes;
                event->ignore();
                return true;
            }
        } catcher;
        w->installEventFilter(&catcher);
        click(u"windowClose"_s);
        w->removeEventFilter(&catcher);
        QCOMPARE(catcher.closes, 1);
        QVERIFY(w->isVisible());
        settle();
    }

    // Song sections in the chart: centred, large titles with what each
    // section plays; assigned by clicking, counted when played.
    void sectionsInTheChartAreAssignedByClicking()
    {
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Piano"_s));
        QVERIFY(doc.addChannel(u"demo.strings"_s, u"Strings"_s));
        QVERIFY(doc.setSongChart(0, u"{comment: Verse}\n[C]words [G]more\n{comment: Chorus}\n[F]la la\n"_s));
        // Adding an instrument showed its tab: back to the chart.
        auto* tabs = w->findChild<QObject*>(u"mainTabs"_s);
        QVERIFY(tabs != nullptr);
        QVERIFY(tabs->setProperty("currentIndex", 0));
        settle();

        auto* chart = w->findChild<QQuickItem*>(u"chartView"_s);
        QVERIFY(chart != nullptr);
        QTRY_COMPARE(findAll(chart, u"sectionHeader"_s).size(), 2);
        const QList<QQuickItem*> headers = findAll(chart, u"sectionHeader"_s);
        QVERIFY(chart->isVisible());

        // Centred and larger than the lyrics.
        QQuickItem* title = findItem(headers.value(0), u"sectionTitle"_s);
        QVERIFY(title != nullptr);
        const double centre = title->mapToScene(QPointF(title->width() / 2, 0)).x();
        const double chartCentre = chart->mapToScene(QPointF(chart->width() / 2, 0)).x();
        QVERIFY2(qAbs(centre - chartCentre) <= 1.0, qPrintable(u"%1 vs %2"_s.arg(centre).arg(chartCentre)));
        const int titleSize = title->property("font").value<QFont>().pixelSize();
        QVERIFY2(titleSize >= 26, qPrintable(QString::number(titleSize))); // lyrics are 20 px here
        // The lyrics are centred like the titles (each line as a whole).
        const QList<QQuickItem*> lyrics = findAll(chart, u"chartLyricFlow"_s);
        QVERIFY(!lyrics.isEmpty());
        for (QQuickItem* line : lyrics) {
            QVERIFY(line->width() > 0 && line->width() < chart->width()); // a short line: narrower than the chart...
            const double lineCentre = line->mapToScene(QPointF(line->width() / 2, 0)).x();
            QVERIFY2(qAbs(lineCentre - chartCentre) <= 1.0, qPrintable(u"%1 vs %2"_s.arg(lineCentre).arg(chartCentre))); // ... in its middle
        }

        // Each plays the first instrument until told otherwise.
        const auto chipNames = [](QQuickItem* header) {
            QStringList names;
            for (QQuickItem* chip : findAll(header, u"sectionChip"_s)) {
                for (QQuickItem* text : chip->childItems().value(0)->childItems()) {
                    if (text->objectName().isEmpty()) names << text->property("text").toString();
                }
            }
            return names;
        };
        QCOMPARE(chipNames(headers.value(0)), QStringList{u"Piano"_s});
        QCOMPARE(chipNames(headers.value(1)), QStringList{u"Piano"_s});
        // The header of section `n`, as the chart shows it now.
        const auto header = [chart](int n) { return findAll(chart, u"sectionHeader"_s).value(n); };

        // [+] on the chorus: the menu offers Strings; picking it adds it.
        QQuickItem* add = findItem(headers.value(1), u"sectionAdd"_s);
        QVERIFY(add != nullptr && add->isEnabled());
        scrollIntoView(add);
        settle();
        QTest::mouseClick(w, Qt::LeftButton, {}, add->mapToScene(QPointF(add->width() / 2, add->height() / 2)).toPoint());
        auto* menu = add->findChild<QObject*>(u"sectionAddMenu"_s);
        QVERIFY(menu != nullptr);
        QTRY_VERIFY(menu->property("visible").toBool());
        QQuickItem* strings = nullptr;
        QVERIFY(QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, strings), Q_ARG(int, 0)));
        QVERIFY(strings != nullptr);
        QCOMPARE(strings->property("text").toString(), u"Strings"_s);
        QTRY_VERIFY(strings->isVisible() && strings->width() > 0);
        QTest::mouseClick(w, Qt::LeftButton, {}, strings->mapToScene(QPointF(strings->width() / 2, strings->height() / 2)).toPoint());
        QTRY_COMPARE(chipNames(header(1)), (QStringList{u"Piano"_s, u"Strings"_s}));
        QVERIFY(!findItem(header(1), u"sectionAdd"_s)->isEnabled()); // nothing left to add
        shoot(u"sections"_s);

        // ✕ takes one out.
        QQuickItem* remove = findAll(header(1), u"sectionChipRemove"_s).value(0);
        QVERIFY(remove != nullptr);
        scrollIntoView(remove);
        settle();
        QTest::mouseClick(w, Qt::LeftButton, {}, remove->mapToScene(QPointF(remove->width() / 2, remove->height() / 2)).toPoint());
        QTRY_COMPARE(chipNames(header(1)), QStringList{u"Strings"_s});

        // The length: click, type, Enter.
        QQuickItem* bars = findItem(header(0), u"sectionBars"_s);
        QVERIFY(bars != nullptr);
        scrollIntoView(bars);
        settle();
        QTest::mouseClick(w, Qt::LeftButton, {}, bars->mapToScene(QPointF(bars->width() / 2, bars->height() / 2)).toPoint());
        QTest::keyClick(w, Qt::Key_8);
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(doc.currentSections().at(0).toMap().value(u"bars"_s).toInt(), 8);

        // Following the chords (the default), there is no Play: the first chord starts.
        auto* play = w->findChild<QQuickItem*>(u"songPlayButton"_s);
        QVERIFY(play != nullptr);
        QTRY_VERIFY(!play->isVisible());
        QTRY_COMPARE(w->findChild<QQuickItem*>(u"songWhere"_s)->property("text").toString(), u"Play C to start"_s);
        // By the tempo: Play counts the bars, the toolbar shows where the
        // song is, the chart lights the section.
        QVERIFY(doc.setSongFollowChords(0, false));
        QTRY_VERIFY(play->isVisible() && play->width() > 0);
        settle(); // (the toolbar laid out again with Play in it)
        click(u"songPlayButton"_s);
        auto* where = w->findChild<QQuickItem*>(u"songWhere"_s);
        QVERIFY(where != nullptr);
        QTRY_COMPARE(where->property("text").toString(), u"Verse · 1/8"_s);
        // The verse's own count, under its lit title.
        QQuickItem* verseBars = findItem(header(0), u"sectionBars"_s);
        QVERIFY(verseBars != nullptr && !verseBars->childItems().isEmpty());
        QTRY_COMPARE(verseBars->childItems().at(0)->property("text").toString(), u"bar 1 of 8"_s);
        shoot(u"sections-playing"_s);
        click(u"songPlayButton"_s); // stops
        QTRY_VERIFY(!m_engine->songPosition().playing);
        settle();
    }

    // The loop pedal over the mixer: a record and a loop button above each
    // strip; the pill when the strip is out of sight; the Loops menu.
    void theLooperStripRecordsAndLoops()
    {
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Piano"_s));
        QVERIFY(doc.addChannel(u"demo.pad"_s, u"Pad"_s));
        settle();

        auto* cells = w->findChild<QQuickItem*>(u"looperCells"_s);
        auto* strips = w->findChild<QQuickItem*>(u"mixerStrips"_s);
        QVERIFY(cells != nullptr && strips != nullptr);
        QTRY_COMPARE(findAll(cells, u"loopRecord"_s).size(), 2);
        // Each cell sits over its strip.
        QQuickItem* strip = nullptr;
        QVERIFY(QMetaObject::invokeMethod(strips, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, strip), Q_ARG(int, 1)));
        QQuickItem* cell = nullptr;
        QVERIFY(QMetaObject::invokeMethod(cells, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, 1)));
        QVERIFY(strip != nullptr && cell != nullptr);
        QCOMPARE(cell->mapToScene(QPointF(0, 0)).x(), strip->mapToScene(QPointF(0, 0)).x());
        QCOMPARE(cell->width(), strip->width());
        QVERIFY(cell->mapToScene(QPointF(0, cell->height())).y() <= strip->mapToScene(QPointF(0, 0)).y()); // above it
        // The loop station runs right across the mixer (over the master
        // strip too), its name in the middle.
        auto* band = w->findChild<QQuickItem*>(u"looperBand"_s);
        auto* mixer = strips->parentItem()->parentItem(); // (strips in their row, in the mixer's column)
        QVERIFY(band != nullptr && mixer != nullptr);
        QCOMPARE(band->width(), mixer->width());
        auto* master = w->findChild<QQuickItem*>(u"masterEffectList"_s);
        QVERIFY(master != nullptr);
        QVERIFY(band->mapToScene(QPointF(band->width(), 0)).x() >= master->mapToScene(QPointF(master->width(), 0)).x());
        auto* title = w->findChild<QQuickItem*>(u"looperTitle"_s);
        QVERIFY(title != nullptr);
        QCOMPARE(title->property("text").toString(), u"LOOP STATION"_s);
        const double titleCentre = title->mapToScene(QPointF(title->width() / 2, 0)).x();
        const double bandCentre = band->mapToScene(QPointF(band->width() / 2, 0)).x();
        QVERIFY2(qAbs(titleCentre - bandCentre) <= 1.0, qPrintable(u"%1 vs %2"_s.arg(titleCentre).arg(bandCentre)));

        const auto press = [w](QQuickItem* item) {
            QTest::mouseClick(w, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        const auto status = [cell] { return findItem(cell, u"loopStatus"_s)->property("text").toString(); };
        QCOMPARE(status(), u"4 bars"_s); // empty: how long a loop will be
        press(findItem(cell, u"loopRecord"_s)); // the pad: record...
        QTRY_COMPARE(status(), u"REC"_s);
        press(findItem(cell, u"loopRecord"_s)); // ... and close: it plays
        QTRY_COMPARE(status(), u"1/4"_s);
        QVERIFY(findItem(cell, u"loopRing"_s)->isVisible());
        shoot(u"looper"_s);

        // The pill shows only while the strip is out of sight.
        auto* pill = w->findChild<QQuickItem*>(u"loopsPill"_s);
        QVERIFY(pill != nullptr);
        QVERIFY(!pill->isVisible());
        auto* loops = m_qml->rootObjects().value(0)->property("loops").value<QObject*>();
        QVERIFY(loops != nullptr);
        const qreal withLooper = strip->height();
        QVERIFY(loops->setProperty("stripVisible", false));
        QTRY_VERIFY(pill->isVisible());
        QTRY_COMPARE(strip->height(), withLooper); // the looper strip adds room, it does not squeeze the strips
        QCOMPARE(findItem(pill, u"loopsPillCount"_s)->property("text").toString(), u"1"_s);
        shoot(u"loops-pill"_s);
        QVERIFY(loops->setProperty("stripVisible", true));
        QTRY_VERIFY(!pill->isVisible());
        settle(); // laid out again

        // Loop: stops it.
        press(findItem(cell, u"loopPlay"_s));
        QTRY_COMPARE(status(), u"stopped"_s);

        // The Loops menu and the keyboard controls.
        click(u"loopsMenuButton"_s);
        auto* menu = w->findChild<QObject*>(u"loopsMenu"_s);
        QVERIFY(menu != nullptr);
        QTRY_VERIFY(menu->property("visible").toBool());
        QTest::keyClick(w, Qt::Key_Escape);
        auto* dialog = w->findChild<QObject*>(u"loopControlsDialog"_s);
        QVERIFY(dialog != nullptr);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_COMPARE(findAll(qobject_cast<QQuickItem*>(dialog->property("contentItem").value<QObject*>()), u"loopControlLearn"_s).size(), 6);
        shoot(u"loop-controls"_s);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        settle();
    }

    // The on-screen keyboard keeps real key proportions at any window width:
    // on a very wide screen it grows a little taller, then sits in the
    // middle instead of stretching the keys.
    void theKeyboardKeepsItsKeysInShape()
    {
        QQuickWindow* w = window();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        auto* keys = w->findChild<QQuickItem*>(u"keyboardKeys"_s);
        auto* board = w->findChild<QQuickItem*>(u"keyboardView"_s);
        QVERIFY(keys != nullptr && board != nullptr);
        const auto whiteKey = [keys] { // middle C
            const QList<QQuickItem*> all = keys->childItems();
            const auto it = std::ranges::find_if(all, [](const QQuickItem* key) { return key->objectName() == u"key60"_s; });
            return it != all.end() ? *it : nullptr;
        };
        for (const int width : {1440, 3440}) {
            w->resize(width, 900);
            QTRY_COMPARE(w->width(), width);
            settle();
            QQuickItem* c = whiteKey();
            QVERIFY(c != nullptr);
            const double ratio = c->height() / (c->width() + 1); // (+1: the gap between keys)
            QVERIFY2(ratio > 3.9 && ratio < 4.5, qPrintable(u"%1 wide: key %2 x %3"_s.arg(width).arg(c->width()).arg(c->height())));
            QVERIFY(board->height() >= 96 && board->height() <= 160);
            // Centred in the room it has.
            const double keysCentre = keys->mapToScene(QPointF(keys->width() / 2, 0)).x();
            const double roomCentre = keys->parentItem()->mapToScene(QPointF(keys->parentItem()->width() / 2, 0)).x();
            QVERIFY2(qAbs(keysCentre - roomCentre) <= 1.0, qPrintable(QString::number(keysCentre - roomCentre)));
        }
        shoot(u"keyboard-wide"_s);
        w->resize(1440, 880);
        settle();
    }

    // The on-screen keyboard: a key clicked plays and lights, and lets go.
    void theKeyboardLightsTheKeysPlayed()
    {
        window()->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        auto* keyboard = window()->findChild<QQuickItem*>(u"keyboardView"_s);
        QVERIFY(keyboard != nullptr);
        QVERIFY(keyboard->isVisible());
        // The keys are the key area's child items (made by a Repeater).
        auto* keys = window()->findChild<QQuickItem*>(u"keyboardKeys"_s);
        QVERIFY(keys != nullptr);
        QQuickItem* middleC = nullptr;
        for (QQuickItem* key : keys->childItems()) {
            if (key->objectName() == u"key60"_s) middleC = key;
        }
        QVERIFY(middleC != nullptr);
        QCOMPARE(middleC->property("velocity").toInt(), 0);
        const QPoint at = middleC->mapToScene(QPointF(middleC->width() / 2, middleC->height() * 0.8)).toPoint();
        QTest::mousePress(window(), Qt::LeftButton, {}, at);
        QCOMPARE(int(m_engine->keyboardActivity().velocity.at(60)), 100); // played
        QTRY_COMPARE(middleC->property("velocity").toInt(), 100);         // and lit
        shoot(u"keyboard"_s);
        QTest::mouseRelease(window(), Qt::LeftButton, {}, at);
        QTRY_COMPARE(middleC->property("velocity").toInt(), 0);
        // Hidden and shown from the toolbar.
        click(u"keyboardButton"_s);
        QVERIFY(!keyboard->isVisible());
        click(u"keyboardButton"_s);
        QVERIFY(keyboard->isVisible());
        settle();
    }

    // Tempo typed and the click switched on in the toolbar; an edit undone.
    void toolbarTempoClickAndUndo()
    {
        window()->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        type(u"tempoField"_s, u"96"_s);
        QCOMPARE(m_engine->tempo(), 96.0);
        click(u"clickButton"_s);
        QVERIFY(m_engine->clickOn());

        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.renameSong(0, u"Renamed"_s));
        settle();
        click(u"undoButton"_s);
        QCOMPARE(doc.currentSongName(), u"Song 1"_s);
        QVERIFY(item(u"trackPlayButton"_s) == nullptr || !item(u"trackPlayButton"_s)->isVisible()); // no backing track
        settle();
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
            shoot(u"settings-%1"_s.arg(page));
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
        QVERIFY(star->property("active").toBool()); // a favourite is listed first, its star lit
        QVERIFY(star->property("activeIconSource").toString().endsWith(u"star-filled.svg"_s));
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

    // The fake engine is not played: the first chord is shown and outlined.
    void aChartWaitsForItsFirstChord()
    {
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Piano"_s));
        QVERIFY(doc.setSongChart(0, u"{c: Verse}\n[Am]words [F]more\n{c: Chorus}\n[C]la [G]la\n"_s));
        auto* tabs = w->findChild<QObject*>(u"mainTabs"_s);
        QVERIFY(tabs != nullptr);
        QVERIFY(tabs->setProperty("currentIndex", 0));
        settle();
        auto* chart = w->findChild<QQuickItem*>(u"chartView"_s);
        QVERIFY(chart != nullptr);
        QQuickItem* start = findItem(chart, u"followStartLine"_s);
        QVERIFY(start != nullptr);
        QTRY_VERIFY(start->isVisible());
        QCOMPARE(start->property("text").toString(), u"Play Am to start"_s);
        const QList<QQuickItem*> next = findAll(chart, u"chartChordNext"_s);
        QVERIFY(!next.isEmpty()); // the first chord, outlined
        QVERIFY(findAll(chart, u"chartChordCurrent"_s).isEmpty()); // nothing lit before the start
        shoot(u"follow-waiting"_s);

        // One chord is not a song to follow (the song settings' hint).
        QVERIFY(doc.setSongChart(0, u"[C]only one"_s));
        QVERIFY(!doc.following());
        QTRY_VERIFY(!start->isVisible());
        QVERIFY(doc.setSongChart(0, u"{c: Verse}\n[Am]words [F]more\n"_s));
        QTRY_VERIFY(start->isVisible());

        // Following by tempo: no start line.
        QVERIFY(doc.setSongFollowChords(0, false));
        QTRY_VERIFY(!start->isVisible());
    }
};

QTEST_MAIN(TestQmlSmoke)
#include "tst_qml_smoke.moc"
