// Opens a real plugin's editor inside a hidden native window. Skips when the
// plugin is not installed. Nothing is shown on screen.
#include "Vst3Node.h"

#include <QFileInfo>
#include <QtTest>

#include <windows.h>
#include <ole2.h>

#include <cstdlib>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

const QString kPiano = u"C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"_s;

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

private:
    HWND m_hwnd;
};

} // namespace

class TestVst3Editor : public QObject
{
    Q_OBJECT

private slots:
    // Plugin editors expect OLE on the UI thread, as in any Windows GUI app
    // (Qt's GUI startup does this; this GUI-less test must do it itself).
    void initTestCase() { QVERIFY(SUCCEEDED(OleInitialize(nullptr))); }
    void cleanupTestCase() { OleUninitialize(); }

    void instrumentEditorAttachesAndDetaches()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto node = Vst3Node::load(kPiano, 48000.0, 256);
        QVERIFY2(node.has_value(), node ? "" : qPrintable(node.error().message));

        auto editor = Vst3Node::createEditor(*node);
        QVERIFY2(editor.has_value(), editor ? "" : qPrintable(editor.error().message));
        QVERIFY(*editor != nullptr); // Piano V2 has an editor
        QCOMPARE((*editor)->title(), u"Piano V2"_s);
        const QSize size = (*editor)->preferredSize();
        QVERIFY2(size.width() > 100 && size.height() > 100, qPrintable(u"%1x%2"_s.arg(size.width()).arg(size.height())));

        HiddenParent parent;
        const auto attached = (*editor)->attach({.handle = parent.handle(), .kind = platform::nativeWindowKind()});
        QVERIFY2(attached.has_value(), attached ? "" : qPrintable(attached.error().message));
        QVERIFY((*editor)->isAttached());

        (*editor)->detach();
        QVERIFY(!(*editor)->isAttached());
    }

    void attachingToNothingIsAnError()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto node = Vst3Node::load(kPiano, 48000.0, 256);
        QVERIFY(node.has_value());
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY(editor.has_value() && *editor != nullptr);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no window to attach"_s));
        const auto attached = (*editor)->attach({.handle = 0, .kind = platform::nativeWindowKind()});
        QVERIFY(!attached);
        QVERIFY(!(*editor)->isAttached());
    }

    // A plugin given a kind of window it does not support is refused,
    // saying which kind (a Windows plugin, a macOS view).
    void anUnsupportedWindowKindIsRefused()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto node = Vst3Node::load(kPiano, 48000.0, 256);
        QVERIFY(node.has_value());
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY(editor.has_value() && *editor != nullptr);
        HiddenParent parent;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"does not support macOS windows"_s));
        const auto refused = (*editor)->attach({.handle = parent.handle(), .kind = platform::NativeWindowKind::Cocoa});
        QVERIFY(!refused);
        QVERIFY2(refused.error().message.contains(u"macOS"_s), qPrintable(refused.error().message));
        QVERIFY(!(*editor)->isAttached());
    }
};

QTEST_GUILESS_MAIN(TestVst3Editor)
#include "tst_vst3_editor.moc"
