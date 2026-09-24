// Integration test on the real Windows display: builds artwork for the
// FabFilter plugins on this machine and checks each picture has real content.
// Windows are placed off-screen, so nothing appears on screen. Skips when the
// folder or an audio device is missing.
#include "ArtworkBuilder.h"
#include "ArtworkCache.h"

#include "openstage/core/FileLog.h"
#include "openstage/engine/RealEngineFactory.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace openstage;
using namespace openstage::ui;
using namespace Qt::StringLiterals;

namespace {

const QString kFolder = u"C:/Program Files/Common Files/VST3/FabFilter"_s;

// Standard deviation of brightness: blank or black captures are ~0.
double contentVariation(const QImage& image)
{
    const QImage small = image.scaled(64, 64).convertToFormat(QImage::Format_Grayscale8);
    double sum = 0.0;
    double sumSq = 0.0;
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const double v = qGray(small.pixel(x, y));
            sum += v;
            sumSq += v * v;
        }
    }
    const double n = small.width() * small.height();
    return std::sqrt(std::max(0.0, sumSq / n - (sum / n) * (sum / n)));
}

} // namespace

class TestArtworkCapture : public QObject
{
    Q_OBJECT

private slots:
    // Installed after QTest's own handler, which it chains to. Flushed per
    // line, so it survives a plugin crashing the process.
    void initTestCase() { QVERIFY(core::FileLog::install(QDir::temp().filePath(u"openstage-artwork-capture.log"_s))); }
    void cleanupTestCase() { core::FileLog::uninstall(); }

    void capturesRealPluginEditors()
    {
        if (!QFileInfo(kFolder).isDir()) QSKIP("FabFilter plugins not installed");
        engine::RealEngineOptions options;
        options.pluginFolder = kFolder;
        auto created = engine::createRealEngine(options);
        if (!created) QSKIP("No audio device");
        engine::IEngine& eng = **created;
        eng.setMasterVolume(-96.0);

        QTemporaryDir dir;
        ArtworkCache cache(dir.path());
        ArtworkBuilder builder(eng, cache);
        QSignalSpy finished(&builder, &ArtworkBuilder::finished);
        builder.start();
        const int total = builder.total();
        QVERIFY(total > 0);
        QVERIFY(finished.wait(total * 8000));

        int good = 0;
        for (const auto& plugin : eng.availablePlugins()) {
            const QString url = cache.urlFor(plugin.id);
            QVERIFY2(!url.isEmpty(), qPrintable(u"no picture for "_s + plugin.name));
            const QImage image(QUrl(url).toLocalFile());
            const double variation = contentVariation(image);
            qInfo().noquote() << plugin.name << image.size() << "variation" << variation;
            if (variation > 8.0) ++good;
        }
        QCOMPARE(good, total); // every picture shows an actual editor
    }
};

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "windows"); // real windows: capture needs them
    QGuiApplication app(argc, argv);
    TestArtworkCapture test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_artwork_capture.moc"
