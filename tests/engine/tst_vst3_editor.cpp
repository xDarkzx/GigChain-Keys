// Opens a real plugin's editor inside a native window: a hidden Windows
// window with Piano V2, an X11 window with Surge XT on Linux (through WSLg's
// X11 layer in WSL), or a Cocoa window's NSView with Surge XT on the Mac.
// Skips when the plugin is not installed, or where there is no window system
// (Linux without a display).
#include "PluginNode.h"
#include "TestPlugins.h"
#include "Vst3Node.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QWindow>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#include <ole2.h>
#include "WindowCapture.h" // (after Qt, which defines Q_OS_WIN)
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

#ifdef Q_OS_WIN
const QString kInstrument = u"C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"_s;
const QString kTitle = u"Piano V2"_s;

// A hidden top-level window for the plugin to attach its view to.
class HiddenParent
{
public:
    HiddenParent()
        : m_hwnd(CreateWindowExW(0, L"STATIC", L"Test parent window", WS_POPUP, 0, 0, 800, 600, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr))
    {
    }
    ~HiddenParent() { DestroyWindow(m_hwnd); }
    HiddenParent(const HiddenParent&) = delete;
    HiddenParent& operator=(const HiddenParent&) = delete;
    HiddenParent(HiddenParent&&) = delete;
    HiddenParent& operator=(HiddenParent&&) = delete;

    [[nodiscard]] quintptr handle() const { return reinterpret_cast<quintptr>(m_hwnd); } // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): an HWND as a window id
    [[nodiscard]] HWND window() const { return m_hwnd; }

    // Shown, so the window system draws it and it can be captured (one off every
    // screen is never drawn), but unseen: all but fully see-through, behind every
    // window, clicks going through it, no taskbar button, never focused.
    void showUnseen(QSize size) const
    {
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE,
                          GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED
                              | WS_EX_TRANSPARENT);
        SetLayeredWindowAttributes(m_hwnd, 0, 1, LWA_ALPHA);
        SetWindowPos(m_hwnd, HWND_BOTTOM, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                     size.width(), size.height(), SWP_NOACTIVATE);
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }

private:
    HWND m_hwnd;
};
#else
const QString kInstrument = test::kInstrument.path;
const QString kTitle = test::kInstrument.name;

// A window of the system's own (Qt's xcb on Linux: an X11 window; cocoa on
// the Mac: an NSView) for the plugin to attach its view to.
class HiddenParent
{
public:
    HiddenParent()
    {
        m_window.resize(800, 600);
        m_window.create();
    }
    [[nodiscard]] quintptr handle() const { return static_cast<quintptr>(m_window.winId()); }

private:
    QWindow m_window;
};
#endif

} // namespace

class TestVst3Editor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        // Plugin editors expect OLE on the UI thread, as in any Windows GUI app.
        QVERIFY(SUCCEEDED(OleInitialize(nullptr)));
#else
        if (platform::nativeWindowKind() == platform::NativeWindowKind::None) {
            QSKIP("Needs the system's window system (X11 on Linux: DISPLAY)");
        }
#endif
    }
#ifdef Q_OS_WIN
    void cleanupTestCase() { OleUninitialize(); }
