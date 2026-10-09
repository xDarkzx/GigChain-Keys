// Loads the real Main.qml with the demo engine and fails on any QML warning.
#include "Notifications.h"
#include "Session.h"
#include "StartupProgress.h"

#include "gigchain/core/Branding.h"
#include "gigchain/engine/FakeEngineFactory.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <QPainter>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <numbers>

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

// For the demo video (see demoVideo): the window, frame by frame, piped to
// ffmpeg at 30 frames a second of real time (a frame that took longer to
// draw is repeated, so the picture keeps time with the voice), and the
// subtitle of the moment shown on the overlay.
class SceneRecorder
{
public:
    static constexpr double kLead = 0.35; // seconds of picture before the voice starts
    static constexpr int kFps = 30;
    static constexpr int kWidth = 1920; // the frames
    static constexpr int kHeight = 1080;
    // The window: a laptop's screen, drawn 1.2 times over (QT_SCALE_FACTOR=1.2) so the frames are sharp and the text big.
    static constexpr int kWindowWidth = 1600;
    static constexpr int kWindowHeight = 900;

    SceneRecorder(QQuickWindow* window, QObject* overlay, QQuickWindow* toasts)
        : m_window(window), m_overlay(overlay), m_toasts(toasts)
    {
        m_timer.setInterval(5);
        QObject::connect(&m_timer, &QTimer::timeout, [this] { tick(); });
    }

    bool start(const QString& path, const QJsonArray& lines)
    {
        if (m_ffmpeg.state() == QProcess::NotRunning && !prepare(path)) return false;
        m_lines = lines;
        m_frames = 0;
        m_clock.start();
        m_timer.start();
        tick();
        return true;
    }

    // ffmpeg started, waiting for the first frame.
    bool prepare(const QString& path)
    {
        m_ffmpeg.start(u"ffmpeg"_s, {u"-y"_s, u"-loglevel"_s, u"error"_s, u"-f"_s, u"rawvideo"_s, u"-pix_fmt"_s, u"bgra"_s,
                                     u"-s"_s, u"%1x%2"_s.arg(kWidth).arg(kHeight), u"-r"_s, QString::number(kFps), u"-i"_s, u"-"_s,
                                     u"-c:v"_s, u"libx264"_s, u"-preset"_s, u"veryfast"_s, u"-crf"_s, u"14"_s, u"-pix_fmt"_s,
                                     u"yuv420p"_s, path});
        if (!m_ffmpeg.waitForStarted(10000)) {
            m_problem = u"ffmpeg did not start: "_s + m_ffmpeg.errorString();
            return false;
        }
        return true;
    }

    // Seconds since the scene started.
    [[nodiscard]] double now() const { return static_cast<double>(m_clock.elapsed()) / 1000.0; }

    bool stop()
    {
        tick();
        qInfo() << "scene recorded:" << m_frames << "frames, slowest grab" << m_grabMs << "ms";
        m_grabMs = 0;
        m_timer.stop();
        m_overlay->setProperty("sub", QString());
        m_ffmpeg.closeWriteChannel();
        if (!m_ffmpeg.waitForFinished(300000) || m_ffmpeg.exitCode() != 0) {
            m_problem = u"ffmpeg failed: "_s + QString::fromUtf8(m_ffmpeg.readAllStandardError());
            return false;
        }
        return true;
    }

    [[nodiscard]] const QString& problem() const { return m_problem; }
    [[nodiscard]] qint64 frames() const { return m_frames; }

private:
    void tick()
    {
        if (m_busy) return;
        m_busy = true;
        // The line being said: from its first word until the next line (or a moment after its last word).
        const double voice = now() - kLead;
        QString sub;
        for (qsizetype i = 0; i < m_lines.size(); ++i) {
            const QJsonObject line = m_lines.at(i).toObject();
            const double end = line.value(u"end"_s).toDouble() + 0.9;
            const double until = i + 1 < m_lines.size() ? std::min(end, m_lines.at(i + 1).toObject().value(u"start"_s).toDouble()) : end;
            if (voice >= line.value(u"start"_s).toDouble() - 0.05 && voice < until) sub = line.value(u"text"_s).toString();
        }
        if (m_overlay->property("sub").toString() != sub) m_overlay->setProperty("sub", sub);
        const auto due = static_cast<qint64>(now() * kFps) + 1;
        if (m_frames < due) {
            QElapsedTimer took;
            took.start();
            QImage frame = m_window->grabWindow();
            m_grabMs = std::max(m_grabMs, took.elapsed());
            // The messages are a window of their own, at the main window's bottom right: drawn in where they show.
            if (m_toasts != nullptr && m_toasts->isVisible() && m_toasts->height() > 1) {
                QPainter painter(&frame);
                painter.drawImage(QPoint(m_toasts->x() - m_window->x(), m_toasts->y() - m_window->y()), m_toasts->grabWindow());
            }
            if (frame.size() != QSize(kWidth, kHeight)) frame = frame.scaled(kWidth, kHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            frame = frame.convertToFormat(QImage::Format_RGB32); // (blue, green, red, 255 in memory: ffmpeg's bgra)
            QByteArray bytes(frame.sizeInBytes(), Qt::Uninitialized);
            std::memcpy(bytes.data(), frame.constBits(), static_cast<std::size_t>(frame.sizeInBytes()));
            for (; m_frames < due; ++m_frames) m_ffmpeg.write(bytes);
            // Written as ffmpeg takes them; waited for only when far behind (a wait stalls the app, and its notes come late).
            constexpr qint64 kFarBehind = qint64{400} * 1024 * 1024;
            while (m_ffmpeg.bytesToWrite() > kFarBehind && m_ffmpeg.waitForBytesWritten(10000)) {}
        }
        m_busy = false;
    }

    QQuickWindow* m_window;
    QObject* m_overlay;
    QQuickWindow* m_toasts;
    QProcess m_ffmpeg;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QJsonArray m_lines;
    qint64 m_frames = 0;
    qint64 m_grabMs = 0;
    bool m_busy = false;
    QString m_problem;
};

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

