#include "ArtworkCache.h"

#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <thread>

using namespace openstage::ui;
using namespace Qt::StringLiterals;

namespace {

QString makeFakePlugin(const QTemporaryDir& dir, const QString& name)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write("plugin v1");
    return path;
}

QImage testImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(u"#4a8fe7"_s));
    return image;
}

} // namespace

class TestArtworkCache : public QObject
{
    Q_OBJECT

private slots:
    void storesAndFindsArtwork()
    {
        QTemporaryDir plugins;
        QTemporaryDir cacheDir;
        const QString plugin = makeFakePlugin(plugins, u"Synth.vst3"_s);
        ArtworkCache cache(cacheDir.path());
        QVERIFY(cache.urlFor(plugin).isEmpty());

        QSignalSpy changed(&cache, &ArtworkCache::artworkChanged);
        QVERIFY(cache.store(plugin, testImage(1200, 800)).has_value());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toString(), plugin);

        const QString url = cache.urlFor(plugin);
        QVERIFY(url.startsWith(u"file:"_s));
        const QImage stored(QUrl(url).toLocalFile());
        QVERIFY(!stored.isNull());
        QVERIFY(stored.width() <= ArtworkCache::kMaxWidth); // kept small
    }

    void updatedPluginNeedsNewArtwork()
    {
        QTemporaryDir plugins;
        QTemporaryDir cacheDir;
        const QString plugin = makeFakePlugin(plugins, u"Synth.vst3"_s);
        ArtworkCache cache(cacheDir.path());
        QVERIFY(cache.store(plugin, testImage(300, 200)).has_value());
        QVERIFY(cache.has(plugin));

        std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // file times have 1 s resolution on some disks
        {
            QFile file(plugin);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
            file.write(" updated");
        }
        QVERIFY(!cache.has(plugin)); // the plugin changed since its picture was taken
    }

    void emptyImageIsAnError()
    {
        QTemporaryDir plugins;
        QTemporaryDir cacheDir;
        ArtworkCache cache(cacheDir.path());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"empty picture"_s));
        const auto stored = cache.store(makeFakePlugin(plugins, u"X.vst3"_s), QImage());
        QVERIFY(!stored);
    }
};

QTEST_GUILESS_MAIN(TestArtworkCache)
#include "tst_artwork_cache.moc"