#endif

    void instrumentEditorAttachesAndDetaches()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("The test instrument is not installed");
        auto node = Vst3Node::load(kInstrument, 48000.0, 256);
        QVERIFY2(node.has_value(), node ? "" : qPrintable(node.error().message));

        auto editor = Vst3Node::createEditor(*node);
        QVERIFY2(editor.has_value(), editor ? "" : qPrintable(editor.error().message));
        QVERIFY(*editor != nullptr); // it has an editor
        QCOMPARE((*editor)->title(), kTitle);
        const QSize size = (*editor)->preferredSize();
        QVERIFY2(size.width() > 100 && size.height() > 100, qPrintable(u"%1x%2"_s.arg(size.width()).arg(size.height())));

        HiddenParent parent;
        const auto attached = (*editor)->attach({.handle = parent.handle(), .kind = platform::nativeWindowKind()});
        QVERIFY2(attached.has_value(), attached ? "" : qPrintable(attached.error().message));
        QVERIFY((*editor)->isAttached());
        QTest::qWait(300); // its own timers and events run meanwhile (Linux: the host's run loop)

        (*editor)->detach();
        QVERIFY(!(*editor)->isAttached());
    }

    // A VST2 plugin's own window opens the same way (its own size, its idle
    // timer running while open) and closes cleanly.
    void aVst2EditorAttachesAndDetaches()
    {
#ifndef Q_OS_WIN
        QSKIP("The test VST2 effect is a Windows one");
#else
        const QString vst2 = u"C:/Program Files/Steinberg/VSTPlugins/TDR Kotelnikov.dll"_s;
        if (!QFileInfo::exists(vst2)) QSKIP("The test VST2 effect is not installed");
        auto node = PluginNode::load(vst2, 48000.0, 256);
        QVERIFY2(node.has_value(), node ? "" : qPrintable(node.error().message));
        auto editor = PluginNode::createEditor(*node);
        QVERIFY2(editor.has_value(), editor ? "" : qPrintable(editor.error().message));
        QVERIFY(*editor != nullptr);
        QCOMPARE((*editor)->title(), u"TDR Kotelnikov"_s);

        HiddenParent parent;
        const auto attached = (*editor)->attach({.handle = parent.handle(), .kind = platform::nativeWindowKind()});
        QVERIFY2(attached.has_value(), attached ? "" : qPrintable(attached.error().message));
        const QSize size = (*editor)->preferredSize();
        QVERIFY2(size.width() > 100 && size.height() > 100, qPrintable(u"%1x%2"_s.arg(size.width()).arg(size.height())));
        QTest::qWait(300); // idled meanwhile
        (*editor)->detach();
        QVERIFY(!(*editor)->isAttached());
#endif
    }

    // An analyser shows the sound going through it (#61): FabFilter Pro-Q 3's
    // spectrum, drawn while a tone plays and not while it is silent.
    void anAnalyserShowsTheSoundGoingThroughIt()
    {
#ifndef Q_OS_WIN
        QSKIP("The test analyser is FabFilter Pro-Q 3 (Windows)");
#else
        const QString proQ = u"C:/Program Files/Common Files/VST3/FabFilter/FabFilter Pro-Q 3.vst3"_s;
        if (!QFileInfo::exists(proQ)) QSKIP("FabFilter Pro-Q 3 is not installed");
        constexpr double kRate = 48000.0;
        constexpr int kBlock = 256;
        auto node = Vst3Node::load(proQ, kRate, kBlock);
        QVERIFY2(node.has_value(), node ? "" : qPrintable(node.error().message));
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY2(editor.has_value() && *editor, "no editor");
        HiddenParent parent;
        QVERIFY((*editor)->attach({.handle = parent.handle(), .kind = platform::nativeWindowKind()}).has_value());
        const QSize size = (*editor)->preferredSize();
        parent.showUnseen(size);
        (*editor)->updateGeometry();

        // The audio thread: silence, then a tone, as the engine plays them.
        std::atomic<bool> playing{true};
        std::atomic<bool> tone{false};
        std::thread audio([&node, &playing, &tone] {
            std::vector<float> left(kBlock);
            std::vector<float> right(kBlock);
            TimeInfo time{.tempo = 120.0, .sampleRate = kRate, .samplePosition = 0, .ppqPosition = 0.0,
                                .barStartPpq = 0.0, .timeSigNumerator = 4, .timeSigDenominator = 4};
            double phase = 0.0;
            while (playing.load()) {
                for (std::size_t i = 0; i < left.size(); ++i) {
                    left.at(i) = right.at(i) = tone.load() ? 0.5F * static_cast<float>(std::sin(phase)) : 0.0F;
                    phase += 2.0 * std::numbers::pi * 220.0 / kRate;
                }
                (*node)->process({}, AudioBlock{.left = left.data(), .right = right.data(), .frames = kBlock}, time);
                time.samplePosition += kBlock; // (playing on, as the engine does)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        QTest::qWait(1500);
        const QImage silent = windowcapture::captureWindow(parent.window());
        tone = true;
        QTest::qWait(2000);
        const QImage sounding = windowcapture::captureWindow(parent.window());
        playing = false;
        audio.join();
        (*editor)->detach();

        if (const QString folder = qEnvironmentVariable("GIGCHAIN_SCREENSHOTS"); !folder.isEmpty()) {
            silent.save(folder + u"/proq-silent.png"_s);
            sounding.save(folder + u"/proq-sounding.png"_s);
        }
        QVERIFY2(!silent.isNull() && !sounding.isNull(), "the window could not be captured");
        QCOMPARE(sounding.size(), silent.size());
        qsizetype changed = 0;
        for (int y = 0; y < silent.height(); ++y) {
            for (int x = 0; x < silent.width(); ++x) changed += silent.pixel(x, y) != sounding.pixel(x, y) ? 1 : 0;
        }
        qInfo() << "pixels the tone changed:" << changed << "of" << silent.width() * silent.height();
        QVERIFY2(changed > 1000, "the analyser drew nothing for the sound going through it");
#endif
    }

    void attachingToNothingIsAnError()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("The test instrument is not installed");
        auto node = Vst3Node::load(kInstrument, 48000.0, 256);
        QVERIFY(node.has_value());
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY(editor.has_value() && *editor != nullptr);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no window to attach"_s));
        const auto attached = (*editor)->attach({.handle = 0, .kind = platform::nativeWindowKind()});
        QVERIFY(!attached);
        QVERIFY(!(*editor)->isAttached());
    }

    // A plugin given a kind of window it does not support is refused,
    // saying which kind (another system's: an X11 window on the Mac, a macOS
    // view elsewhere).
    void anUnsupportedWindowKindIsRefused()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("The test instrument is not installed");
        auto node = Vst3Node::load(kInstrument, 48000.0, 256);
        QVERIFY(node.has_value());
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY(editor.has_value() && *editor != nullptr);
        HiddenParent parent;
#ifdef Q_OS_MACOS
        const auto foreign = platform::NativeWindowKind::X11;
        const QString foreignName = u"X11"_s;
#else
        const auto foreign = platform::NativeWindowKind::Cocoa;
        const QString foreignName = u"macOS"_s;
#endif
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"does not support "_s + foreignName + u" windows"_s));
        const auto refused = (*editor)->attach({.handle = parent.handle(), .kind = foreign});
        QVERIFY(!refused);
        QVERIFY2(refused.error().message.contains(foreignName), qPrintable(refused.error().message));
        QVERIFY(!(*editor)->isAttached());
    }

    // The app running where plugins have no window (Linux on Wayland without
    // X11): the editor is refused, saying how to get one, and the plugin is
    // never handed a window of the wrong kind (its toolkit would end the app).
    void noWindowForPluginsIsRefusedWithTheWayOut()
    {
        if (!QFileInfo::exists(kInstrument)) QSKIP("The test instrument is not installed");
        auto node = Vst3Node::load(kInstrument, 48000.0, 256);
        QVERIFY(node.has_value());
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY(editor.has_value() && *editor != nullptr);
        HiddenParent parent;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"QT_QPA_PLATFORM=xcb"_s));
        const auto refused = (*editor)->attach({.handle = parent.handle(), .kind = platform::NativeWindowKind::None});
        QVERIFY(!refused);
        QVERIFY2(refused.error().message.contains(u"X11"_s), qPrintable(refused.error().message));
        QVERIFY(!(*editor)->isAttached());
    }
};

int main(int argc, char** argv)
{
    // (The tests' default is off-screen; plugin windows here need the real
    // window system, where there is one.)
#if defined(Q_OS_MACOS)
    qputenv("QT_QPA_PLATFORM", "cocoa"); // real NSViews
#elif !defined(Q_OS_WIN)
    if (!qEnvironmentVariableIsEmpty("DISPLAY")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QGuiApplication app(argc, argv);
    TestVst3Editor test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_vst3_editor.moc"
