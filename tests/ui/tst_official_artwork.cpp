#include "OfficialArtwork.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QBuffer>
#include <QDataStream>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

void writeImage(const QString& path, const char* format)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY2(image.save(path, format), qPrintable(path));
}

// An .ico holding one PNG per size, in the given order: the ICONDIR header,
// one 16-byte ICONDIRENTRY per image, then the images.
void writeIco(const QString& path, const QList<int>& sides)
{
    QList<QByteArray> pngs;
    for (const int side : sides) {
        QImage image(side, side, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        pngs << png;
    }
    QByteArray ico;
    QDataStream out(&ico, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint16{0} << quint16{1} << static_cast<quint16>(sides.size());
    quint32 offset = 6 + (16 * static_cast<quint32>(sides.size()));
    for (qsizetype i = 0; i < sides.size(); ++i) {
        const auto side = static_cast<quint8>(sides.at(i) >= 256 ? 0 : sides.at(i));
        out << side << side << quint8{0} << quint8{0} << quint16{1} << quint16{32}
            << static_cast<quint32>(pngs.at(i).size()) << offset;
        offset += static_cast<quint32>(pngs.at(i).size());
    }
    for (const QByteArray& png : pngs) out.writeRawData(png.constData(), static_cast<int>(png.size()));
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(ico), ico.size());
}

// Windows installers hide a folder's icon (and its desktop.ini). Other
// systems have no hidden attribute: the file stays as it is.
void hide(const QString& path)
{
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    QVERIFY(SetFileAttributesW(native.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) != 0);
#else
    QVERIFY(QFileInfo::exists(path));
#endif
}

void writeFile(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("plugin");
}

engine::PluginInfo plugin(const QString& id, const QString& classId = {})
{
    engine::PluginInfo info{.id = id,
                            .name = QFileInfo(id).completeBaseName(),
                            .vendor = u"Maker"_s,
                            .kind = engine::PluginKind::Instrument,
                            .subCategories = u"Instrument"_s,
                            .version = u"1.0"_s,
                            .classId = classId,
                            .website = {},
                            .email = {},
                            .sdkVersion = {}};
    return info;
}

} // namespace

