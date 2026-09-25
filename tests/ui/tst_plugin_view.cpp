// The Instrument tab's plugin view with a real plugin in a real window,
// measured through Windows: the sizes the user would see. The window sits
// off screen. Skips when the plugin or an audio device is missing.
#include "DocumentController.h"
#include "EditorService.h"
#include "PluginEditorHost.h"

#include "gigchain/engine/RealEngineFactory.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <windows.h>

#include <array>
#include <memory>

using namespace gigchain;
using namespace Qt::StringLiterals;

namespace {

const QString kAnalogLab = u"C:/Program Files/Common Files/VST3/Arturia/Analog Lab V.vst3"_s;

struct Measured
{
    QSize window; // our window, the one the plugin is attached to
    QSize plugin; // the plugin's own window inside it
    bool pluginVisible = false;
};

// Our window is the only child of the main window; the plugin's is its child.
Measured measure(const QQuickWindow& main)
{
    Measured m;
    // A window id is an HWND on Windows: the cast is the only way to it.
    HWND ours = GetWindow(reinterpret_cast<HWND>(main.winId()), GW_CHILD); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    if (ours == nullptr) return m;
    RECT r{};
    GetWindowRect(ours, &r);
    m.window = QSize(r.right - r.left, r.bottom - r.top);
    HWND plugin = GetWindow(ours, GW_CHILD);
    if (plugin == nullptr) return m;
    GetWindowRect(plugin, &r);
    m.plugin = QSize(r.right - r.left, r.bottom - r.top);
    m.pluginVisible = IsWindowVisible(plugin) != FALSE;
    return m;
}

// Every window under `parent`, one line each: class, size, visible, style.
void dumpTree(HWND parent, int depth, QStringList& out)
{
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        std::array<wchar_t, 128> className{};
        const int length = GetClassNameW(child, className.data(), static_cast<int>(className.size()));
        RECT r{};
        GetWindowRect(child, &r);
        out << u"%1%2 %3x%4 at %5,%6 visible %7 style %8"_s.arg(QString(static_cast<qsizetype>(depth) * 2, u' '))
                   .arg(QString::fromWCharArray(className.data(), length))
                   .arg(r.right - r.left)
                   .arg(r.bottom - r.top)
                   .arg(r.left)
                   .arg(r.top)
                   .arg(IsWindowVisible(child))
                   .arg(static_cast<qulonglong>(GetWindowLongPtrW(child, GWL_STYLE)), 0, 16);
        dumpTree(child, depth + 1, out);
    }
}

QString describe(const Measured& m)
{
    return u"window %1x%2, plugin %3x%4, plugin visible %5"_s.arg(m.window.width())
        .arg(m.window.height())
        .arg(m.plugin.width())
        .arg(m.plugin.height())
        .arg(m.pluginVisible);
}

} // namespace

class TestPluginView : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<engine::IEngine> m_engine;
    std::unique_ptr<ui::DocumentController> m_doc;
    std::unique_ptr<ui::EditorService> m_service;
    std::unique_ptr<QQuickWindow> m_window;
    ui::PluginEditorHost* m_host = nullptr;

    // Lets the view react (Qt's events, the plugin's own window messages).
    static void settle(int ms = 300) { QTest::qWait(ms); }

