// Loads the real Main.qml with the demo engine and fails on any QML warning.
#include "Notifications.h"
#include "Session.h"
#include "StartupProgress.h"

#include "gigchain/core/Branding.h"
#include "gigchain/engine/FakeEngineFactory.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
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

    // A finger on a touch screen: pressed at `from`, slid to `to` the way a
    // hand moves (a frame at a time), lifted.
    QPointingDevice* m_finger = nullptr;
    void fingerDrag(QPoint from, QPoint to)
    {
        QTest::touchEvent(window(), m_finger).press(0, from);
        constexpr int kSteps = 12;
        for (int i = 1; i <= kSteps; ++i) {
            QTest::qWait(16);
            QTest::touchEvent(window(), m_finger).move(0, from + ((to - from) * i / kSteps));
        }
        QTest::touchEvent(window(), m_finger).release(0, to);
        QTest::qWait(50);
    }
    void fingerTap(QQuickItem* target)
    {
        const QPoint at = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
        QTest::touchEvent(window(), m_finger).press(0, at);
        QTest::qWait(30);
        QTest::touchEvent(window(), m_finger).release(0, at);
        QTest::qWait(30);
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
    void initTestCase()
    {
        QQuickStyle::setStyle(u"Basic"_s);
        m_finger = QTest::createTouchDevice();
    }

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
        auto* panic = root->findChild<QObject*>(u"panicButton"_s); // in the toolbar, as MainStage's
        QVERIFY(panic != nullptr);
        QVERIFY(QMetaObject::invokeMethod(panic, "clicked"));
        const ui::Notifications& shown = *m_session->document().notifications();
        QVERIFY(shown.rowCount() > 0 && shown.text(shown.rowCount() - 1).contains(u"Panic"_s));
        QVERIFY(root->setProperty("performMode", false));
        settle();
    }

    // On stage, as MainStage and Gig Performer lay it out: a slim header
    // (previous, the song, next), the song's parts as tiles, and the chart
    // filling the rest. Panic is the toolbar's (no second, bigger one).
    void performIsTheChartWithASlimHeaderAndTheSongsParts()
    {
        QObject* root = m_qml->rootObjects().value(0);
        window()->resize(1600, 900);
        QVERIFY(m_session->document().addSong());
        QVERIFY(m_session->document().selectPatch(0, 0));
        QVERIFY(m_session->document().setSongChart(
            0, u"{comment: Verse}\n[C]Hello [G]world\n[Am]Here we [F]go\n{comment: Chorus}\n[F]Sing it [G]loud\n"_s));
        QVERIFY(root->setProperty("performMode", true));
        settle();
        shoot(u"perform-song"_s);
        QVERIFY(root->findChild<QObject*>(u"performPanic"_s) == nullptr);
        // The chart has the screen: the mixer and keyboard stay closed on
        // stage unless asked for, and Edit keeps its own.
        QCOMPARE(root->property("mixerOpen").toBool(), false);
        QCOMPARE(root->property("keyboardOpen").toBool(), false);
        QCOMPARE(root->property("editMixerOpen").toBool(), true);

        // Previous and next: small, in the header, next to the song's name.
        auto* previous = root->findChild<QQuickItem*>(u"performPrevious"_s);
        auto* next = root->findChild<QQuickItem*>(u"performNext"_s);
        auto* chart = root->findChild<QQuickItem*>(u"performChart"_s);
        QVERIFY(previous != nullptr && next != nullptr && chart != nullptr);
        QVERIFY2(previous->height() <= 56 && previous->width() <= 64, "previous is a small header button");
        QVERIFY2(next->height() <= 56 && next->width() <= 64, "next is a small header button");
        const double chartTop = chart->mapToScene({0, 0}).y();
        QVERIFY2(previous->mapToScene({0, 0}).y() < chartTop, "previous sits above the chart");
        QVERIFY2(next->mapToScene({0, 0}).y() < chartTop, "next sits above the chart");
        auto* perform = root->findChild<QQuickItem*>(u"performView"_s);
        QVERIFY(perform != nullptr);
        QVERIFY2(chart->height() > perform->height() * 0.55, qPrintable(u"the chart has %1 of %2"_s.arg(chart->height()).arg(perform->height())));
        QVERIFY(QMetaObject::invokeMethod(next, "clicked"));
        QCOMPARE(m_session->document().songIndex(), 1);
        QVERIFY(QMetaObject::invokeMethod(previous, "clicked"));
        QCOMPARE(m_session->document().songIndex(), 0);

        // The song's parts, as tiles: the current one lit, a tap goes there.
        auto* parts = root->findChild<QQuickItem*>(u"performParts"_s);
        QVERIFY(parts != nullptr);
        QVERIFY(parts->isVisible());
        QCOMPARE(parts->property("count").toInt(), 2);
        shoot(u"perform-parts"_s);
        // The chart is read, not edited, on stage: no "+" to add an
        // instrument, no click-to-type length in its section titles.
        // (Delegates are the chart's visual children, not its QObject ones.)
        const auto all = [](QQuickItem* top, const QString& name) {
            QList<QQuickItem*> found;
            QList<QQuickItem*> todo{top};
            while (!todo.isEmpty()) {
                QQuickItem* item = todo.takeLast();
                if (item->objectName() == name) found << item;
                todo << item->childItems();
            }
            return found;
        };
        const auto stageItems = [&all, chart](const QString& name) {
            const QList<QQuickItem*> found = all(chart, name);
            return static_cast<int>(std::ranges::count_if(found, [](const QQuickItem* item) { return item->isVisible(); }));
        };
        QCOMPARE(stageItems(u"sectionTitle"_s), 2);
        QCOMPARE(stageItems(u"sectionAdd"_s), 0);
        QCOMPARE(stageItems(u"sectionBarsInput"_s), 0);
        const QList<QQuickItem*> bars = all(chart, u"sectionBars"_s);
        QVERIFY(!bars.isEmpty());
        QCOMPARE(bars.first()->property("editable").toBool(), false);
        QVERIFY(QMetaObject::invokeMethod(parts, "choose", Q_ARG(QVariant, 1)));
        settle();
        QCOMPARE(m_engine->songPosition().section, 1);

        // A song with no lyrics or chords says so and leads to the editor.
        QVERIFY(m_session->document().selectPatch(1, 0));
        settle();
        QVERIFY(!parts->isVisible());
        auto* add = root->findChild<QQuickItem*>(u"performAddChart"_s);
        QVERIFY(add != nullptr && add->isVisible());
        QCOMPARE(add->property("text").toString(), u"Add lyrics & chords"_s);
        auto* none = root->findChild<QQuickItem*>(u"performNoChart"_s);
        QVERIFY(none != nullptr && none->isVisible());
        QVERIFY2(none->mapToScene({0, 0}).y() > perform->mapToScene({0, 0}).y() + 100, "the message is in the open, under the header");
        shoot(u"perform-empty"_s);
        QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
        settle();
        QCOMPARE(root->property("performMode").toBool(), false);
        auto* tabs = root->findChild<QObject*>(u"mainTabs"_s);
        QCOMPARE(tabs->property("currentIndex").toInt(), 0); // the chart tab
    }

    // On stage with a touch screen: Play, Next part and Loop part are big
    // enough for a finger and work with one; a part tile tapped is where the
    // song goes; a finger slid over the chart scrolls it (and plays nothing,
    // even when it starts on a tile or a chord).
    void aFingerPlaysAndScrollsOnStage()
    {
        QObject* root = m_qml->rootObjects().value(0);
        window()->resize(1400, 800);
        ui::DocumentController& doc = m_session->document();
        QString chart = u"{comment: Verse}\n"_s;
        for (int i = 0; i < 30; ++i) chart += u"[C]Line [G]of the [Am]long [F]verse\n"_s;
        chart += u"{comment: Chorus}\n[F]Sing it [G]loud\n"_s;
        QVERIFY(doc.setSongChart(0, chart));
        QVERIFY(doc.selectPatch(0, 0));
        QVERIFY(root->setProperty("performMode", true));
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        settle();

        auto* play = item(u"performPlay"_s);
        auto* nextPart = item(u"performNextPart"_s);
        auto* loopPart = item(u"performLoopPart"_s);
        QVERIFY(play != nullptr && nextPart != nullptr && loopPart != nullptr);
        for (QQuickItem* button : {play, nextPart, loopPart}) {
            QVERIFY2(button->isVisible() && button->height() >= 44,
                     qPrintable(u"%1 is %2 px high: too small for a finger"_s.arg(button->objectName()).arg(button->height())));
        }
        shoot(u"perform-transport"_s);

        // A tile tapped (stopped): that part is where the song is.
        const QList<QQuickItem*> tiles = findAll(item(u"performParts"_s), u"performPart"_s);
        QCOMPARE(tiles.size(), 2);
        QVERIFY(tiles.at(1)->height() >= 44);
        fingerTap(tiles.at(1));
        QTRY_COMPARE(m_engine->songPosition().section, 1);
        fingerTap(tiles.at(0));
        QTRY_COMPARE(m_engine->songPosition().section, 0);

        // A finger slid up the chart scrolls it, even starting on a chord.
        auto* stage = item(u"performChart"_s);
        QVERIFY(stage != nullptr);
        QVERIFY(stage->property("contentHeight").toReal() > stage->height());
        const QPoint start = stage->mapToScene(QPointF(stage->width() / 2, stage->height() * 0.8)).toPoint();
        fingerDrag(start, start - QPoint(0, 300));
        QTRY_VERIFY2(stage->property("contentY").toReal() > 100,
                     qPrintable(u"the chart scrolled to %1"_s.arg(stage->property("contentY").toReal())));
        QCOMPARE(m_engine->songPosition().section, 0); // nothing was tapped on the way

        // Play, by finger: the song plays; Loop part lights; Stop.
        fingerTap(play);
        QTRY_VERIFY(m_engine->songPosition().playing);
        fingerTap(loopPart);
        QTRY_VERIFY(loopPart->property("checked").toBool());
        fingerTap(play);
        QTRY_VERIFY(!m_engine->songPosition().playing);
    }

    // In the setlist a finger slid over the songs scrolls the list; it does
    // not drag a song to another place (a mouse still does).
    void aFingerScrollsTheSetlist()
    {
        window()->resize(1400, 700);
        ui::DocumentController& doc = m_session->document();
        for (int i = 0; i < 40; ++i) QVERIFY(doc.addSong());
        QVERIFY(doc.selectPatch(0, 0));
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        settle();
        auto* list = window()->findChild<QQuickItem*>(u"setlistList"_s);
        QVERIFY(list != nullptr);
        QVERIFY(list->property("contentHeight").toReal() > list->height());
        const double before = list->property("contentY").toReal();
        QStringList order;
        for (const core::Song& song : doc.setlist().songs) order << song.id.value();
        const QPoint start = list->mapToScene(QPointF(list->width() / 2, list->height() * 0.7)).toPoint();
        fingerDrag(start, start - QPoint(0, 250));
        QTRY_VERIFY2(list->property("contentY").toReal() > before + 100,
                     qPrintable(u"the list scrolled from %1 to %2"_s.arg(before).arg(list->property("contentY").toReal())));
        QStringList after;
        for (const core::Song& song : doc.setlist().songs) after << song.id.value();
        QCOMPARE(after, order); // no song was moved
    }

    // A channel is named after the sound it plays: double-click its name
    // plate, type, Enter (F2 does the same for the strip clicked).
    void aChannelIsRenamedOnItsStrip()
    {
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Analog Lab V"_s));
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Analog Lab V"_s));
        QCOMPARE(doc.currentPatch()->channels.at(1).name, u"Analog Lab V 2"_s);
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));
        settle();
        QQuickItem* plate = stripChild(u"stripNamePlate"_s);
        QVERIFY(plate != nullptr);
        const QPoint at = plate->mapToScene(QPointF(plate->width() / 2, plate->height() / 2)).toPoint();
        QTest::mouseDClick(w, Qt::LeftButton, {}, at);
        QQuickItem* field = stripChild(u"stripNameField"_s);
        QVERIFY(field != nullptr);
        QTRY_VERIFY(field->isVisible() && field->hasActiveFocus());
        QVERIFY(field->setProperty("text", u"Classic American Piano"_s));
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(doc.currentPatch()->channels.at(0).name, u"Classic American Piano"_s);
        QVERIFY(!field->isVisible());
        settle();

        // F2 on the strip clicked.
        click(u"stripNamePlate"_s); // selects the strip (the mixer takes the keys)
        QTest::keyClick(w, Qt::Key_F2);
        QTRY_VERIFY(field->isVisible());
        QTest::keyClick(w, Qt::Key_Escape); // nothing changed
        QTRY_VERIFY(!field->isVisible());
        QCOMPARE(doc.currentPatch()->channels.at(0).name, u"Classic American Piano"_s);
    }

    // A−/A+ in the Perform view change the chart's size, kept for next time.
    void theStageChartGrowsAndShrinks()
    {
        QObject* root = m_qml->rootObjects().value(0);
        QVERIFY(m_session->document().setSongChart(0, u"[C]Hello [G]world\n"_s));
        QVERIFY(root->setProperty("performMode", true));
        settle();
        auto* bigger = root->findChild<QQuickItem*>(u"performBigger"_s);
        auto* smaller = root->findChild<QQuickItem*>(u"performSmaller"_s);
        QVERIFY(bigger != nullptr && smaller != nullptr);
        const double before = m_session->settingsController().chartTextSize();
        QVERIFY(QMetaObject::invokeMethod(bigger, "clicked"));
        QVERIFY(m_session->settingsController().chartTextSize() > before);
        QVERIFY(QMetaObject::invokeMethod(smaller, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(smaller, "clicked"));
        QVERIFY(m_session->settingsController().chartTextSize() < before);
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

    // The play mode (the top bar's layers button): all together lights every
    // strip; one at a time dims the others (the reason on hover); a click on
    // another strip plays that one instead.
    void theMixerPlaysAllTogetherOrOneAtATime()
    {
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Piano"_s));
        QVERIFY(doc.addChannel(u"demo.pad"_s, u"Pad"_s));
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(w));
        settle();
        auto* strips = m_qml->rootObjects().value(0)->findChild<QObject*>(u"mixerStrips"_s);
        QVERIFY(strips != nullptr);
        const auto stripAt = [strips](int i) {
            QQuickItem* strip = nullptr;
            QMetaObject::invokeMethod(strips, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, strip), Q_ARG(int, i));
            return strip;
        };
        QVERIFY(stripAt(0) != nullptr && stripAt(1) != nullptr);
        QCOMPARE(stripAt(0)->property("silentReason").toString(), QString());
        QCOMPARE(stripAt(1)->property("silentReason").toString(), QString()); // layers: both play
        const auto centre = [](QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };

        // One button in the top bar, where it always is: lit while one at a time.
        QQuickItem* mode = item(u"playModeButton"_s);
        QVERIFY(mode != nullptr && mode->isVisible());
        QVERIFY(!mode->property("checked").toBool());
        QVERIFY(mode->property("tip").toString().contains(u"together"_s));
        doc.setSelectedChannel(0);
        click(u"playModeButton"_s);
        settle();
        QCOMPARE(doc.playMode(), 1);
        QVERIFY(mode->property("checked").toBool());
        QVERIFY(mode->property("iconSource").toString().contains(u"stack-single"_s));
        shoot(u"play-mode-one"_s);
        QVERIFY(stripAt(1)->property("silentReason").toString().contains(u"One at a time"_s));
        QVERIFY(stripAt(1)->opacity() < 1.0);
        QCOMPARE(stripAt(0)->opacity(), 1.0);

        // The pad's strip clicked: the pad plays, the piano rests.
        QQuickItem* padSlot = findItem(stripAt(1), u"instrumentSlot"_s);
        QVERIFY(padSlot != nullptr);
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(padSlot));
        settle();
        QCOMPARE(doc.selectedChannel(), 1);
        QCOMPARE(stripAt(1)->property("silentReason").toString(), QString());
        QVERIFY(!stripAt(0)->property("silentReason").toString().isEmpty());

        click(u"playModeButton"_s);
        settle();
        QCOMPARE(doc.playMode(), 0);
        QVERIFY(!mode->property("checked").toBool());
        QCOMPARE(stripAt(0)->property("silentReason").toString(), QString());
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
        // (Room for the chart under its flow bar: the mixer stays, the keys go.)
        QVERIFY(m_qml->rootObjects().value(0)->setProperty("editKeyboardOpen", false));
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
        // The lyrics are centred like the titles (each line as a whole; in the
        // Chart tab a line is its word cells).
        const QList<QQuickItem*> lyrics = findAll(chart, u"chartCells"_s);
        QVERIFY(!lyrics.isEmpty());
        for (QQuickItem* line : lyrics) {
            QVERIFY(line->width() > 0 && line->width() < chart->width()); // a short line: narrower than the chart...
            const double lineCentre = line->mapToScene(QPointF(line->width() / 2, 0)).x();
            QVERIFY2(qAbs(lineCentre - chartCentre) <= 3.0, qPrintable(u"%1 vs %2"_s.arg(lineCentre).arg(chartCentre))); // ... in its middle
        }

        // Each plays every instrument (layers) until told otherwise.
        const auto chipNames = [](QQuickItem* header) {
            QStringList names;
            for (QQuickItem* chip : findAll(header, u"sectionChip"_s)) {
                for (QQuickItem* text : chip->childItems().value(0)->childItems()) {
                    if (text->objectName().isEmpty()) names << text->property("text").toString();
                }
            }
            return names;
        };
        QCOMPARE(chipNames(headers.value(0)), (QStringList{u"Piano"_s, u"Strings"_s}));
        QCOMPARE(chipNames(headers.value(1)), (QStringList{u"Piano"_s, u"Strings"_s}));
        QVERIFY(!findItem(headers.value(1), u"sectionAdd"_s)->isEnabled()); // nothing left to add
        // The header of section `n`, as the chart shows it now.
        const auto header = [chart](int n) { return findAll(chart, u"sectionHeader"_s).value(n); };

        // ✕ on the chorus's Strings: the piano alone.
        QQuickItem* takeOut = findAll(headers.value(1), u"sectionChipRemove"_s).value(1);
        QVERIFY(takeOut != nullptr);
        scrollIntoView(takeOut);
        settle();
        QTest::mouseClick(w, Qt::LeftButton, {}, takeOut->mapToScene(QPointF(takeOut->width() / 2, takeOut->height() / 2)).toPoint());
        QTRY_COMPARE(chipNames(header(1)), QStringList{u"Piano"_s});

        // [+] on the chorus: the menu offers Strings; picking it adds it back.
        QQuickItem* add = findItem(header(1), u"sectionAdd"_s);
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

        // Play counts the bars along its timeline, the toolbar shows where
        // the song is, the chart lights the section.
        auto* play = w->findChild<QQuickItem*>(u"songPlayButton"_s);
        QVERIFY(play != nullptr);
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

    // A chord tapped on stage shows how to play it: a dot on each key (the
    // slash note in the left hand), its inversions to look through, and the
    // one kept for the song (Practice then plays it).
    void aChordTappedOnStageShowsHowToPlayIt()
    {
        QObject* root = m_qml->rootObjects().value(0);
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.setSongChart(0, u"{comment: Verse}\n[E/D#]Slow [C#m]down\n"_s));
        QVERIFY(root->setProperty("performMode", true));
        settle();
        QQuickWindow* w = window();
        QQuickItem* scene = w->contentItem();
        auto* stage = findItem(scene, u"performView"_s);
        QVERIFY(stage != nullptr && stage->isVisible());
        // The E/D# chord's text in the stage chart.
        QList<QQuickItem*> texts;
        findAll(stage, QString(), texts);
        QQuickItem* chordText = nullptr;
        for (QQuickItem* t : texts) {
            if (t->property("text").toString() == u"E/D#"_s && t->isVisible()) chordText = t;
        }
        QVERIFY(chordText != nullptr);
        QTest::mouseClick(w, Qt::LeftButton, {}, chordText->mapToScene(QPointF(chordText->width() / 2, chordText->height() / 2)).toPoint());
        // (One diagram in each view: Perform's is the one that opens.)
        const auto shownDiagram = [root]() -> QObject* {
            const QList<QObject*> all = root->findChildren<QObject*>(u"chordDiagram"_s);
            const auto it = std::ranges::find_if(all, [](QObject* d) { return d->property("visible").toBool(); });
            return it == all.end() ? nullptr : *it;
        };
        QTRY_VERIFY(shownDiagram() != nullptr);
        QObject* diagram = shownDiagram();
        QCOMPARE(diagram->property("chord").toString(), u"E/D#"_s);
        QList<QQuickItem*> dots;
        findAll(scene, u"chordDiagramDot"_s, dots);
        QCOMPARE(dots.size(), 4); // D#; E G# B
        auto* right = findItem(scene, u"chordDiagramRight"_s);
        QVERIFY(right != nullptr);
        QVERIFY2(right->property("text").toString().contains(u"E G# B"_s), qPrintable(right->property("text").toString()));
        shoot(u"chord-diagram"_s);
        // The 1st inversion looked at, then kept for the song.
        auto* first = findItem(scene, u"chordInversion1"_s);
        QVERIFY(first != nullptr);
        QVERIFY(QMetaObject::invokeMethod(first, "clicked"));
        QTRY_VERIFY(findItem(scene, u"chordDiagramRight"_s)->property("text").toString().contains(u"G# B E"_s));
        auto* keep = findItem(scene, u"chordDiagramKeep"_s);
        QVERIFY(keep != nullptr);
        QVERIFY(QMetaObject::invokeMethod(keep, "clicked"));
        QCOMPARE(doc.setlist().songs.at(0).chordInversions.at(u"E/D#"_s), 1);
        QVERIFY(QMetaObject::invokeMethod(diagram, "close"));
        QVERIFY(root->setProperty("performMode", false));
        settle();
    }

    // The flow bar over the chart: the chart's sections in order; a part
    // played once more shows ×2 and is the song's flow (its timeline keeps to it).
    void theFlowBarShowsAndChangesTheSongsOrder()
    {
        QObject* root = m_qml->rootObjects().value(0);
        QVERIFY(root->setProperty("editMixerOpen", false));
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.setSongChart(0, u"{comment: Verse 1}\n[Am]a [F]b\n{comment: Chorus}\n[C]c [G]d\n{comment: Verse 2}\n[Dm]e [E]f\n"_s));
        settle();
        QQuickWindow* w = window();
        QQuickItem* scene = w->contentItem();
        auto* bar = findItem(scene, u"flowBar"_s);
        QVERIFY(bar != nullptr && bar->isVisible());
        const auto partTexts = [bar] {
            QStringList texts;
            for (QQuickItem* part : findAll(bar, u"flowPart"_s)) texts << part->property("text").toString();
            return texts;
        };
        QCOMPARE(partTexts(), (QStringList{u"Verse 1"_s, u"Chorus"_s, u"Verse 2"_s}));
        // The chorus's menu: play it once more.
        QQuickItem* chorus = findAll(bar, u"flowPart"_s).value(1);
        QVERIFY(QMetaObject::invokeMethod(chorus, "clicked"));
        settle();
        auto* onceMore = chorus->findChild<QObject*>(u"flowOnceMore"_s); // (its own menu)
        QVERIFY(onceMore != nullptr);
        QVERIFY(QMetaObject::invokeMethod(onceMore, "triggered"));
        settle();
        QTRY_COMPARE(partTexts(), (QStringList{u"Verse 1"_s, u"Chorus  ×2"_s, u"Verse 2"_s}));
        QVERIFY(doc.songFlowSet());
        QCOMPARE(doc.songFlow().size(), 4);
        shoot(u"flow-bar"_s);
        QVERIFY(doc.setSongFlow({}));
    }

    // A long chart scrolls with the mouse wheel (the page itself is not
    // dragged: dragging moves chords).
    void aLongChartScrollsWithTheWheel()
    {
        QObject* root = m_qml->rootObjects().value(0);
        QVERIFY(root->setProperty("editMixerOpen", false));
        QVERIFY(root->setProperty("editKeyboardOpen", false));
        QString chart = u"{comment: Verse}\n"_s;
        for (int i = 0; i < 80; ++i) chart += u"[C]Line number %1 of the song\n"_s.arg(i);
        QVERIFY(m_session->document().setSongChart(0, chart));
        settle();
        QQuickWindow* w = window();
        auto* scroll = findItem(w->contentItem(), u"chartScroll"_s);
        QVERIFY(scroll != nullptr);
        auto* page = scroll->property("contentItem").value<QQuickItem*>();
        QVERIFY(page != nullptr);
        QCOMPARE(page->property("contentY").toDouble(), 0.0);
        const QPointF at = scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2));
        for (int i = 0; i < 3; ++i) {
            QWheelEvent wheel(at, w->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(w, &wheel);
        }
        QTRY_VERIFY2(page->property("contentY").toDouble() > 50.0, qPrintable(page->property("contentY").toString()));
    }

    // A song pasted from a chord site (chords on their own lines over the
    // words), then its chords dragged along their lines, one line after
    // another, as a player tidies it: each lands where it is dropped and
    // stays there (none snaps back), and Ctrl+Z takes back the last drag
    // only.
    void theChordsOfAPastedSongAreDragged()
    {
        QObject* root = m_qml->rootObjects().value(0);
        QVERIFY(root->setProperty("editMixerOpen", false));
        QVERIFY(root->setProperty("editKeyboardOpen", false));
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.pasteChart(0, u"[Verse 1]\n"
                                   "G              D\n"
                                   "I found a love for me\n"
                                   "Em                C\n"
                                   "Darling just dive right in\n"_s));
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[G]I found a love [D]for me\n[Em]Darling just dive [C]right in\n"_s);
        settle();
        QQuickItem* scene = w->contentItem();
        const auto centre = [](QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        // The cell of the word `text`, its box, its chip.
        const auto cellOf = [scene](const QString& text) -> QQuickItem* {
            QList<QQuickItem*> all;
            findAll(scene, u"chartCell"_s, all);
            const auto it = std::ranges::find_if(all, [&text](QQuickItem* c) { return c->property("modelData").toMap().value(u"text"_s).toString() == text; });
            return it == all.end() ? nullptr : *it;
        };
        const auto boxOf = [&cellOf](const QString& text) { QQuickItem* c = cellOf(text); return c != nullptr ? findItem(c, u"chordBox"_s) : nullptr; };
        const auto chipNamed = [scene](const QString& name) -> QQuickItem* {
            QList<QQuickItem*> chips;
            findAll(scene, u"chartChordChip"_s, chips);
            const auto it = std::ranges::find_if(chips, [&name](QQuickItem* c) { return c->property("chordName").toString() == name; });
            return it == chips.end() ? nullptr : *it;
        };
        const auto drag = [w](QPoint from, QPoint to) {
            QTest::mousePress(w, Qt::LeftButton, {}, from);
            for (int i = 1; i <= 12; ++i) {
                QTest::mouseMove(w, from + (to - from) * i / 12);
                QTest::qWait(10);
            }
            QTest::mouseRelease(w, Qt::LeftButton, {}, to);
            QTest::qWait(50);
        };

        // Line 1: D from "for" to "love".
        QQuickItem* d = chipNamed(u"D"_s);
        QVERIFY(d != nullptr && boxOf(u"love"_s) != nullptr);
        drag(centre(d), centre(boxOf(u"love"_s)));
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[G]I found a [D]love for me\n[Em]Darling just dive [C]right in\n"_s);
        settle();

        // Line 2: C from "right" to "in"; line 1 keeps its move.
        QQuickItem* c = chipNamed(u"C"_s);
        QVERIFY(c != nullptr && boxOf(u"in"_s) != nullptr);
        drag(centre(c), centre(boxOf(u"in"_s)));
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[G]I found a [D]love for me\n[Em]Darling just dive right [C]in\n"_s);
        settle();
        // Every chip is drawn over its word (not swept to the line's start).
        for (const auto& [chord, wordText] : {std::pair{u"G"_s, u"I"_s}, {u"D"_s, u"love"_s}, {u"Em"_s, u"Darling"_s}, {u"C"_s, u"in"_s}}) {
            QQuickItem* chip = chipNamed(chord);
            QQuickItem* cell = cellOf(wordText);
            QVERIFY(chip != nullptr && cell != nullptr);
            const double chipX = chip->mapToScene(QPointF(0, 0)).x();
            const double cellX = cell->mapToScene(QPointF(0, 0)).x();
            QVERIFY2(qAbs(chipX - cellX) < 2.0, qPrintable(u"%1 drawn at %2, its word at %3"_s.arg(chord).arg(chipX).arg(cellX)));
        }

        // Ctrl+Z: the last drag only, the chart as it looked before it.
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        settle();
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[G]I found a [D]love for me\n[Em]Darling just dive [C]right in\n"_s);
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        settle();
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[G]I found a love [D]for me\n[Em]Darling just dive [C]right in\n"_s);
    }

    // The Chart tab edited as a player does it, with the mouse and keys, as
    // cells: a click opens a line (a bar over each word without a chord); a
    // word double-clicked and changed where it is; a chord typed in a word's
    // box, Tab on to the next box; a chord clicked and changed; chords
    // dragged onto other words, from the chart and from the palette; one
    // dragged to Remove; a section renamed; new lines typed at the end.
    void theChartIsEditedWhereItIsRead()
    {
        QObject* root = m_qml->rootObjects().value(0);
        QVERIFY(root->setProperty("editMixerOpen", false)); // (room for the chart)
        QVERIFY(root->setProperty("editKeyboardOpen", false));
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.setSongChart(0, u"{comment: Verse}\nI need your love"_s));
        settle();
        QQuickItem* scene = w->contentItem();
        QVERIFY(findItem(scene, u"editChartButton"_s) == nullptr); // no Edit / Done
        const auto centre = [](QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        const auto cellOf = [scene](const QString& text) -> QQuickItem* {
            QList<QQuickItem*> all;
            findAll(scene, u"chartCell"_s, all);
            const auto it = std::ranges::find_if(all, [&text](QQuickItem* c) { return c->property("modelData").toMap().value(u"text"_s).toString() == text; });
            return it == all.end() ? nullptr : *it;
        };
        const auto part = [&cellOf](const QString& text, const QString& name) { QQuickItem* c = cellOf(text); return c != nullptr ? findItem(c, name) : nullptr; };
        const auto visibleCount = [scene](const QString& name) {
            QList<QQuickItem*> all;
            findAll(scene, name, all);
            return std::ranges::count_if(all, [](QQuickItem* i) { return i->isVisible(); });
        };
        // Typed key by key, as a player types (QTest has no keyClicks for a window).
        const auto typeKeys = [w](const QString& text) {
            for (const QChar c : text) QTest::keyClick(w, c.toLatin1());
        };
        const auto drag = [w](QPoint from, QPoint to) {
            QTest::mousePress(w, Qt::LeftButton, {}, from);
            for (int i = 1; i <= 12; ++i) {
                QTest::mouseMove(w, from + (to - from) * i / 12);
                QTest::qWait(10);
            }
            QTest::mouseRelease(w, Qt::LeftButton, {}, to);
            QTest::qWait(50);
        };

        // A click on the line opens it: a bar over each word (no chords yet).
        QCOMPARE(visibleCount(u"chordBar"_s), 0);
        QVERIFY(part(u"love"_s, u"chartWord"_s) != nullptr);
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(part(u"love"_s, u"chartWord"_s)));
        QTRY_COMPARE(visibleCount(u"chordBar"_s), 4);

        // "love" double-clicked and changed where it is: "love baby".
        QTest::mouseDClick(w, Qt::LeftButton, {}, centre(part(u"love"_s, u"chartWord"_s)));
        QTRY_VERIFY(part(u"love"_s, u"wordEditInput"_s) != nullptr && part(u"love"_s, u"wordEditInput"_s)->isVisible());
        typeKeys(u"love baby"_s); // (the word is selected: typed over)
        QTest::keyClick(w, Qt::Key_Return);
        QCOMPARE(doc.currentChart(), u"{comment: Verse}\nI need your love baby"_s);
        settle();

        // A chord typed in the box over "love"; Tab: the next box ("baby").
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(part(u"love"_s, u"chordBox"_s)));
        QTRY_VERIFY(part(u"love"_s, u"chordEditInput"_s) != nullptr && part(u"love"_s, u"chordEditInput"_s)->isVisible());
        typeKeys(u"Gm"_s);
        QTest::keyClick(w, Qt::Key_Tab);
        QCOMPARE(doc.currentChart(), u"{comment: Verse}\nI need your [Gm]love baby"_s);
        QTRY_VERIFY(part(u"baby"_s, u"chordEditInput"_s) != nullptr && part(u"baby"_s, u"chordEditInput"_s)->isVisible());
        typeKeys(u"C"_s);
        QTest::keyClick(w, Qt::Key_Return);
        QCOMPARE(doc.currentChart(), u"{comment: Verse}\nI need your [Gm]love [C]baby"_s);
        settle();

        // A chord clicked and changed: Gm to Gm7.
        QList<QQuickItem*> chips;
        findAll(scene, u"chartChordChip"_s, chips);
        QCOMPARE(chips.size(), 2);
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(chips.at(0)));
        QTRY_VERIFY(part(u"love"_s, u"chordEditInput"_s)->isVisible());
        QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
        typeKeys(u"Gm7"_s);
        QTest::keyClick(w, Qt::Key_Return);
        QCOMPARE(doc.currentChart(), u"{comment: Verse}\nI need your [Gm7]love [C]baby"_s);
        settle();

        // Dragged from "love" to "your"; the C to Remove.
        chips.clear();
        findAll(scene, u"chartChordChip"_s, chips);
        drag(centre(chips.at(0)), centre(part(u"your"_s, u"chordBox"_s)));
        QCOMPARE(doc.currentChart(), u"{comment: Verse}\nI need [Gm7]your love [C]baby"_s);
        settle();
        chips.clear();
        findAll(scene, u"chartChordChip"_s, chips);
        auto* bin = findItem(scene, u"chordRemoveZone"_s);
        QVERIFY(bin != nullptr && chips.size() == 2);
        drag(centre(chips.at(1)), centre(bin));
        QCOMPARE(doc.currentChart(), u"{comment: Verse}\nI need [Gm7]your love baby"_s);
        settle();

        // From the palette: typed in the chord field, dropped on "I".
        auto* field = findItem(scene, u"chordField"_s);
        QVERIFY(field != nullptr);
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(field));
        typeKeys(u"D"_s);
        settle();
        QList<QQuickItem*> palette;
        findAll(scene, u"paletteChord"_s, palette);
        QCOMPARE(palette.size(), 2); // the typed D, and the song's Gm7
        QCOMPARE(palette.at(0)->property("chordName").toString(), u"D"_s);
        drag(centre(palette.at(0)), centre(part(u"I"_s, u"chordBox"_s)));
        QCOMPARE(doc.currentChart(), u"{comment: Verse}\n[D]I need [Gm7]your love baby"_s);
        settle();

        // The section renamed: double-click its title, type, Enter.
        auto* title = findItem(scene, u"sectionTitle"_s);
        QVERIFY(title != nullptr);
        QTest::mouseDClick(w, Qt::LeftButton, {}, centre(title));
        QTest::qWait(50);
        auto* titleInput = findItem(scene, u"sectionTitleInput"_s);
        QVERIFY(titleInput != nullptr && titleInput->isVisible());
        QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
        typeKeys(u"Verse 1"_s);
        QTest::keyClick(w, Qt::Key_Return);
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[D]I need [Gm7]your love baby"_s);
        settle();
        shoot(u"chart-live-edit"_s);

        // A new line typed at the end; Enter in it saves it and starts another.
        auto* newLine = findItem(scene, u"newChartLine"_s);
        QVERIFY(newLine != nullptr && newLine->isVisible());
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(newLine));
        typeKeys(u"Hold me"_s);
        QTest::keyClick(w, Qt::Key_Return);
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[D]I need [Gm7]your love baby\nHold me\n"_s);
        typeKeys(u"tight"_s);
        // Backspace at the start of a line: saved, then joined to the one above.
        QTest::keyClick(w, Qt::Key_Home);
        QTest::keyClick(w, Qt::Key_Backspace);
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[D]I need [Gm7]your love baby\nHold metight"_s);

        // Ctrl+V while typing a line pastes into it, not a new chart.
        QVERIFY(QGuiApplication::clipboard() != nullptr);
        QGuiApplication::clipboard()->setText(u"X"_s);
        QTest::keyClick(w, Qt::Key_V, Qt::ControlModifier);
        auto* lineText = findItem(scene, u"lineTextInput"_s);
        QList<QQuickItem*> typed;
        findAll(scene, u"lineTextInput"_s, typed);
        const auto shown = std::ranges::find_if(typed, [](QQuickItem* i) { return i->isVisible(); });
        QVERIFY(lineText != nullptr && shown != typed.end());
        QCOMPARE((*shown)->property("text").toString(), u"Hold meXtight"_s);
        QCOMPARE(doc.currentChart(), u"{comment: Verse 1}\n[D]I need [Gm7]your love baby\nHold metight"_s); // (saved on Enter)
    }

    // The Practice tab: the song's chords falling onto the keyboard, each
    // note over its key; Play moves them down; the modes are buttons.
    // Practice is a mode of its own beside Edit and Perform (the top bar's
    // switch), not a tab of the Edit view.
    void thePracticeModeShowsTheNotesFalling()
    {
        QObject* root = m_qml->rootObjects().value(0);
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.setSongChart(0, u"{comment: Verse}\n[C]a [F]b [G]c [Am]d\n{comment: Chorus}\n[F]e [G]f [C]g\n"_s));
        QQuickItem* scene = window()->contentItem();
        QVERIFY(findItem(scene, u"practiceTab"_s) == nullptr);
        auto* practiceButton = window()->findChild<QObject*>(u"practiceButton"_s);
        QVERIFY(practiceButton != nullptr);
        QVERIFY(QMetaObject::invokeMethod(practiceButton, "clicked"));
        settle();
        QVERIFY(root->property("practiceMode").toBool());
        QVERIFY(!root->property("performMode").toBool());
        auto* view = findItem(scene, u"practiceView"_s);
        QVERIFY(view != nullptr && view->isVisible());
        auto* chartTabs = findItem(scene, u"mainTabs"_s);
        QVERIFY(chartTabs == nullptr || !chartTabs->isVisible()); // the Edit view is gone
        auto* bottomKeys = findItem(scene, u"keyboardView"_s);
        QVERIFY(bottomKeys == nullptr || !bottomKeys->isVisible()); // its own keyboard instead
        QList<QQuickItem*> notes;
        findAll(scene, u"practiceNote"_s, notes);
        QCOMPARE(notes.size(), 28); // 7 chords, 4 notes each
        // The first chord's middle C falls over the C4 key.
        auto* key = findItem(scene, u"practiceKey60"_s);
        QVERIFY(key != nullptr);
        // The keys keep a piano's shape: a wide window shows more of them, not wider ones.
        QVERIFY2(key->width() <= 40.0 && key->height() >= key->width() * 3.5,
                 qPrintable(u"key %1 x %2"_s.arg(key->width()).arg(key->height())));
        QVERIFY(findItem(scene, u"practiceKey35"_s) != nullptr); // below C2: the window is filled
        QQuickItem* middleC = nullptr;
        for (QQuickItem* note : notes) {
            const QVariantMap data = note->property("modelData").toMap();
            if (data.value(u"pitch"_s).toInt() == 60 && data.value(u"chord"_s).toInt() == 0) middleC = note;
        }
        QVERIFY(middleC != nullptr && middleC->isVisible());
        const double noteCentre = middleC->mapToScene(QPointF(middleC->width() / 2, 0)).x();
        const double keyCentre = key->mapToScene(QPointF(key->width() / 2, 0)).x();
        QVERIFY2(qAbs(noteCentre - keyCentre) <= 2.0, qPrintable(u"%1 vs %2"_s.arg(noteCentre).arg(keyCentre)));
        shoot(u"practice"_s);

        // Play: the notes come down; the chord now is shown.
        const double before = middleC->y();
        auto* play = findItem(scene, u"practicePlay"_s);
        QVERIFY(play != nullptr);
        auto* listen = findItem(scene, u"practiceMode1"_s); // Play along: the demo engine plays nothing anyway
        QVERIFY(QMetaObject::invokeMethod(listen, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(play, "clicked"));
        QTRY_VERIFY(middleC->y() > before + 20);
        QTRY_COMPARE(findItem(scene, u"practiceNow"_s)->property("text").toString(), u"C"_s);
        // The notes landing now sparkle where they hit the keys (YouTube-style).
        QList<QQuickItem*> sparks;
        findAll(scene, u"practiceSpark"_s, sparks);
        const auto shown = std::ranges::count_if(sparks, [](QQuickItem* s) { return s->isVisible(); });
        QCOMPARE(shown, 4); // the C chord: C2; C4, E4, G4
        shoot(u"practice-playing"_s);
        // Back to Edit: it pauses.
        auto* editButton = window()->findChild<QObject*>(u"editButton"_s);
        QVERIFY(editButton != nullptr);
        QVERIFY(QMetaObject::invokeMethod(editButton, "clicked"));
        settle();
        QVERIFY(!root->property("practiceMode").toBool());
        QVERIFY(!m_session->practice().playing());
    }

    // Help > User guide: the guide opens at a page, finds pages by their
    // words and follows the links between them; Help > About names the app.
    void theHelpMenuOpensTheGuideAndAbout()
    {
        QObject* root = m_qml->rootObjects().value(0);
        QVERIFY(root->findChild<QObject*>(u"helpButton"_s) != nullptr);

        QVERIFY(QMetaObject::invokeMethod(root, "openHelp", Q_ARG(QVariant, u"practice"_s)));
        settle();
        auto* guide = root->findChild<QQuickWindow*>(u"helpWindow"_s);
        QVERIFY(guide != nullptr);
        QTRY_VERIFY(guide->isVisible());
        auto* page = root->findChild<QObject*>(u"helpPage"_s);
        QVERIFY(page != nullptr);
        QVERIFY(page->property("markdown").toString().startsWith(u"# Practice mode"_s));
        QVERIFY(page->property("text").toString().contains(u"Practice mode"_s)); // shown
        auto* topicList = root->findChild<QObject*>(u"helpTopics"_s);
        QVERIFY(topicList != nullptr);
        QVERIFY(topicList->property("count").toInt() >= 10);
        if (qEnvironmentVariableIsSet("GIGCHAIN_SCREENSHOTS")) {
            guide->grabWindow().save(qEnvironmentVariable("GIGCHAIN_SCREENSHOTS") + u"/help.png"_s);
        }

        // A link to another page goes there.
        QVERIFY(QMetaObject::invokeMethod(page, "linkActivated", Q_ARG(QString, u"charts.md"_s)));
        QTRY_VERIFY(page->property("markdown").toString().startsWith(u"# Chord charts"_s));

        // Search lists the pages holding the words; picking one opens it.
        auto* search = root->findChild<QObject*>(u"helpSearch"_s);
        QVERIFY(search != nullptr);
        QVERIFY(search->setProperty("text", u"wait for me"_s));
        auto* results = root->findChild<QObject*>(u"helpResults"_s);
        QVERIFY(results != nullptr);
        QTRY_VERIFY(results->property("count").toInt() >= 1);
        QVERIFY(results->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject*>(u"helpWindowRoot"_s), "openTopic", Q_ARG(QVariant, u"looper"_s)));
        QTRY_VERIFY(page->property("markdown").toString().startsWith(u"# Loop station"_s));
        guide->close();

        // About: the splash picture, the name and the version.
        QVERIFY(QMetaObject::invokeMethod(root, "openAbout"));
        settle();
        auto* about = root->findChild<QObject*>(u"aboutDialog"_s);
        QVERIFY(about != nullptr);
        QTRY_VERIFY(about->property("visible").toBool());
        auto* version = root->findChild<QObject*>(u"aboutVersion"_s);
        QVERIFY(version != nullptr);
        QVERIFY(version->property("text").toString().contains(gigchain::branding::version()));
        shoot(u"about"_s);
        QVERIFY(QMetaObject::invokeMethod(about, "close"));

        // Help > Get free instruments: each with its download page.
        auto* menuItem = root->findChild<QObject*>(u"freeInstrumentsItem"_s);
        QVERIFY(menuItem != nullptr);
        QVERIFY(QMetaObject::invokeMethod(menuItem, "triggered"));
        auto* free = root->findChild<QObject*>(u"freeInstrumentsDialog"_s); // (a popup, not an item)
        QVERIFY(free != nullptr);
        QTRY_VERIFY(free->property("visible").toBool());
        settle();
        const QList<QQuickItem*> entries = findAll(window()->contentItem(), u"freeInstrument"_s); // (dialogs sit in the overlay, under it)
        QStringList urls;
        for (QQuickItem* entry : entries) {
            if (entry->isVisible()) urls << entry->property("url").toString();
        }
        urls.removeDuplicates();
        QVERIFY2(urls.size() >= 5, qPrintable(urls.join(u", "_s)));
        QVERIFY(urls.contains(u"https://splice.com/instrument/labs-instrument"_s));
        QVERIFY(urls.contains(u"https://surge-synthesizer.github.io/"_s));
        for (const QString& url : urls) QVERIFY2(url.startsWith(u"https://"_s), qPrintable(url));
        shoot(u"free-instruments"_s);
        QVERIFY(QMetaObject::invokeMethod(free, "close"));
    }

    // No instruments installed: the Instruments panel shows free ones to
    // get instead of an empty list; with some, the list.
    void noInstrumentsShowsFreeOnesToGet()
    {
        QQuickWindow* w = window();
        auto* tabs = m_qml->rootObjects().value(0)->findChild<QObject*>(u"sidePanelTabs"_s);
        QVERIFY(tabs != nullptr);
        tabs->setProperty("currentIndex", 1); // the Instruments tab
        settle();
        auto* none = w->findChild<QQuickItem*>(u"noInstruments"_s);
        auto* list = w->findChild<QQuickItem*>(u"pluginList"_s);
        QVERIFY(none != nullptr && list != nullptr);
        QVERIFY(list->property("count").toInt() > 0); // (the demo engine has instruments)
        QVERIFY(!none->isVisible());
        QVERIFY(list->isVisible());
        // A search with no match is not "none installed".
        auto* search = w->findChild<QQuickItem*>(u"pluginSearch"_s);
        QVERIFY(search->setProperty("text", u"zzzz no such instrument"_s));
        settle();
        QCOMPARE(list->property("count").toInt(), 0);
        QVERIFY(!none->isVisible());
        QVERIFY(search->setProperty("text", QString()));
    }

    // The computer keyboard plays: Space plays and stops the song, the
    // arrows change sounds and songs, single letters for the rest. Never
    // while typing.
    void theComputerKeyboardPlaysTheSong()
    {
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addPatch(0));
        QVERIFY(doc.addSong());
        QVERIFY(doc.setSongChart(0, u"{c: Verse}\n[C]a [G]b\n{c: Chorus}\n[F]c\n"_s));
        QVERIFY(doc.selectPatch(0, 0));
        QQuickWindow* w = window();
        QVERIFY(w != nullptr);
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));
        auto* status = m_qml->rootObjects().value(0)->property("engineStatus").value<QObject*>();
        QVERIFY(status != nullptr);

        // Typing: a space is a space.
        auto* tabs = m_qml->rootObjects().value(0)->findChild<QObject*>(u"sidePanelTabs"_s);
        QVERIFY(tabs != nullptr);
        tabs->setProperty("currentIndex", 1); // Plugins tab
        QTest::qWait(20);
        auto* field = m_qml->rootObjects().value(0)->findChild<QQuickItem*>(u"pluginSearch"_s);
        QVERIFY(field != nullptr);
        field->forceActiveFocus();
        QTest::keyClick(w, Qt::Key_Space);
        QTest::keyClick(w, Qt::Key_C);
        QVERIFY(!m_engine->songPosition().playing);
        QVERIFY(!status->property("clickOn").toBool());
        QTest::keyClick(w, Qt::Key_Return); // finish editing: focus leaves the field
        QVERIFY(!field->hasActiveFocus());

        QTest::keyClick(w, Qt::Key_Space);
        QVERIFY(m_engine->songPosition().playing);
        QTest::keyClick(w, Qt::Key_Space);
        QVERIFY(!m_engine->songPosition().playing);
        QTest::keyClick(w, Qt::Key_Right);
        QCOMPARE(doc.patchIndex(), 1);
        QTest::keyClick(w, Qt::Key_Left);
        QCOMPARE(doc.patchIndex(), 0);
        QTest::keyClick(w, Qt::Key_Down);
        QCOMPARE(doc.songIndex(), 1);
        QTest::keyClick(w, Qt::Key_Up);
        QCOMPARE(doc.songIndex(), 0);
        QTest::keyClick(w, Qt::Key_N); // the next section
        QCOMPARE(m_engine->songPosition().section, 1);
        QTest::keyClick(w, Qt::Key_C);
        QVERIFY(status->property("clickOn").toBool());
        QTest::keyClick(w, Qt::Key_C);
        QVERIFY(!status->property("clickOn").toBool());
        QTest::keyClick(w, Qt::Key_M);
        QVERIFY(status->property("masterMuted").toBool());
        QTest::keyClick(w, Qt::Key_M);
        QVERIFY(!status->property("masterMuted").toBool());
        QTest::keyClick(w, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(doc.setlist().songs.size(), 3u);
        settle();
    }

    // Shortcuts shown the way each computer names them: Ctrl+Z here, ⌘Z on a Mac.
    void shortcutsAreShownTheMacWayOnAMac()
    {
        QQmlComponent component(m_qml.get());
        component.setData("import QtQuick\nimport GigChain.Ui\nQtObject {\n"
                          "  property string redo: Theme.keys('Ctrl+Shift+Z', true)\n"
                          "  property string settings: Theme.keys('Ctrl+,', true)\n"
                          "  property string help: Theme.keys('F1', true)\n"
                          "  property string space: Theme.keys('Space', true)\n"
                          "  property string here: Theme.keys('Ctrl+Shift+Z', false)\n}",
                          QUrl());
        std::unique_ptr<QObject> keys(component.create());
        QVERIFY2(keys != nullptr, qPrintable(component.errorString()));
        QCOMPARE(keys->property("redo").toString(), u"⇧⌘Z"_s);
        QCOMPARE(keys->property("settings").toString(), u"⌘,"_s);
        QCOMPARE(keys->property("help").toString(), u"⌘?"_s);
        QCOMPARE(keys->property("space").toString(), u"Space"_s);
        QCOMPARE(keys->property("here").toString(), u"Ctrl+Shift+Z"_s);
    }

    // What was clicked last is what Delete, F2 and Ctrl+D act on: a song in
    // the list, a channel strip. Ctrl+Z brings it back.
    void aClickedSongOrChannelIsEditedFromTheKeyboard()
    {
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.renameSong(0, u"One"_s));
        QVERIFY(doc.addSong());
        QVERIFY(doc.addSong());
        QVERIFY(doc.selectPatch(0, 0));
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));
        settle();
        // The second song's row (delegates are visual children only).
        const auto rowOf = [w](int song) -> QQuickItem* {
            QList<QQuickItem*> todo{w->contentItem()};
            while (!todo.isEmpty()) {
                QQuickItem* item = todo.takeLast();
                if (item->objectName() == u"setlistRow"_s && item->isVisible() && item->property("songIndex").toInt() == song) return item;
                todo << item->childItems();
            }
            return nullptr;
        };
        const auto centre = [](QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        QQuickItem* second = rowOf(1);
        QVERIFY(second != nullptr);
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(second));
        settle();
        QCOMPARE(doc.songIndex(), 1);
        QTest::keyClick(w, Qt::Key_Delete);
        QCOMPARE(doc.setlist().songs.size(), 2u);
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(doc.setlist().songs.size(), 3u);
        // A Mac's delete key is Backspace; ⌘delete arrives as Ctrl+Backspace.
        QTest::keyClick(w, Qt::Key_Backspace, Qt::ControlModifier);
        QCOMPARE(doc.setlist().songs.size(), 2u);
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(doc.setlist().songs.size(), 3u);
        QTest::keyClick(w, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(doc.setlist().songs.size(), 4u);

        // F2: the selected song's name, typed in place.
        QVERIFY(doc.selectPatch(0, 0));
        settle();
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(rowOf(0)));
        QTest::keyClick(w, Qt::Key_F2);
        QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
        for (const QChar c : u"Opener"_s) QTest::keyClick(w, c.toLatin1());
        QTest::keyClick(w, Qt::Key_Return);
        QCOMPARE(doc.currentSongName(), u"Opener"_s);

        // A channel strip clicked: Delete removes the channel.
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Piano"_s));
        settle();
        QQuickItem* slot = stripChild(u"instrumentSlot"_s);
        QVERIFY(slot != nullptr);
        QTest::mouseClick(w, Qt::LeftButton, {}, centre(slot));
        settle();
        QCOMPARE(doc.selectedChannel(), 0);
        QTest::keyClick(w, Qt::Key_Delete);
        settle();
        QVERIFY(doc.currentPatch()->channels.empty());
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        settle();
        QCOMPARE(doc.currentPatch()->channels.size(), 1u);
    }

    // Before Play, the song's first chord is outlined and nothing is lit.
    void aChartOutlinesItsFirstChord()
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
        QTRY_VERIFY(!findAll(chart, u"chartChordNext"_s).isEmpty()); // the first chord, outlined
        QVERIFY(findAll(chart, u"chartChordCurrent"_s).isEmpty()); // nothing lit before Play
    }
};

QTEST_MAIN(TestQmlSmoke)
#include "tst_qml_smoke.moc"
