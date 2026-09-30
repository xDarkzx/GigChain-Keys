// The plugin scan cache: plugins are opened once, then read from the cache
// until their file changes (opening 63 plugins at every start froze the PC).
#include "PluginCatalog.h"
#include "TestPlugins.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

const QString kSmallPlugin = gigchain::test::kEffect.path; // TDR Kotelnikov; Surge XT Effects on Linux

} // namespace

class TestPluginCache : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(QDir(m_dir->path()).mkpath(u"VST3"_s));
        m_folder = m_dir->filePath(u"VST3"_s);
        m_cache = m_dir->filePath(u"plugin-cache.json"_s);
    }

    void aPluginIsOpenedOnceThenReadFromTheCache()
    {
        if (!QFileInfo::exists(kSmallPlugin)) QSKIP("The test effect is not installed");
        // (Under its own name: a Linux bundle's binary is named after it.)
        QVERIFY(gigchain::test::copyPlugin(kSmallPlugin, m_folder + u'/' + QFileInfo(kSmallPlugin).fileName()));

        ScanStats first;
        const auto scanned = PluginCatalog::scan(m_folder, m_cache, &first);
        QCOMPARE(scanned.size(), std::size_t{1});
        QCOMPARE(first.opened, 1);
        QCOMPARE(first.fromCache, 0);
        QVERIFY(QFileInfo::exists(m_cache));

        ScanStats second;
        const auto cached = PluginCatalog::scan(m_folder, m_cache, &second);
        QCOMPARE(second.opened, 0); // not opened again
        QCOMPARE(second.fromCache, 1);
        QCOMPARE(cached.size(), std::size_t{1});
        QCOMPARE(cached[0].name, scanned[0].name);
        QCOMPARE(cached[0].vendor, scanned[0].vendor);
        QCOMPARE(cached[0].classId, scanned[0].classId);
        QCOMPARE(cached[0].website, scanned[0].website);
        QVERIFY(cached[0].kind == scanned[0].kind);
    }

    void progressIsReportedForTheSplashScreen()
    {
        writeFile(m_folder + u"/A.vst3"_s, "x");
        writeFile(m_folder + u"/B.vst3"_s, "y");
        std::vector<std::pair<int, int>> steps;
        QStringList names;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Skipping plugin .*A\\.vst3"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Skipping plugin .*B\\.vst3"_s));
        (void)PluginCatalog::scan(m_folder, m_cache, nullptr, [&](const QString& name, int done, int total) {
            names << name;
            steps.emplace_back(done, total);
        });
        QCOMPARE(names, (QStringList{u"A"_s, u"B"_s}));
        QCOMPARE(steps, (std::vector<std::pair<int, int>>{{0, 2}, {1, 2}}));
    }

    void aChangedFileIsOpenedAgain()
    {
        const QString broken = m_folder + u"/Broken.vst3"_s;
        writeFile(broken, "not a plugin");
        ScanStats first;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Skipping plugin .*Broken\\.vst3"_s));
        QVERIFY(PluginCatalog::scan(m_folder, m_cache, &first).empty());
        QCOMPARE(first.opened, 1);
        QCOMPARE(first.failed, 1);

        // Still failing and unchanged: skipped without opening it, but still reported.
        ScanStats second;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Skipping plugin .*Broken\\.vst3 .*failed before"_s));
        QVERIFY(PluginCatalog::scan(m_folder, m_cache, &second).empty());
        QCOMPARE(second.opened, 0);
        QCOMPARE(second.failed, 1);

        writeFile(broken, "still not a plugin, but a different file"); // e.g. the plugin was updated
        ScanStats third;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Skipping plugin .*Broken\\.vst3"_s));
        (void)PluginCatalog::scan(m_folder, m_cache, &third);
        QCOMPARE(third.opened, 1);
    }

    // A cache grown past any real one (a runaway write, disk damage) is not
    // read into memory: it counts as no cache, and says so.
    void anOversizedCacheIsNotRead()
    {
        QFile file(m_cache);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(PluginCatalog::kMaxCacheBytes + 1));
        file.close();
        ScanStats stats;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Plugin cache .* too large"_s));
        QVERIFY(PluginCatalog::scan(m_folder, m_cache, &stats).empty());
        QCOMPARE(stats.fromCache, 0);
    }

    void anUnreadableCacheMeansAFullScan()
    {
        writeFile(m_cache, "{ this is not json");
        ScanStats stats;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Plugin cache .* unreadable"_s));
        QVERIFY(PluginCatalog::scan(m_folder, m_cache, &stats).empty());
        QCOMPARE(stats.fromCache, 0);
    }

    void aRemovedPluginLeavesTheList()
    {
        if (!QFileInfo::exists(kSmallPlugin)) QSKIP("The test effect is not installed");
        const QString copy = m_folder + u'/' + QFileInfo(kSmallPlugin).fileName();
        QVERIFY(gigchain::test::copyPlugin(kSmallPlugin, copy));
        QCOMPARE(PluginCatalog::scan(m_folder, m_cache).size(), std::size_t{1});
        QVERIFY(gigchain::test::removePlugin(copy));
        QVERIFY(PluginCatalog::scan(m_folder, m_cache).empty());
    }

private:
    static void writeFile(const QString& path, const QByteArray& bytes)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_folder;
    QString m_cache;
};

QTEST_GUILESS_MAIN(TestPluginCache)
#include "tst_plugin_cache.moc"
