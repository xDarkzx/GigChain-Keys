// Opens a real plugin's editor inside a hidden native window. Skips when the
// plugin is not installed. Nothing is shown on screen.
#include "Vst3Node.h"

#include <QFileInfo>
#include <QtTest>

#include <windows.h>
#include <ole2.h>

#include <cstdlib>

using namespace openstage;
using namespace openstage::engine;
using namespace Qt::StringLiterals;

namespace {

const QString kPiano = u"C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"_s;

// A hidden top-level window for the plugin to attach its view to.
class HiddenParent
{
public:
    HiddenParent()
        : m_hwnd(CreateWindowExW(0, L"STATIC", L"OpenStage test parent", WS_POPUP, 0, 0, 800, 600, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr))
    {
    }
    ~HiddenParent() { DestroyWindow(m_hwnd); }
    HiddenParent(const HiddenParent&) = delete;
    HiddenParent& operator=(const HiddenParent&) = delete;
    HiddenParent(HiddenParent&&) = delete;
    HiddenParent& operator=(HiddenParent&&) = delete;

    [[nodiscard]] quintptr handle() const { return reinterpret_cast<quintptr>(m_hwnd); }

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
        const auto attached = (*editor)->attach(parent.handle());
        QVERIFY2(attached.has_value(), attached ? "" : qPrintable(attached.error().message));
        QVERIFY((*editor)->isAttached());

        QSize requested;
        (*editor)->setResizeHandler([&requested](QSize s) { requested = s; });
        (*editor)->detach();
        QVERIFY(!(*editor)->isAttached());
    }

    void scalableEditorFollowsContentScale()
    {
        // Serum 2 supports host scaling (measured on this machine).
        const QString serum = u"C:/Program Files/Common Files/VST3/Serum2.vst3"_s;
        if (!QFileInfo::exists(serum)) QSKIP("Serum 2 not installed");
        auto node = Vst3Node::load(serum, 48000.0, 256);
        QVERIFY(node.has_value());
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY(editor.has_value() && *editor != nullptr);
        HiddenParent parent;
        QVERIFY((*editor)->attach(parent.handle()).has_value());

        QVERIFY((*editor)->setContentScale(1.0));
        const QSize full = (*editor)->preferredSize();
        QVERIFY((*editor)->setContentScale(0.5));
        const QSize half = (*editor)->preferredSize();
        QVERIFY2(std::abs(half.width() * 2 - full.width()) <= 2 && std::abs(half.height() * 2 - full.height()) <= 2,
                 qPrintable(u"%1x%2 -> %3x%4"_s.arg(full.width()).arg(full.height()).arg(half.width()).arg(half.height())));
        (*editor)->detach();
    }

    void fixedEditorSaysItCannotScale()
    {
        // Arturia plugins only zoom from their own menu (measured on this machine).
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto node = Vst3Node::load(kPiano, 48000.0, 256);
        QVERIFY(node.has_value());
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY(editor.has_value() && *editor != nullptr);
        HiddenParent parent;
        QVERIFY((*editor)->attach(parent.handle()).has_value());
        const QSize before = (*editor)->preferredSize();
        QVERIFY(!(*editor)->setContentScale(0.5));
        QCOMPARE((*editor)->preferredSize(), before);
        QVERIFY(!(*editor)->canResize());
        (*editor)->detach();
    }

    void attachingToNothingIsAnError()
    {
        if (!QFileInfo::exists(kPiano)) QSKIP("Arturia Piano V2 not installed");
        auto node = Vst3Node::load(kPiano, 48000.0, 256);
        QVERIFY(node.has_value());
        auto editor = Vst3Node::createEditor(*node);
        QVERIFY(editor.has_value() && *editor != nullptr);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no window to attach"_s));
        const auto attached = (*editor)->attach(0);
        QVERIFY(!attached);
        QVERIFY(!(*editor)->isAttached());
    }
};

QTEST_GUILESS_MAIN(TestVst3Editor)
#include "tst_vst3_editor.moc"
