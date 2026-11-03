// Instruments in windows of their own (InstrumentWindows), with the views
// the app has: the Instrument tab's, and one per window (as the QML windows
// make them). Every plugin window the engine hands out is counted: a plugin
// is never open in two places, not even for a moment.
#include "DocumentController.h"
#include "EditorService.h"
#include "InstrumentWindows.h"
#include "PluginEditorHost.h"
#include "SpyEngine.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <exception>
#include <map>
#include <memory>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

// A view of a service in a window of its own, as the app shows them.
struct View
{
    std::unique_ptr<QQuickWindow> window;
    PluginEditorHost* host = nullptr; // owned by the window's content
};

std::unique_ptr<View> makeView(EditorService& service)
{
    auto view = std::make_unique<View>();
    view->window = std::make_unique<QQuickWindow>();
    view->window->resize(900, 700);
    view->window->show();
    view->host = new PluginEditorHost(view->window->contentItem());
    view->host->setSize(QSizeF(900, 640));
    view->host->setService(&service);
    return view;
}

} // namespace

class TestInstrumentWindows : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<test::SpyEngine> m_engine;
    std::unique_ptr<DocumentController> m_doc;
    std::unique_ptr<EditorService> m_tabService;
    std::unique_ptr<InstrumentWindows> m_windows;
    std::unique_ptr<View> m_tab;
    std::map<const QObject*, std::unique_ptr<View>> m_open; // the windows' views, by their service

    [[nodiscard]] QString idOf(int channel) const { return m_doc->currentPatch()->channels.at(static_cast<std::size_t>(channel)).id.value(); }
    [[nodiscard]] int open(int channel) const
    {
        const auto it = m_engine->openEditors.find(idOf(channel));
        return it != m_engine->openEditors.end() ? it->second : 0;
    }
    [[nodiscard]] int most(int channel) const
    {
        const auto it = m_engine->mostEditors.find(idOf(channel));
        return it != m_engine->mostEditors.end() ? it->second : 0;
    }
    // The open windows, as the QML Instantiator keeps them: a view for each.
    void syncViews()
    {
        const QList<QObject*> now = m_windows->windows();
        std::erase_if(m_open, [&now](const auto& each) { return !now.contains(each.first); });
        for (QObject* service : now) {
            if (!m_open.contains(service)) m_open[service] = makeView(*qobject_cast<EditorService*>(service));
        }
    }
    [[nodiscard]] EditorService* window(int index) const { return qobject_cast<EditorService*>(m_windows->windows().value(index)); }
    [[nodiscard]] PluginEditorHost* hostOf(const QObject* service) const { return m_open.at(service)->host; }
    void startWindows()
    {
        m_windows = std::make_unique<InstrumentWindows>(*m_engine, *m_doc, *m_tabService, *m_settings);
        connect(m_windows.get(), &InstrumentWindows::windowsChanged, this, &TestInstrumentWindows::syncViews);
        syncViews();
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"s.ini"_s), QSettings::IniFormat);
        m_engine = std::make_unique<test::SpyEngine>();
        m_engine->editorsWithWindows = true;
        m_doc = std::make_unique<DocumentController>(*m_engine, *m_settings);
        m_doc->newSetlist();
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->addChannel(u"spy/Pad.vst3"_s, u"Spy Pad"_s));
        m_doc->setSelectedChannel(0);
        m_tabService = std::make_unique<EditorService>(*m_engine, *m_doc);
        startWindows();
        m_tab = makeView(*m_tabService);
        QTRY_VERIFY(m_tab->host->hasEditor());
        QCOMPARE(open(0), 1);
    }

    void cleanup()
    {
        m_open.clear();
        m_tab.reset();
        m_windows.reset();
        m_tabService.reset();
        m_doc.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    void poppingOutMovesThePluginFromTheTabToTheWindow()
    {
        m_windows->popOut();
        // The tab let it go before the window opened it.
        QVERIFY(m_tabService->shownElsewhere());
        QVERIFY(!m_tab->host->hasEditor());
        QVERIFY(m_windows->linkedOpen());
        QCOMPARE(m_windows->windows().size(), 1);
        QTRY_VERIFY(hostOf(window(0))->hasEditor());
        QCOMPARE(open(0), 1);
        QCOMPARE(most(0), 1); // never two at once
        QCOMPARE(window(0)->channelName(), u"Spy Piano"_s);
        // The window takes the plugin's own size.
        QCOMPARE(hostOf(window(0))->editorSize(), QSize(640, 400) / m_open.at(window(0))->window->devicePixelRatio());
    }

    void theLinkedWindowFollowsTheSelectedChannel()
    {
        m_windows->popOut();
        QTRY_VERIFY(hostOf(window(0))->hasEditor());
        m_doc->setSelectedChannel(1);
        QTRY_COMPARE(open(1), 1);
        QCOMPARE(open(0), 0);
        QCOMPARE(window(0)->channelName(), u"Spy Pad"_s);
        QVERIFY(!m_tab->host->hasEditor()); // still in the window, not the tab
        QCOMPARE(most(0), 1);
        QCOMPARE(most(1), 1);
    }

    void dockingBringsItBackToTheTab()
    {
        m_windows->popOut();
        QTRY_VERIFY(hostOf(window(0))->hasEditor());
        m_doc->setSelectedChannel(1);
        QTRY_COMPARE(open(1), 1);
        m_doc->setSelectedChannel(0); // (the window moves back to the piano)
        QTRY_COMPARE(open(0), 1);
        QSignalSpy docked(m_windows.get(), &InstrumentWindows::docked);
        m_windows->dock(window(0));
        QCOMPARE(docked.count(), 1);
        QVERIFY(!m_windows->linkedOpen());
        QVERIFY(m_windows->windows().isEmpty());
        QTRY_VERIFY(m_tab->host->hasEditor());
        QCOMPARE(open(0), 1);
        QCOMPARE(most(0), 1);
        QCOMPARE(m_doc->selectedChannel(), 0);
    }

    void aKeptWindowStaysOnItsChannel()
    {
        m_windows->openFor(0);
        QVERIFY(m_windows->windows().size() == 1 && !window(0)->linked());
        QTRY_VERIFY(hostOf(window(0))->hasEditor());
        QVERIFY(!m_tab->host->hasEditor()); // the piano is in its window
        m_doc->setSelectedChannel(1);
        QTRY_VERIFY(m_tab->host->hasEditor()); // the tab shows the pad
        QCOMPARE(window(0)->channelName(), u"Spy Piano"_s);
        QCOMPARE(open(0), 1);
        QCOMPARE(open(1), 1);
        m_doc->setSelectedChannel(0); // the piano again: still in its window
        QTRY_VERIFY(!m_tab->host->hasEditor());
        QVERIFY(m_tabService->shownElsewhere());
        QCOMPARE(most(0), 1);
        QCOMPARE(most(1), 1);
        // Opening it again brings that window to the front, no second one.
        QSignalSpy raised(m_windows.get(), &InstrumentWindows::raiseRequested);
        m_windows->openFor(0);
        QCOMPARE(raised.count(), 1);
        QCOMPARE(m_windows->windows().size(), 1);
    }

    void aKeptWindowComesBeforeTheLinkedOne()
    {
        m_windows->openFor(0); // the piano, kept
        m_windows->popOut();   // linked: the selected channel, the piano too
        QCOMPARE(m_windows->windows().size(), 2);
        const EditorService* kept = window(0);
        EditorService* linked = window(1);
        QTRY_VERIFY(hostOf(kept)->hasEditor());
        QVERIFY(linked->shownElsewhere());
        QVERIFY(!hostOf(linked)->hasEditor());
        m_doc->setSelectedChannel(1);
        QTRY_VERIFY(hostOf(linked)->hasEditor()); // the pad
        m_doc->setSelectedChannel(0);
        QTRY_VERIFY(!hostOf(linked)->hasEditor());
        QCOMPARE(open(0), 1);
        QCOMPARE(most(0), 1);
        // "Show window" on the linked one: the kept window to the front.
        QSignalSpy raised(m_windows.get(), &InstrumentWindows::raiseRequested);
        m_windows->showWindowOf(linked);
        QCOMPARE(raised.count(), 1);
        QVERIFY(raised.at(0).at(0).value<QObject*>() == kept);
        // "Bring it back" from the tab closes both in the way.
        m_windows->bringBack(m_tabService.get());
        QVERIFY(m_windows->windows().isEmpty());
        QTRY_VERIFY(m_tab->host->hasEditor());
        QCOMPARE(most(0), 1);
    }

    void linkOffKeepsTheWindowOnWhatItShows()
    {
        m_windows->popOut();
        EditorService* first = window(0);
        QTRY_VERIFY(hostOf(first)->hasEditor());
        const std::size_t asked = m_engine->editorRequests.size();
        QVERIFY(m_windows->setLinked(first, false));
        QVERIFY(!first->linked());
        QVERIFY(!m_windows->linkedOpen());
        QTest::qWait(20);
        QCOMPARE(m_engine->editorRequests.size(), asked); // the same plugin window: not reopened
        QVERIFY(hostOf(first)->hasEditor());
        m_doc->setSelectedChannel(1);
        QTRY_VERIFY(m_tab->host->hasEditor());
        QCOMPARE(first->channelName(), u"Spy Piano"_s);
        // Another linked window; then the first may not link too.
        m_windows->popOut();
        QVERIFY(!m_windows->canLink(first));
        QVERIFY(!m_windows->setLinked(first, true));
        QVERIFY(m_windows->canLink(window(1)));
        QCOMPARE(most(0), 1);
        QCOMPARE(most(1), 1);
    }

    void aKeptWindowWaitsWhileItsChannelIsNotInTheSong()
    {
        m_windows->openFor(0);
        const EditorService* kept = window(0);
        QTRY_VERIFY(hostOf(kept)->hasEditor());
        QVERIFY(m_doc->addSong()); // another song, now the current one
        QTRY_VERIFY(!hostOf(kept)->hasEditor());
        QCOMPARE(kept->emptyReason(), u"Spy Piano is not in this song"_s);
        QCOMPARE(kept->channelName(), u"Spy Piano"_s);
        QCOMPARE(m_windows->windows().size(), 1); // the window waits where it is
        m_doc->previousSong();
        QTRY_VERIFY(hostOf(kept)->hasEditor());
        QCOMPARE(most(0), 1);
    }

    void theLinkedWindowIsRememberedOpen()
    {
        m_windows->popOut();
        m_windows->rememberGeometry(QRect(2100, 120, 800, 500), true);
        m_open.clear();
        m_windows.reset();
        startWindows();
        QVERIFY(m_windows->linkedOpen());
        m_windows->dock(window(0));
        m_open.clear();
        m_windows.reset();
        startWindows();
        QVERIFY(!m_windows->linkedOpen());
    }

    void onTopIsRemembered()
    {
        QVERIFY(m_windows->onTop()); // (as effects' windows: above the main window)
        m_windows->setOnTop(false);
        m_windows.reset();
        startWindows();
        QVERIFY(!m_windows->onTop());
    }

    void aWindowOnAScreenThatIsGoneOpensOnTheMainWindow()
    {
        const QList<QRect> two{QRect(0, 0, 1920, 1040), QRect(1920, 0, 2560, 1400)};
        const QList<QRect> one{QRect(0, 0, 1920, 1040)};
        const QRect onSecond(2100, 120, 1000, 700);
        QCOMPARE(InstrumentWindows::onScreens(onSecond, two), onSecond);
        QCOMPARE(InstrumentWindows::onScreens(onSecond, one), QRect()); // unplugged: not off in nowhere
        // Hanging off an edge but grabbable: kept, moved fully onto the screen.
        QCOMPARE(InstrumentWindows::onScreens(QRect(1500, 100, 800, 600), one), QRect(1120, 100, 800, 600));
        // Only its bottom on a screen (title bar above it): not grabbable.
        QCOMPARE(InstrumentWindows::onScreens(QRect(100, -650, 800, 700), one), QRect());
        // Bigger than the screen it is on now: shrunk to it.
        QCOMPARE(InstrumentWindows::onScreens(QRect(0, 0, 2400, 1300), one), QRect(0, 0, 1920, 1040));
        QCOMPARE(InstrumentWindows::onScreens(QRect(), two), QRect());
    }

    void closingTwiceAndUnknownChannelsAreHarmless()
    {
        m_windows->openFor(7); // no such channel: nothing opens (logged)
        QVERIFY(m_windows->windows().isEmpty());
        m_windows->popOut();
        QObject* first = window(0);
        m_windows->close(first);
        m_windows->close(first);
        m_windows->dock(first);
        QVERIFY(m_windows->windows().isEmpty());
        QTRY_VERIFY(m_tab->host->hasEditor());
        QCOMPARE(most(0), 1);
    }
};

int main(int argc, char** argv)
{
    // Anything thrown fails the run, saying what it was.
    try {
        const QGuiApplication app(argc, argv); // (windows)
        TestInstrumentWindows test;
        return QTest::qExec(&test, argc, argv);
    } catch (const std::exception& e) {
        qCritical().noquote() << "FAIL: stopped by an error:" << e.what();
    } catch (...) {
        qCritical() << "FAIL: stopped by an error of an unknown kind";
    }
    return 1;
}

#include "tst_instrument_windows.moc"
