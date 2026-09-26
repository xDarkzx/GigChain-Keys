// Reading new plugins happens in a scanner process of its own (as Audacity
// 4 does): a plugin that crashes while being read ends that process only,
// never the host. Uses a fake plugin that crashes on purpose, and a small
// real one (skipped when it is not installed).
#include "PluginCatalog.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <windows.h>

#include <crtdbg.h>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

const QString kReal = u"C:/Program Files/Common Files/VST3/TDR Kotelnikov.vst3"_s;
constexpr const char* kCrasherPath = GIGCHAIN_CRASHING_PLUGIN;
constexpr const char* kScannerPath = GIGCHAIN_PLUGIN_SCANNER;

} // namespace

class TestPluginScanner : public QObject
{
    Q_OBJECT

    const QString m_crasher = QString::fromUtf8(kCrasherPath);
    const QString m_scanner = QString::fromUtf8(kScannerPath);
    QTemporaryDir m_dir;
    QString m_folder;
    QString m_cache;

private slots:
    // Tests run unattended: a failed C runtime assertion is printed, never a
    // dialog box waiting for a click (tests/common/NoErrorDialogs.cpp).
    // cppcheck-suppress functionStatic ; a Qt Test slot, called through moc: cannot be static
    void initTestCase()
    {
        // No dialog window for either (Qt Test itself sets errors to debugger output only).
        QCOMPARE(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_REPORT_MODE) & _CRTDBG_MODE_WNDW, 0);
        QCOMPARE(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_REPORT_MODE) & _CRTDBG_MODE_WNDW, 0);
        QVERIFY((GetErrorMode() & SEM_NOGPFAULTERRORBOX) != 0);
    }

    void init()
    {
        if (!QFileInfo::exists(kReal)) QSKIP("TDR Kotelnikov not installed");
        QVERIFY(QFileInfo::exists(m_crasher));
        QVERIFY(QFileInfo::exists(m_scanner));
        m_folder = m_dir.filePath(u"VST3"_s);
        m_cache = m_dir.filePath(u"plugin-cache.json"_s);
        QVERIFY(QDir().mkpath(m_folder));
        QFile::remove(m_folder + u"/Crasher.vst3"_s);
        QFile::remove(m_folder + u"/TDR Kotelnikov.vst3"_s);
        QFile::remove(m_cache);
        QVERIFY(QFile::copy(m_crasher, m_folder + u"/Crasher.vst3"_s));
        QVERIFY(QFile::copy(kReal, m_folder + u"/TDR Kotelnikov.vst3"_s));
    }

    void aPluginThatCrashesWhileBeingReadDoesNotTakeTheHostDown()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Skipping plugin .*Crasher\\.vst3 : it crashed while being read"_s));
        ScanStats stats;
        const auto plugins = PluginCatalog::scan(m_folder, m_cache, &stats, {}, nullptr, m_scanner);
        // Still here: the crash ended the scanner's process, not this one.
        QCOMPARE(plugins.size(), std::size_t{1});
        QVERIFY(plugins.front().name.contains(u"Kotelnikov"_s));
        QCOMPARE(stats.opened, 2);
        QCOMPARE(stats.failed, 1);

        // Remembered, with the reason, so it is not opened again...
        QFile file(m_cache);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY2(file.readAll().contains("crashed while being read"), "the crash is not in the cache");
        file.close();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Skipping plugin .*Crasher\\.vst3 : it crashed while being read"_s));
        ScanStats again;
        const auto second = PluginCatalog::scan(m_folder, m_cache, &again, {}, nullptr, m_scanner);
        QCOMPARE(second.size(), std::size_t{1});
        QCOMPARE(again.opened, 0); // ...until its file changes
        QCOMPARE(again.fromCache, 2);
    }

    void theScannerReadsWhatTheHostWouldRead()
    {
        // The same plugin, read in this process and by the scanner, gives the same details.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Skipping plugin .*Crasher"_s));
        const auto outside = PluginCatalog::scan(m_folder, {}, nullptr, {}, nullptr, m_scanner);
        QVERIFY(QFile::remove(m_folder + u"/Crasher.vst3"_s)); // never read in this process
        const auto inside = PluginCatalog::scan(m_folder, {}, nullptr, {}, nullptr, {});
        QCOMPARE(outside.size(), std::size_t{1});
        QCOMPARE(inside.size(), std::size_t{1});
        const PluginInfo& a = outside.front();
        const PluginInfo& b = inside.front();
        QCOMPARE(a.id, b.id);
        QCOMPARE(a.name, b.name);
        QCOMPARE(a.vendor, b.vendor);
        QVERIFY(a.kind == b.kind);
        QCOMPARE(a.subCategories, b.subCategories);
        QCOMPARE(a.version, b.version);
        QCOMPARE(a.classId, b.classId);
    }

    void aMissingScannerFallsBackToReadingHereAndSaysSo()
    {
        QVERIFY(QFile::remove(m_folder + u"/Crasher.vst3"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"plugin scanner .*not found.*reading plugins in this process"_s));
        const auto plugins = PluginCatalog::scan(m_folder, {}, nullptr, {}, nullptr, m_dir.filePath(u"NoScanner.exe"_s));
        QCOMPARE(plugins.size(), std::size_t{1});
    }
};

QTEST_GUILESS_MAIN(TestPluginScanner)
#include "tst_plugin_scanner.moc"