    // ---- The demo video's director (see demoVideo) ----------------------
    QObject* m_overlay = nullptr;
    SceneRecorder* m_recorder = nullptr;

    // Waits until `seconds` into the scene's voice.
    void at(double seconds) const
    {
        while (m_recorder->now() < seconds + SceneRecorder::kLead) QTest::qWait(4);
    }
    static QPointF centre(const QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)); }
    static QRectF rectOf(const QQuickItem* item) { return item->mapRectToScene(QRectF(0, 0, item->width(), item->height())); }
    // The pointer glides there.
    void glide(QPointF to, int ms = 600) const
    {
        m_overlay->setProperty("glide", ms);
        m_overlay->setProperty("pointer", to);
        QTest::qWait(ms + 40);
    }
    // The pointer glides to the item and clicks it (a ripple shows the click).
    void press(const QQuickItem* target, Qt::MouseButton button = Qt::LeftButton, int ms = 600)
    {
        QVERIFY(target != nullptr);
        glide(centre(target), ms);
        QVERIFY(QMetaObject::invokeMethod(m_overlay, "click"));
        QTest::mouseClick(window(), button, {}, centre(target).toPoint());
        QTest::qWait(30);
    }
    void spot(const QRectF& area) const
    {
        m_overlay->setProperty("spot", area);
        m_overlay->setProperty("spotOn", true);
    }
    void spot(const QQuickItem* target) const
    {
        QVERIFY(target != nullptr);
        spot(rectOf(target));
    }
    void unspot() const { m_overlay->setProperty("spotOn", false); }
    // The visible items with that name, in the visual tree's order.
    [[nodiscard]] QList<QQuickItem*> shown(const QString& name) const
    {
        QList<QQuickItem*> found = findAll(window()->contentItem(), name);
        found.removeIf([](const QQuickItem* item) { return !item->isVisible() || item->width() <= 0; });
        return found;
    }
    [[nodiscard]] QQuickItem* shownOne(const QString& name) const { return shown(name).value(0); }
    // The chart's chords that a tap opens (a tap handler is not an item: the chord it sits on is).
    [[nodiscard]] QList<QQuickItem*> tappableChords() const
    {
        QList<QQuickItem*> found;
        const std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
            for (const QObject* child : item->children()) {
                if (child->objectName() == u"chordTap"_s && child->property("enabled").toBool() && item->isVisible()) found << item;
            }
            for (QQuickItem* inner : item->childItems()) walk(inner);
        };
        walk(window()->contentItem());
        return found;
    }
    // The strips of the first `count` channels, together (as much as the mixer shows of them).
    [[nodiscard]] QRectF stripsArea(int count) const
    {
        QRectF area;
        for (int i = 0; i < count; ++i) {
            if (const QQuickItem* s = strip(i)) area = area.united(rectOf(s));
        }
        if (const auto* strips = window()->findChild<QQuickItem*>(u"mixerStrips"_s)) area = area.intersected(rectOf(strips));
        return area;
    }
    // The strip of a channel (delegates are not QObject children).
    [[nodiscard]] QQuickItem* strip(int channel) const
    {
        auto* strips = window()->findChild<QObject*>(u"mixerStrips"_s);
        QQuickItem* found = nullptr;
        if (strips != nullptr) QMetaObject::invokeMethod(strips, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, found), Q_ARG(int, channel));
        return found;
    }
    // Practice: each falling note played as it lands (and let go), until `done`.
    void playAlong(QList<QVariantMap>& toPlay, QList<std::pair<int, double>>& held, const std::function<bool()>& done)
    {
        const ui::PracticeController& practice = m_session->practice();
        while (!done()) {
            QTest::qWait(4);
            for (auto it = held.begin(); it != held.end();) {
                if (practice.position() < it->second) {
                    ++it;
                    continue;
                }
                m_engine->injectNote(1, it->first, 0);
                it = held.erase(it);
            }
            for (auto it = toPlay.begin(); it != toPlay.end();) {
                const double start = it->value(u"start"_s).toDouble();
                if (practice.position() + 0.01 < start) {
                    ++it;
                    continue;
                }
                m_engine->injectNote(1, it->value(u"pitch"_s).toInt(), 96);
                held << std::pair{it->value(u"pitch"_s).toInt(), start + (it->value(u"length"_s).toDouble() * 0.9)};
                it = toPlay.erase(it);
            }
        }
    }
    void playAlong(QList<QVariantMap>& toPlay, QList<std::pair<int, double>>& held, double untilSeconds)
    {
        playAlong(toPlay, held, [this, untilSeconds] { return m_recorder->now() >= untilSeconds + SceneRecorder::kLead; });
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
        QTRY_VERIFY(loopPart->isEnabled()); // (it opens once the screen has seen the song playing)
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

    // Right-click a strip's fader: Learn a keyboard knob; the banner says
    // what to do and Esc calls it off. The master fader too.
    void aFaderLearnsAKeyboardKnobFromItsMenu()
    {
        ui::DocumentController& doc = m_session->document();
        QVERIFY(doc.addChannel(u"demo.piano"_s, u"Piano"_s));
        QQuickWindow* w = window();
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));
        settle();
        auto* status = m_qml->rootObjects().value(0)->property("engineStatus").value<QObject*>();
        QVERIFY(status != nullptr);
        QQuickItem* fader = stripChild(u"faderKnobArea"_s);
        QVERIFY(fader != nullptr);
        QTest::mouseClick(w, Qt::RightButton, {}, fader->mapToScene(QPointF(fader->width() / 2, fader->height() / 2)).toPoint());
        // The menu that opened (a menu's rows are in the window's overlay while it is open).
        const auto openLearnItem = [w]() -> QQuickItem* {
            const QList<QQuickItem*> items = findAll(w->contentItem(), u"learnKnob"_s);
            const auto open = std::ranges::find_if(items, [](const QQuickItem* item) {
                return item->isVisible() && item->property("enabled").toBool();
            });
            return open != items.end() ? *open : nullptr;
        };
        QTRY_VERIFY(openLearnItem() != nullptr);
        QQuickItem* learn = openLearnItem();
        QTest::mouseClick(w, Qt::LeftButton, {}, learn->mapToScene(QPointF(learn->width() / 2, learn->height() / 2)).toPoint());
        QTRY_COMPARE(status->property("learningMixerKnob").toInt(), 1);
        auto* banner = w->findChild<QQuickItem*>(u"knobLearnBanner"_s);
        QVERIFY(banner != nullptr);
        QTRY_VERIFY(banner->isVisible());
        shoot(u"knob-learn"_s);
        QTest::keyClick(w, Qt::Key_Escape);
        QTRY_COMPARE(status->property("learningMixerKnob").toInt(), -1);
        QVERIFY(!banner->isVisible());

        // A knob inside the plugin: the strip's menu starts learning on the
        // Instrument tab, and the banner says to move the knob in the plugin.
        QVERIFY(QMetaObject::invokeMethod(&doc, "editChannel", Q_ARG(int, 0), Q_ARG(QString, u"plugin-learn"_s)));
        QTRY_VERIFY(status->property("learningMapping").toBool());
        QTRY_VERIFY(banner->isVisible());
        auto* text = w->findChild<QQuickItem*>(u"knobLearnText"_s);
        QVERIFY(text != nullptr);
        QVERIFY2(text->property("text").toString().contains(u"in the plugin"_s), qPrintable(text->property("text").toString()));
        QCOMPARE(w->findChild<QObject*>(u"mainTabs"_s)->property("currentIndex").toInt(), 1); // the Instrument tab
        QTest::keyClick(w, Qt::Key_Escape);
        QTRY_VERIFY(!status->property("learningMapping").toBool());
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

    // The README's pictures (docs/images), with GIGCHAIN_README_SHOTS set to
    // the folder to write them to: a small made-up setlist on the demo
    // engine, in Edit, Perform (playing), the fader's MIDI Learn menu,
    // Practice and a warm-up run played note by note (its score). Skipped
    // otherwise.
    // The demo video. With GIGCHAIN_DEMO_VIDEO set to the video's folder
    // (subs.json: each scene's voice length and its timed lines, made from
    // the voice clips; Director.qml: the pointer, spotlight and subtitles
    // drawn over the app; splash.png), the app is played through scene by
    // scene, each recorded to scene_NN.mp4 (ffmpeg on the PATH).
    // GIGCHAIN_DEMO_SCENES=03,05 records only those. Off otherwise.
    void demoVideo()
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_DEMO_VIDEO");
        if (folder.isEmpty()) QSKIP("Set GIGCHAIN_DEMO_VIDEO to the video's folder to record the demo video");
        const QStringList only = qEnvironmentVariable("GIGCHAIN_DEMO_SCENES").split(u',', Qt::SkipEmptyParts);
        QFile subsFile(folder + u"/subs.json"_s);
        QVERIFY2(subsFile.open(QIODevice::ReadOnly), qPrintable(subsFile.errorString()));
        const QJsonObject scenes = QJsonDocument::fromJson(subsFile.readAll()).object();
        QVERIFY(!scenes.isEmpty());

        QObject* root = m_qml->rootObjects().value(0);
        QQuickWindow* w = window();
        w->resize(SceneRecorder::kWindowWidth, SceneRecorder::kWindowHeight);
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));
        QTest::qWait(300);
        qInfo() << "window" << w->size() << "frames" << w->grabWindow().size();
        ui::DocumentController& doc = m_session->document();
        auto* loops = root->property("loops").value<QObject*>();
        QVERIFY(loops != nullptr && loops->setProperty("stripVisible", false));

        // The overlay, over everything (popups too): in the window's popup layer.
        QQuickItem* layer = nullptr;
        for (QQuickItem* level = w->contentItem(); level != nullptr && layer == nullptr; level = level->parentItem()) {
            for (QQuickItem* child : level->childItems()) {
                if (child->inherits("QQuickOverlay")) layer = child;
            }
        }
        QQmlComponent component(m_qml.get(), QUrl::fromLocalFile(folder + u"/Director.qml"_s));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> overlayObject(component.beginCreate(m_qml->rootContext()));
        auto* overlay = qobject_cast<QQuickItem*>(overlayObject.get());
        QVERIFY(overlay != nullptr);
        component.setInitialProperties(overlay, {{u"logoSource"_s, QUrl::fromLocalFile(folder + u"/splash.png"_s)}});
        overlay->setParentItem(layer != nullptr ? layer : w->contentItem());
        component.completeCreate();
        m_overlay = overlay;
        SceneRecorder recorder(w, overlay, root->findChild<QQuickWindow*>(u"notificationWindow"_s));
        m_recorder = &recorder;
        const auto cleanUp = qScopeGuard([this] {
            m_recorder = nullptr;
            m_overlay = nullptr;
        });

        // ---- The setlist ---------------------------------------------------
        const auto part = [](const QString& name) { return QVariantMap{{u"name"_s, name}, {u"occurrence"_s, 1}}; };
        QVERIFY(doc.renameSong(0, u"Morning Light"_s));
        QVERIFY(doc.setSongTempo(0, 96));
        QVERIFY(doc.setSongChart(0, u"{comment: Verse 1}\n"
                                  "[C]Down by the [G]water we [Am]wait for the [F]morning\n"
                                  "[C]Every small [G]light on the [F]harbour wall\n"
                                  "{comment: Chorus}\n"
                                  "[F]Hold on, [G]hold on, the [Am]night is nearly [F]over\n"
                                  "[C]We are [G]almost [F]home\n"
                                  "{comment: Verse 2}\n"
                                  "[C]Out on the [G]road where the [Am]rain keeps on [F]falling\n"
                                  "[C]Every old [G]song brings me [F]back again\n"
                                  "{comment: Bridge}\n"
                                  "[Am]And if the [G]light should [F]fade\n"
                                  "[Am]I will [G]find my [F]way [G]home\n"_s));
        QVERIFY(doc.setSongFlow({part(u"Verse 1"_s), part(u"Chorus"_s), part(u"Verse 2"_s), part(u"Chorus"_s), part(u"Bridge"_s),
                                 part(u"Chorus"_s)}));
        for (int section = 0; section < 4; ++section) QVERIFY(doc.setSectionBars(section, 2));
        QVERIFY(doc.addChannel(u"fake.grand-piano"_s, u"Grand Piano"_s));
        QVERIFY(doc.setChannelName(0, u"Warm Piano"_s));
        QVERIFY(doc.addEffect(0, u"fake.reverb"_s, u"Reverb"_s));
        QVERIFY(doc.addChannel(u"fake.analog-pad"_s, u"Analog Pad"_s));
        QVERIFY(doc.setChannelName(1, u"Soft Pad"_s));
        QVERIFY(doc.setChannelVolume(1, -8.0));
        QVERIFY(doc.addEffect(1, u"fake.chorus"_s, u"Chorus"_s));
        QVERIFY(doc.addChannel(u"fake.string-ensemble"_s, u"String Ensemble"_s));
        QVERIFY(doc.setChannelName(2, u"Strings"_s));
        QVERIFY(doc.setChannelVolume(2, -12.0));
        QVERIFY(doc.addChannel(u"fake.synth-lead"_s, u"Synth Lead"_s));
        QVERIFY(doc.setChannelName(3, u"Synth Bass"_s));
        QVERIFY(doc.setChannelVolume(3, -6.0));
        QVERIFY(doc.removeSectionChannel(0, 2)); // the strings wait for the chorus
        QVERIFY(doc.removeSectionChannel(2, 2));
        const QList<std::pair<QString, QList<std::pair<QString, QString>>>> others = {
            {u"Blue Harbour"_s, {{u"fake.electric-piano"_s, u"Electric Piano"_s}, {u"fake.tonewheel-organ"_s, u"Tonewheel Organ"_s}}},
            {u"Silver Line"_s, {{u"fake.grand-piano"_s, u"Grand Piano"_s}, {u"fake.synth-lead"_s, u"Synth Lead"_s}}},
            {u"Golden Hour"_s, {{u"fake.electric-piano"_s, u"Electric Piano"_s}, {u"fake.analog-pad"_s, u"Analog Pad"_s}}},
            {u"City Rain"_s, {{u"fake.tonewheel-organ"_s, u"Tonewheel Organ"_s}}},
            {u"Last Train Home"_s, {{u"fake.grand-piano"_s, u"Grand Piano"_s}, {u"fake.string-ensemble"_s, u"String Ensemble"_s}}},
        };
        for (const auto& [name, instruments] : others) {
            QVERIFY(doc.addSong());
            const int song = static_cast<int>(doc.setlist().songs.size()) - 1;
            QVERIFY(doc.renameSong(song, name));
            QVERIFY(doc.selectPatch(song, 0));
            for (const auto& [id, instrument] : instruments) QVERIFY(doc.addChannel(id, instrument));
            if (song == 2) QVERIFY(doc.addEffect(0, u"fake.delay"_s, u"Delay"_s));
        }
        QVERIFY(doc.saveAs(m_dir->filePath(u"Saturday Gig.gigchain.json"_s))); // (the title bar names it)
        // The song pasted in scene 7, as a chord website shows it.
        const QString pasted = u"Verse\n"
                                "C              G\n"
                                "Under the streetlights the city is sleeping\n"
                                "Am               F\n"
                                "I hear your voice in the hum of the rain\n"
                                "Chorus\n"
                                "F          G          C\n"
                                "Carry me home, carry me home\n"
                                "Am         F           G\n"
                                "Back to the place where the river runs slow\n"_s;

        auto* tabs = w->findChild<QObject*>(u"mainTabs"_s);
        auto* sideTabs = findItem(w->contentItem(), u"sidePanelTabs"_s);
        QVERIFY(tabs != nullptr && sideTabs != nullptr);
        auto* warmup = root->property("warmup").value<ui::WarmupController*>();
        QVERIFY(warmup != nullptr);
        ui::PracticeController& practice = m_session->practice();
        // Each scene starts from Edit, the song stopped, the setlist showing.
        const auto toEdit = [&](int song) {
            // Whatever menu or box the last scene left open, closed.
            for (QObject* popup : root->findChildren<QObject*>()) {
                if (popup->inherits("QQuickPopup") && popup->property("visible").toBool()) QMetaObject::invokeMethod(popup, "close");
            }
            doc.stopSong();
            if (practice.playing()) QVERIFY(QMetaObject::invokeMethod(&practice, "stop"));
            warmup->setActive(false);
            practice.setMode(1);
            practice.setSpeed(1.0);
            // Every key let go, and the last scene's key presses forgotten (a warm-up would count them).
            for (int note = 0; note < 128; ++note) m_engine->injectNote(1, note, 0);
            (void)m_engine->takeKeyPresses();
            QVERIFY(root->setProperty("performMode", false));
            QVERIFY(root->setProperty("practiceMode", false));
            QVERIFY(doc.selectPatch(song, 0));
            sideTabs->setProperty("currentIndex", 0);
            tabs->setProperty("currentIndex", 0);
            doc.setSelectedChannel(0);
            m_overlay->setProperty("pointer", QPointF(-60, -60));
            m_overlay->setProperty("caption", QString());
            m_overlay->setProperty("logo", 0.0);
            unspot();
            w->setVisibility(QWindow::Windowed);
            w->setGeometry(0, 0, SceneRecorder::kWindowWidth, SceneRecorder::kWindowHeight);
            QTest::qWait(600);
        };
        // Perform: full screen in the app; here the window keeps the video's size (the hidden screen is smaller).
        const auto perform = [&] {
            QVERIFY(root->setProperty("performMode", true));
            w->setVisibility(QWindow::Windowed);
            w->setGeometry(0, 0, SceneRecorder::kWindowWidth, SceneRecorder::kWindowHeight);
        };
        // (The status line's engine name is the demo's, not the app's: hidden.)
        auto* engineName = w->findChild<QQuickItem*>(u"statusAudio"_s);
        QVERIFY(engineName != nullptr);
        engineName->setVisible(false);
        QJsonObject scene;
        const auto begin = [&](const QString& key) {
            if (!only.isEmpty() && !only.contains(key)) return false;
            scene = scenes.value(key).toObject();
            if (scene.isEmpty()) return false;
            if (!recorder.start(folder + u"/scene_"_s + key + u".mp4"_s, scene.value(u"lines"_s).toArray())) {
                qWarning().noquote() << recorder.problem();
                return false;
            }
            return true;
        };
        const auto end = [&] {
            at(scene.value(u"duration"_s).toDouble() + 0.45);
            QVERIFY2(recorder.stop(), qPrintable(recorder.problem()));
        };
        const auto rowOf = [&](int song) { return shown(u"setlistRow"_s).value(song); };
        const auto spotMixer = [&] {
            QTest::qWait(60); // (the strips of a song just chosen, laid out)
            spot(stripsArea(static_cast<int>(doc.currentPatch()->channels.size())));
        };

        // ---- 01: the hook. The song playing on stage. ------------------------
        toEdit(0);
        if (only.isEmpty() || only.contains(u"01"_s)) {
            perform();
            QTest::qWait(500);
            doc.playSongFromTop();
            QTest::qWait(1500);
            if (begin(u"01"_s)) {
                at(8.9);
                press(shownOne(u"performNextPart"_s));
                at(10.6);
                m_overlay->setProperty("caption", u"Free · Open source"_s);
                end();
            }
        }

        // ---- 02: meet it ------------------------------------------------------
        toEdit(0);
        m_overlay->setProperty("logo", 1.0);
        QTest::qWait(600);
        if (begin(u"02"_s)) {
            at(1.8);
            m_overlay->setProperty("logoLine2", u"Windows"_s);
            at(3.1);
            m_overlay->setProperty("logoLine2", u"Windows  ·  Mac"_s);
            at(3.7);
            m_overlay->setProperty("logoLine2", u"Windows  ·  Mac  ·  Linux"_s);
            at(4.6);
            m_overlay->setProperty("logo", 0.0);
            end();
        }
        m_overlay->setProperty("logoLine2", QString());

        // ---- 03: your sounds ---------------------------------------------------
        toEdit(0);
        if (begin(u"03"_s)) {
            // The side panel's Instruments tab (its right half).
            const QRectF tabsArea = rectOf(sideTabs);
            glide(QPointF(tabsArea.x() + (tabsArea.width() * 0.75), tabsArea.center().y()));
            QVERIFY(QMetaObject::invokeMethod(m_overlay, "click"));
            QTest::mouseClick(w, Qt::LeftButton, {}, QPoint(static_cast<int>(tabsArea.x() + (tabsArea.width() * 0.75)), static_cast<int>(tabsArea.center().y())));
            QTest::qWait(300);
            const QQuickItem* list = shownOne(u"pluginList"_s);
            QVERIFY(list != nullptr);
            spot(list);
            at(2.5);
            const QRectF listArea = rectOf(list);
            for (int i = 0; i < 4; ++i) glide(QPointF(listArea.x() + 120, listArea.y() + 40 + (i * 46)), 520);
            at(5.6);
            unspot();
            const QRectF back = rectOf(sideTabs);
            glide(QPointF(back.x() + (back.width() * 0.25), back.center().y()));
            QVERIFY(QMetaObject::invokeMethod(m_overlay, "click"));
            QTest::mouseClick(w, Qt::LeftButton, {}, QPoint(static_cast<int>(back.x() + (back.width() * 0.25)), static_cast<int>(back.center().y())));
            at(7.3);
            press(rowOf(1), Qt::LeftButton, 450);
            spotMixer();
            at(9.3);
            press(rowOf(2), Qt::LeftButton, 400);
            at(10.4);
            press(rowOf(0), Qt::LeftButton, 400);
            at(11.8);
            unspot();
            spot(shownOne(u"keyboardView"_s));
            for (const int note : {48, 60, 64, 67}) m_engine->injectNote(1, note, 100); // a chord, held
            at(13.2);
            press(rowOf(3), Qt::LeftButton, 450);
            at(15.0);
            spotMixer();
            at(18.6);
            for (const int note : {48, 60, 64, 67}) m_engine->injectNote(1, note, 0);
            end();
        }

        // ---- 04: mixer and splits ---------------------------------------------
        toEdit(0);
        QVERIFY(doc.setChannelKeyRange(3, 0, 127));
        if (begin(u"04"_s)) {
            spot(stripsArea(1));
            at(3.0);
            spot(strip(0)->findChild<QQuickItem*>(u"effectList"_s));
            at(4.2);
            spot(strip(0)->findChild<QQuickItem*>(u"panKnob"_s));
            at(5.2);
            spot(strip(0)->findChild<QQuickItem*>(u"faderKnobArea"_s));
            at(6.2);
            spotMixer();
            at(7.6);
            unspot();
            QVERIFY(QMetaObject::invokeMethod(&doc, "editChannel", Q_ARG(int, 3), Q_ARG(QString, u"zone"_s)));
            at(8.6);
            QVERIFY(doc.setChannelKeyRange(3, 24, 47)); // the bass: the left hand
            at(10.2);
            QVERIFY(QMetaObject::invokeMethod(w->findChild<QObject*>(u"zoneDialog"_s), "close"));
            QVERIFY(doc.setChannelKeyRange(0, 48, 108)); // piano and strings: the right hand
            QVERIFY(doc.setChannelKeyRange(2, 48, 108));
            QTest::qWait(250);
            {
                QRectF zones;
                for (const QQuickItem* zone : shown(u"zoneText"_s)) zones = zones.united(rectOf(zone));
                spot(zones);
            }
            at(12.4);
            spotMixer();
            end();
        }

        // ---- 05: MIDI Learn -----------------------------------------------------
        toEdit(0);
        if (begin(u"05"_s)) {
            const auto* fader = strip(0)->findChild<QQuickItem*>(u"faderKnobArea"_s);
            QVERIFY(fader != nullptr);
            at(0.4);
            glide(centre(fader), 900);
            at(1.9);
            spot(fader);
            at(4.2);
            unspot();
            press(fader, Qt::RightButton, 300);
            QTRY_VERIFY(!shown(u"learnKnob"_s).isEmpty());
            at(5.6);
            press(shownOne(u"learnKnob"_s), Qt::LeftButton, 400);
            at(7.4);
            // A knob on the keyboard turned: learnt, then it moves the fader.
            m_engine->injectController(1, 21, 96);
            QTest::qWait(120);
            for (int step = 0; step <= 60; ++step) {
                const double phase = step / 60.0;
                m_engine->injectController(1, 21, static_cast<int>(96 - (50 * std::sin(phase * std::numbers::pi))));
                QTest::qWait(55);
            }
            spot(fader);
            at(11.8);
            unspot();
            press(strip(0)->findChild<QQuickItem*>(u"panKnob"_s), Qt::RightButton, 400);
            at(13.2);
            QTest::keyClick(w, Qt::Key_Escape);
            press(strip(0)->findChild<QQuickItem*>(u"instrumentSlot"_s), Qt::RightButton, 400);
            end();
            QTest::keyClick(w, Qt::Key_Escape);
        }

        // ---- 06: the keyboard's buttons -----------------------------------------
        toEdit(0);
        perform();
        QTest::qWait(500);
        if (begin(u"06"_s)) {
            spot(shownOne(u"performTransport"_s));
            at(0.6);
            QVERIFY(QMetaObject::invokeMethod(m_overlay, "pressKey", Q_ARG(QVariant, u"▶  Play"_s)));
            doc.playSongFromTop();
            at(4.0);
            QVERIFY(QMetaObject::invokeMethod(m_overlay, "pressKey", Q_ARG(QVariant, u"▶▶  Next"_s)));
            doc.nextPart();
            at(5.3);
            QVERIFY(QMetaObject::invokeMethod(m_overlay, "pressKey", Q_ARG(QVariant, u"■  Stop"_s)));
            doc.stopSong();
            end();
        }

        // ---- 07: chord charts ----------------------------------------------------
        toEdit(1);
        if (begin(u"07"_s)) {
            QGuiApplication::clipboard()->setText(pasted);
            const QQuickItem* paste = shownOne(u"pasteChartButton"_s);
            QVERIFY(paste != nullptr);
            press(paste, Qt::LeftButton, 700);
            QTRY_VERIFY(!doc.setlist().songs.at(1).chart.isEmpty());
            at(1.5);
            m_overlay->setProperty("pointer", QPointF(-60, -60));
            perform(); // the chart as it is read on stage: each chord on its word
            QTest::qWait(300);
            spot(shownOne(u"performChart"_s));
            at(4.0);
            unspot();
            at(5.2);
            const QList<QQuickItem*> chords = tappableChords();
            QVERIFY(chords.size() > 2);
            press(chords.at(2), Qt::LeftButton, 400);
            at(6.6);
            for (QObject* diagram : w->findChildren<QObject*>(u"chordDiagram"_s)) {
                auto* content = diagram->property("contentItem").value<QQuickItem*>();
                if (diagram->property("visible").toBool() && content != nullptr && content->parentItem() != nullptr) spot(content->parentItem());
            }
            end();
            QTest::keyClick(w, Qt::Key_Escape);
        }

        // ---- 08: on stage -------------------------------------------------------
        toEdit(0);
        if (begin(u"08"_s)) {
            spot(shownOne(u"flowBar"_s));
            const QList<QQuickItem*> flow = shown(u"flowPart"_s);
            at(0.8);
            for (int i = 0; i < std::min<qsizetype>(5, flow.size()); ++i) {
                if (i == 1) at(1.6);
                if (i == 2) at(2.5);
                if (i == 4) at(3.2);
                glide(centre(flow.at(i)), 380);
            }
            at(4.4);
            unspot();
            press(shownOne(u"songPlayButton"_s), Qt::LeftButton, 500);
            at(7.2);
            spotMixer();
            at(11.5);
            unspot();
            m_overlay->setProperty("pointer", QPointF(-60, -60));
            perform();
            at(14.5);
            spot(shownOne(u"performTransport"_s));
            at(15.9);
            press(shownOne(u"performNextPart"_s), Qt::LeftButton, 450);
            end();
        }

        // ---- 09: practice -------------------------------------------------------
        toEdit(0);
        QVERIFY(QMetaObject::invokeMethod(w->findChild<QObject*>(u"practiceButton"_s), "clicked"));
        QTest::qWait(500);
        practice.setMode(1);
        practice.setSpeed(1.0);
        if (begin(u"09"_s)) {
            QList<QVariantMap> toPlay;
            for (const QVariant& n : practice.notes()) toPlay << n.toMap();
            QList<std::pair<int, double>> held;
            at(1.5);
            press(shownOne(u"practicePlay"_s), Qt::LeftButton, 500);
            playAlong(toPlay, held, 7.5);
            spot(shownOne(u"practiceSpeed"_s));
            m_overlay->setProperty("pointer", centre(shownOne(u"practiceSpeed"_s)));
            practice.setSpeed(0.6);
            playAlong(toPlay, held, 9.0);
            spot(shownOne(u"practiceMode2"_s));
            press(shownOne(u"practiceMode2"_s), Qt::LeftButton, 350);
            playAlong(toPlay, held, scene.value(u"duration"_s).toDouble() + 0.4);
            for (const auto& [pitch, until] : held) m_engine->injectNote(1, pitch, 0);
            end();
        }

        // ---- 10: warm-ups -------------------------------------------------------
        toEdit(0);
        if (only.isEmpty() || only.contains(u"10"_s)) {
            QVERIFY(QMetaObject::invokeMethod(w->findChild<QObject*>(u"practiceButton"_s), "clicked"));
            QTest::qWait(400);
            warmup->setActive(true);
            QTest::qWait(300);
            warmup->startExercise(1, static_cast<int>(core::WarmupHands::Both));
            QList<QVariantMap> toPlay;
            for (const QVariant& n : practice.notes()) toPlay << n.toMap();
            QList<std::pair<int, double>> held;
            // Played along until the run's last few seconds: the scene ends on its score.
            const auto secondsLeft = [&practice] { return (practice.length() - practice.position()) * 60.0 / practice.tempo(); };
            // (ffmpeg started first: starting it while playing would make the notes late.)
            QVERIFY2(recorder.prepare(folder + u"/scene_10.mp4"_s), qPrintable(recorder.problem()));
            playAlong(toPlay, held, [&] { return secondsLeft() <= 4.6; });
            if (begin(u"10"_s)) {
                playAlong(toPlay, held, 2.2); // (still playing: waiting here would miss the notes)
                m_overlay->setProperty("caption", u"Beginner  ·  Intermediate  ·  Pro"_s);
                playAlong(toPlay, held, [&] { return !practice.playing(); });
                m_overlay->setProperty("caption", QString());
                QTRY_VERIFY(!shown(u"warmupStars"_s).isEmpty());
                at(5.0);
                QRectF score;
                for (const QString& name : {u"warmupStars"_s, u"warmupNotesScore"_s, u"warmupTip"_s, u"warmupNext"_s}) {
                    if (const QQuickItem* piece = shownOne(name)) score = score.united(rectOf(piece));
                }
                spot(score);
                end();
            }
        }

        // ---- 11: the close ------------------------------------------------------
        toEdit(0);
        m_overlay->setProperty("logo", 1.0);
        m_overlay->setProperty("logoLine", u"Free  ·  Open source"_s);
        m_overlay->setProperty("logoLine2", u"github.com/xDarkzx/GigChain-Keys"_s);
        QTest::qWait(600);
        if (begin(u"11"_s)) end();
    }

    void readmeScreenshots()
    {
        const QString folder = qEnvironmentVariable("GIGCHAIN_README_SHOTS");
        if (folder.isEmpty()) QSKIP("Set GIGCHAIN_README_SHOTS to a folder to take the README's pictures");
        const auto snap = [&](const QString& name) {
            QTest::qWait(400);
            QVERIFY(window()->grabWindow().save(folder + u'/' + name + u".png"_s));
        };
        QObject* root = m_qml->rootObjects().value(0);
        QQuickWindow* w = window();
        w->resize(1600, 1000);
        w->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(w));
        ui::DocumentController& doc = m_session->document();
        // (The loop station's row out of the way: the chart has the room.)
        auto* loops = root->property("loops").value<QObject*>();
        QVERIFY(loops != nullptr && loops->setProperty("stripVisible", false));

        // The setlist: three songs, the first with sections, a flow, three instruments named for their sound.
        QVERIFY(doc.renameSong(0, u"Morning Light"_s));
        QVERIFY(doc.addSong());
        QVERIFY(doc.renameSong(1, u"Blue Harbour"_s));
        QVERIFY(doc.addSong());
        QVERIFY(doc.renameSong(2, u"Silver Line"_s));
        QVERIFY(doc.selectPatch(0, 0));
        QVERIFY(doc.setSongTempo(0, 96));
        QVERIFY(doc.setSongChart(0, u"{comment: Verse 1}\n"
                                  "[C]Down by the [G]water we [Am]wait for the [F]morning\n"
                                  "[C]Every small [G]light on the [F]harbour wall\n"
                                  "{comment: Chorus}\n"
                                  "[F]Hold on, [G]hold on, the [Am]night is nearly [F]over\n"
                                  "[C]We are [G]almost [F]home\n"
                                  "{comment: Verse 2}\n"
                                  "[C]Out on the [G]road where the [Am]rain keeps on [F]falling\n"
                                  "[C]Every old [G]song brings me [F]back again\n"_s));
        const auto part = [](const QString& name) { return QVariantMap{{u"name"_s, name}, {u"occurrence"_s, 1}}; };
        QVERIFY(doc.setSongFlow({part(u"Verse 1"_s), part(u"Chorus"_s), part(u"Verse 2"_s), part(u"Chorus"_s)}));
        QVERIFY(doc.addChannel(u"fake.grand-piano"_s, u"Grand Piano"_s));
        QVERIFY(doc.setChannelName(0, u"Warm Piano"_s));
        QVERIFY(doc.addEffect(0, u"fake.reverb"_s, u"Reverb"_s));
        QVERIFY(doc.addChannel(u"fake.analog-pad"_s, u"Analog Pad"_s));
        QVERIFY(doc.setChannelName(1, u"Soft Pad"_s));
        QVERIFY(doc.setChannelVolume(1, -8.0));
        QVERIFY(doc.addChannel(u"fake.string-ensemble"_s, u"String Ensemble"_s));
        QVERIFY(doc.setChannelName(2, u"Strings"_s));
        QVERIFY(doc.setChannelVolume(2, -12.0));
        QVERIFY(doc.removeSectionChannel(0, 2)); // the strings wait for the chorus
        auto* tabs = w->findChild<QObject*>(u"mainTabs"_s);
        QVERIFY(tabs != nullptr && tabs->setProperty("currentIndex", 0));
        doc.setSelectedChannel(0);
        settle();
        snap(u"edit"_s);

        // MIDI Learn from a fader's right-click.
        QQuickItem* fader = stripChild(u"faderKnobArea"_s);
        QVERIFY(fader != nullptr);
        QTest::mouseClick(w, Qt::RightButton, {}, fader->mapToScene(QPointF(fader->width() / 2, fader->height() / 3)).toPoint());
        snap(u"midi-learn"_s);
        QTest::keyClick(w, Qt::Key_Escape);
        settle();

        // Perform, the song playing.
        QVERIFY(root->setProperty("performMode", true));
        settle();
        auto* play = findItem(w->contentItem(), u"performPlay"_s);
        QVERIFY(play != nullptr);
        QVERIFY(QMetaObject::invokeMethod(play, "clicked"));
        QTest::qWait(2500);
        snap(u"perform"_s);
        QVERIFY(QMetaObject::invokeMethod(play, "clicked"));
        QVERIFY(root->setProperty("performMode", false));
        settle();

        // Practice: the song's chords falling.
        auto* practiceButton = w->findChild<QObject*>(u"practiceButton"_s);
        QVERIFY(QMetaObject::invokeMethod(practiceButton, "clicked"));
        settle();
        QVERIFY(QMetaObject::invokeMethod(findItem(w->contentItem(), u"practiceMode1"_s), "clicked"));
        QVERIFY(QMetaObject::invokeMethod(findItem(w->contentItem(), u"practicePlay"_s), "clicked"));
        QTest::qWait(3000);
        snap(u"practice"_s);
        QVERIFY(QMetaObject::invokeMethod(findItem(w->contentItem(), u"practicePlay"_s), "clicked"));

        // The warm-up: five fingers from C with both hands, played on time
        // (each note sent as it lands), then its score.
        auto* warmup = root->property("warmup").value<ui::WarmupController*>();
        QVERIFY(warmup != nullptr);
        warmup->setActive(true);
        warmup->startExercise(1, static_cast<int>(core::WarmupHands::Both));
        const ui::PracticeController& practice = m_session->practice();
        QList<QVariantMap> toPlay;
        for (const QVariant& n : practice.notes()) toPlay << n.toMap();
        QList<std::pair<int, double>> held; // (pitch, the beat it lets go at)
        bool shotPlaying = false;
        for (int i = 0; i < 4000 && practice.playing(); ++i) {
            QTest::qWait(4);
            for (auto it = held.begin(); it != held.end();) {
                if (practice.position() < it->second) {
                    ++it;
                    continue;
                }
                m_engine->injectNote(1, it->first, 0);
                it = held.erase(it);
            }
            for (auto it = toPlay.begin(); it != toPlay.end();) {
                const double start = it->value(u"start"_s).toDouble();
                if (practice.position() + 0.01 < start) {
                    ++it;
                    continue;
                }
                m_engine->injectNote(1, it->value(u"pitch"_s).toInt(), 96);
                held << std::pair{it->value(u"pitch"_s).toInt(), start + (it->value(u"length"_s).toDouble() * 0.9)};
                it = toPlay.erase(it);
            }
            if (!shotPlaying && practice.position() > 6.2) {
                shotPlaying = true;
                QVERIFY(w->grabWindow().save(folder + u"/warmup.png"_s));
            }
        }
        QTRY_VERIFY(findItem(w->contentItem(), u"warmupStars"_s)->isVisible());
        snap(u"warmup-score"_s);
    }

    // The Practice tab's warm-up: the level's exercises and Start; Start
    // runs the first exercise, right hand, its notes falling with their
    // fingers; back to Song, the song's notes return.
    void theWarmupStartsFromThePracticeTab()
    {
        window()->resize(1500, 900);
        QQuickItem* scene = window()->contentItem();
        auto* practiceButton = window()->findChild<QObject*>(u"practiceButton"_s);
        QVERIFY(practiceButton != nullptr);
        QVERIFY(QMetaObject::invokeMethod(practiceButton, "clicked"));
        settle();
        auto* warmupMode = findItem(scene, u"practiceWarmupMode"_s);
        QVERIFY(warmupMode != nullptr);
        QVERIFY(QMetaObject::invokeMethod(warmupMode, "clicked"));
        settle();
        auto* panel = findItem(scene, u"warmupPanel"_s);
        QVERIFY(panel != nullptr);
        QTRY_VERIFY(panel->isVisible()); // the level's exercises, and Start
        QVERIFY(findItem(scene, u"warmupBar"_s)->isVisible());
        shoot(u"warmup-start"_s);

        auto* begin = findItem(scene, u"warmupBegin"_s);
        QVERIFY(begin != nullptr && begin->isVisible());
        QVERIFY(QMetaObject::invokeMethod(begin, "clicked"));
        settle();
        QVERIFY(m_session->practice().exercise());
        QVERIFY(m_session->practice().playing());
        QTRY_VERIFY(findItem(scene, u"warmupPlaying"_s)->isVisible()); // what to play, and how
        QVERIFY(!panel->isVisible());
        const QList<QQuickItem*> fingers = findAll(scene, u"practiceFinger"_s);
        QCOMPARE(fingers.size(), 33); // the warm-up run, right hand: C to G and back, four times, and the C
        QTest::qWait(1200);           // (the count-in: the notes come into view)
        shoot(u"warmup-playing"_s);
        // To the end with nothing played (the demo has no keyboard): scored,
        // no stars, and the tip says what to check.
        m_session->practice().advance(60000.0);
        QTRY_VERIFY(panel->isVisible());
        auto* stars = findItem(scene, u"warmupStars"_s);
        QVERIFY(stars != nullptr && stars->isVisible());
        QCOMPARE(stars->property("text").toString(), u"☆☆☆"_s);
        QVERIFY(findItem(scene, u"warmupTip"_s)->property("text").toString().contains(u"Nothing was heard"_s));
        QVERIFY(findItem(scene, u"warmupNext"_s)->isVisible());
        shoot(u"warmup-result"_s);

        auto* songMode = findItem(scene, u"practiceSongMode"_s);
        QVERIFY(QMetaObject::invokeMethod(songMode, "clicked"));
        settle();
        QVERIFY(!m_session->practice().exercise());
        QVERIFY(!m_session->practice().playing());
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