class TestOfficialArtwork : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_vst3; // the plugin folder of each test

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_vst3 = m_dir->filePath(u"VST3"_s);
        QVERIFY(QDir().mkpath(m_vst3));
    }

    void vst3SnapshotComesFromTheBundle()
    {
        // Steinberg's format: Contents/Resources/Snapshots/<class id>_snapshot[_2.0x].png
        const QString bundle = m_vst3 + u"/Synth.vst3"_s;
        const QString cid = u"84E8DE5F92554F5396FAE4133C935A18"_s;
        writeImage(bundle + u"/Contents/Resources/Snapshots/"_s + cid + u"_snapshot.png"_s, "PNG");
        writeImage(bundle + u"/Contents/Resources/Snapshots/"_s + cid + u"_snapshot_2.0x.png"_s, "PNG");

        const PluginArtwork found = OfficialArtwork(m_vst3).find(plugin(bundle, cid));
        QVERIFY(found.banner.endsWith(u"_snapshot_2.0x.png"_s)); // the sharper one
    }

    void theBundlesOwnIconEvenWhenHidden()
    {
        // As Splice Instrument installs it: Plugin.ico (any case), hidden.
        const QString bundle = m_vst3 + u"/Splice/Splice INSTRUMENT.vst3"_s;
        writeFile(bundle + u"/Contents/x86_64-win/Splice INSTRUMENT.vst3"_s);
        writeImage(bundle + u"/Plugin.ico"_s, "ICO");
        hide(bundle + u"/Plugin.ico"_s);
        writeImage(m_vst3 + u"/Splice/PlugIn.ico"_s, "ICO"); // the maker's: the bundle's own comes first

        const PluginArtwork found = OfficialArtwork(m_vst3).find(plugin(bundle));
        QCOMPARE(QDir::cleanPath(found.icon), QDir::cleanPath(bundle + u"/Plugin.ico"_s));
    }

    void aMakersFolderIconIsSharedByItsPlugins()
    {
        // As Arturia installs it: single-file plugins, the icon on their folder.
        const QString maker = m_vst3 + u"/Maker"_s;
        writeFile(maker + u"/Piano.vst3"_s);
        writeFile(maker + u"/Synths/Pad.vst3"_s);
        writeImage(maker + u"/PlugIn.ico"_s, "ICO");
        hide(maker + u"/PlugIn.ico"_s);

        const OfficialArtwork artwork(m_vst3);
        QCOMPARE(QDir::cleanPath(artwork.find(plugin(maker + u"/Piano.vst3"_s)).icon), maker + u"/PlugIn.ico"_s);
        QCOMPARE(QDir::cleanPath(artwork.find(plugin(maker + u"/Synths/Pad.vst3"_s)).icon), maker + u"/PlugIn.ico"_s);
    }

    void neverAnIconOfThePluginFolderOrAnotherMaker()
    {
        writeImage(m_vst3 + u"/PlugIn.ico"_s, "ICO");            // belongs to no plugin
        writeImage(m_vst3 + u"/Other/PlugIn.ico"_s, "ICO");      // another maker's
        writeImage(m_dir->filePath(u"PlugIn.ico"_s), "ICO");      // above the plugin folder
        writeFile(m_vst3 + u"/Loose.vst3"_s);
        writeFile(m_vst3 + u"/Maker/Keys.vst3"_s);

        const OfficialArtwork artwork(m_vst3);
        QVERIFY(artwork.find(plugin(m_vst3 + u"/Loose.vst3"_s)).isEmpty());
        QVERIFY(artwork.find(plugin(m_vst3 + u"/Maker/Keys.vst3"_s)).isEmpty());
    }

    void withoutAPluginFolderOnlyTheBundleIsSearched()
    {
        writeImage(m_vst3 + u"/Maker/PlugIn.ico"_s, "ICO");
        writeFile(m_vst3 + u"/Maker/Keys.vst3"_s);
        QVERIFY(OfficialArtwork(QString()).find(plugin(m_vst3 + u"/Maker/Keys.vst3"_s)).isEmpty());
    }

    void theProviderGivesTheLargestSizeScaledToTheRequest()
    {
        // An .ico with three sizes, smallest first (as makers write them).
        const QString path = m_vst3 + u"/Maker Name/PlugIn.ico"_s; // a space: the url must survive it
        writeIco(path, {16, 48, 128});
        QCOMPARE(QImageReader(path).imageCount(), 3);

        PluginIconProvider provider;
        const QString id = PluginIconProvider::url(path).section(u'/', 3);
        QSize size;
        QCOMPARE(provider.requestImage(id, &size, {}).size(), QSize(128, 128));
        QCOMPARE(provider.requestImage(id, &size, QSize(40, 40)).size(), QSize(40, 40));
        QCOMPARE(size, QSize(40, 40));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Cannot read the plugin icon .*missing\\.ico"_s));
        QVERIFY(provider.requestImage(PluginIconProvider::url(m_vst3 + u"/missing.ico"_s).section(u'/', 3), &size, {})
                    .isNull());
    }

    void realMachineIconsAreFoundAndReadable()
    {
        // The installed plugins on this machine (skipped where absent).
        const QString vst3 = u"C:/Program Files/Common Files/VST3"_s;
        const OfficialArtwork artwork(vst3);
        const QStringList installed = {vst3 + u"/Serum2.vst3"_s, vst3 + u"/Arturia/Piano V2.vst3"_s};
        PluginIconProvider provider;
        const QString prefix = u"image://"_s + QLatin1StringView(PluginIconProvider::kName) + u'/';
        int checked = 0;
        for (const QString& id : installed) {
            if (!QFileInfo::exists(id)) continue;
            const QString icon = artwork.find(plugin(id)).icon;
            QVERIFY2(!icon.isEmpty(), qPrintable(u"no icon for "_s + id));
            // What QML asks for: the id is the url after the provider's name.
            const QString url = PluginIconProvider::url(icon);
            QVERIFY2(url.startsWith(prefix), qPrintable(url));
            QSize size;
            const QImage image = provider.requestImage(url.mid(prefix.size()), &size, {});
            QVERIFY2(!image.isNull(), qPrintable(icon));
            QVERIFY2(image.width() >= 256, qPrintable(u"%1 is only %2 px"_s.arg(icon).arg(image.width())));
            QCOMPARE(size, image.size());
            ++checked;
        }
        if (checked == 0) QSKIP("Neither Serum 2 nor Arturia Piano V2 is installed");
    }
};

QTEST_GUILESS_MAIN(TestOfficialArtwork)
#include "tst_official_artwork.moc"