private slots:
    void initTestCase()
    {
        // ctest starts tests hidden (measured: STARTF_USESHOWWINDOW, SW_HIDE),
        // and Windows applies that to the first window a process shows, not
        // the one it asked for. A throwaway window takes it.
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        GetStartupInfoW(&startup);
        qInfo() << "started with flags" << Qt::hex << startup.dwFlags << "show" << startup.wShowWindow;
        QWindow first;
        first.setGeometry(-3000, -3000, 10, 10);
        first.show();
        first.hide();
    }

    void init()
    {
        if (!QFileInfo::exists(kAnalogLab)) QSKIP("Arturia Analog Lab V not installed");
        auto created = engine::createRealEngine();
        if (!created && created.error().code == core::ErrorCode::DeviceUnavailable) QSKIP("No audio device");
        QVERIFY2(created.has_value(), created ? "" : qPrintable(created.error().message));
        m_engine = std::move(*created);
        m_engine->setMasterVolume(-96.0);
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"s.ini"_s), QSettings::IniFormat);
        m_doc = std::make_unique<ui::DocumentController>(*m_engine, *m_settings);
        m_doc->newSetlist();
        QVERIFY(m_doc->addChannel(kAnalogLab, u"Analog Lab V"_s));
        m_service = std::make_unique<ui::EditorService>(*m_engine, *m_doc);

        m_window = std::make_unique<QQuickWindow>();
        m_window->setGeometry(-3000, -3000, 1400, 1000); // off screen: nothing shows on the user's
        m_window->show();
        // Measured through Windows below: only meaningful if Windows shows it.
        const auto mainHandle = reinterpret_cast<HWND>(m_window->winId()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        QVERIFY2(IsWindowVisible(mainHandle) != FALSE, "the test's own window is not on screen");
        m_host = new ui::PluginEditorHost(m_window->contentItem());
        m_host->setSize(QSizeF(1300, 900));
        m_host->setService(m_service.get());
        QTRY_VERIFY_WITH_TIMEOUT(m_host->hasEditor(), 15000);
        settle();
    }

    void cleanup()
    {
        m_window.reset(); // takes the view with it
        m_host = nullptr;
        m_service.reset();
        m_doc.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    void thePluginOpensAtARealSizeInsideTheArea()
    {
        const Measured m = measure(*m_window);
        qInfo().noquote() << "opened:" << describe(m);
        const auto mainHandle = reinterpret_cast<HWND>(m_window->winId()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        qInfo().noquote() << "main window: Qt visible" << m_window->isVisible() << "exposed" << m_window->isExposed()
                          << "Windows visible" << IsWindowVisible(mainHandle) << "style" << Qt::hex
                          << static_cast<qulonglong>(GetWindowLongPtrW(mainHandle, GWL_STYLE));
        QStringList tree;
        dumpTree(reinterpret_cast<HWND>(m_window->winId()), 0, tree); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        qInfo().noquote() << "window tree:\n" + tree.join(u'\n');
        QVERIFY2(m.window.width() > 200 && m.window.height() > 150, qPrintable(describe(m)));
        QVERIFY2(m.plugin.width() > 200 && m.plugin.height() > 150, qPrintable(describe(m)));
        QVERIFY2(m.pluginVisible, qPrintable(describe(m)));
        QVERIFY2(m.window.width() <= 1300 && m.window.height() <= 900, qPrintable(describe(m))); // inside the area
    }

    void aSmallerAreaNeverGetsCovered()
    {
        m_host->setSize(QSizeF(400, 250));
        settle();
        const Measured m = measure(*m_window);
        qInfo().noquote() << "area 400x250:" << describe(m);
        QVERIFY2(m.window.width() <= 400 && m.window.height() <= 250, qPrintable(describe(m)));
        QVERIFY2(m.window.width() > 100 && m.window.height() > 100, qPrintable(describe(m)));
    }

    void theAreaGrowingBackGivesThePluginItsSizeBack()
    {
        const Measured before = measure(*m_window);
        m_host->setSize(QSizeF(400, 250));
        settle();
        m_host->setSize(QSizeF(1300, 900));
        settle();
        const Measured after = measure(*m_window);
        qInfo().noquote() << "back to 1300x900:" << describe(after) << "(first:" << describe(before) << ")";
        QCOMPARE(after.window, before.window);
        QVERIFY2(after.plugin.width() >= before.plugin.width() && after.plugin.height() >= before.plugin.height(),
                 qPrintable(describe(after)));
    }
};

// A real window (the "windows" platform), not the off-screen one the other
// UI tests use: plugins draw into real native windows.
int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "windows");
    QGuiApplication app(argc, argv);
    TestPluginView test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_plugin_view.moc"
