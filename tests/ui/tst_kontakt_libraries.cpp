#include "KontaktLibraries.h"
#include "LibraryListModel.h"

#include <QAbstractItemModelTester>
#include <QSettings>
#include <QSignalSpy>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace openstage;
using namespace openstage::ui;
using namespace Qt::StringLiterals;

namespace {

QByteArray png(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::darkBlue);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// Same layout as a real .nicnt (per KoEd's parser): binary header, the
// ProductHints XML, NI section markers, then the embedded PNGs (the wide
// Kontakt banner first, then NKS artwork), then LibInfo XML.
QByteArray fakeNicnt(const QString& name, const QString& company)
{
    QByteArray data("\x01\x02\x03\x04binary-header", 17);
    data += QStringLiteral("<?xml version=\"1.0\"?><ProductHints><Product><Name>%1</Name><Company>%2</Company>"
                           "<FactoryLibrary><Name>%1</Name></FactoryLibrary></Product></ProductHints>")
                .arg(name, company)
                .toUtf8();
    data += "/\\ NI FC MTD  /\\ more binary /\\ NI FC TOC  /\\ ";
    data += png(906, 98);  // Kontakt library banner
    data += png(134, 66);  // MST_artwork
    data += png(96, 47);   // VB_artwork
    data += "<?xml version=\"1.0\"?><soundinfos></soundinfos>";
    return data;
}

void write(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

} // namespace

class TestKontaktLibraries : public QObject
{
    Q_OBJECT

private slots:
    void extractsTheLibraryBanner()
    {
        QTemporaryDir dir;
        const QString nicnt = dir.filePath(u"Lib/My Library.nicnt"_s);
        write(nicnt, fakeNicnt(u"My Library"_s, u"Maker"_s));
        const auto banner = extractNicntBanner(nicnt);
        QVERIFY2(banner.has_value(), banner ? "" : qPrintable(banner.error().message));
        const QImage image = QImage::fromData(*banner);
        QCOMPARE(image.size(), QSize(906, 98)); // the wide banner, not the NKS thumbnails
    }

    void readsNameAndCompany()
    {
        QTemporaryDir dir;
        const QString nicnt = dir.filePath(u"Lib/file-name.nicnt"_s);
        write(nicnt, fakeNicnt(u"Albion NEO"_s, u"Spitfire Audio"_s));
        const auto hints = readNicntProduct(nicnt);
        QCOMPARE(hints.name, u"Albion NEO"_s);
        QCOMPARE(hints.company, u"Spitfire Audio"_s);
    }

    void fileWithoutPictureIsAnError()
    {
        QTemporaryDir dir;
        const QString nicnt = dir.filePath(u"Empty.nicnt"_s);
        write(nicnt, "no pictures here");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no library banner"_s));
        QVERIFY(!extractNicntBanner(nicnt));
    }

    void scanFindsLibrariesAndCachesBanners()
    {
        QTemporaryDir libraries;
        QTemporaryDir cache;
        write(libraries.filePath(u"Strings/Strings.nicnt"_s), fakeNicnt(u"Strings"_s, u"Maker A"_s));
        write(libraries.filePath(u"Vendor/Drums Library/Drums.nicnt"_s), fakeNicnt(u"Drums"_s, u"Maker B"_s));
        // A wallpaper.png beside the .nicnt overrides the embedded banner (Kontakt behaviour).
        write(libraries.filePath(u"Strings/wallpaper.png"_s), png(574, 99));

        const auto found = scanKontaktLibraries({libraries.path()}, cache.path());
        QCOMPARE(found.size(), std::size_t{2});
        QCOMPARE(found[0].name, u"Drums"_s); // sorted by name
        QCOMPARE(found[0].company, u"Maker B"_s);
        QVERIFY(QFileInfo::exists(found[0].bannerPath));
        QCOMPARE(QImage(found[0].bannerPath).size(), QSize(906, 98));
        QVERIFY(found[1].bannerPath.endsWith(u"wallpaper.png"_s));
    }

    void modelRemembersFoldersAndListsLibraries()
    {
        QTemporaryDir root;
        write(root.filePath(u"Libs/Strings/Strings.nicnt"_s), fakeNicnt(u"Strings"_s, u"Maker A"_s));
        QSettings settings(root.filePath(u"s.ini"_s), QSettings::IniFormat);
        {
            LibraryListModel model(settings, root.filePath(u"cache"_s));
            QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
            QCOMPARE(model.rowCount(), 0);
            QSignalSpy scanned(&model, &LibraryListModel::scanFinished);
            model.addFolder(QUrl::fromLocalFile(root.filePath(u"Libs"_s)));
            QVERIFY(scanned.wait(10000));
            QCOMPARE(model.rowCount(), 1);
            const auto roles = model.roleNames();
            const QModelIndex first = model.index(0);
            QCOMPARE(model.data(first, roles.key("name")).toString(), u"Strings"_s);
            QCOMPARE(model.data(first, roles.key("company")).toString(), u"Maker A"_s);
            QVERIFY(model.data(first, roles.key("bannerUrl")).toString().startsWith(u"file:"_s));
        }
        // A new model (next app start) remembers the folder.
        LibraryListModel again(settings, root.filePath(u"cache"_s));
        QSignalSpy scanned(&again, &LibraryListModel::scanFinished);
        QVERIFY(scanned.wait(10000));
        QCOMPARE(again.rowCount(), 1);
        QCOMPARE(again.folders(), QStringList{root.filePath(u"Libs"_s)});
    }

    void realLibraryOnThisMachine()
    {
        const QString nicnt = u"D:/Sample Libraries/SesionsHorns/Session Horns Pro Library/Session Horns Pro.nicnt"_s;
        if (!QFileInfo::exists(nicnt)) QSKIP("Session Horns Pro not installed");
        const auto banner = extractNicntBanner(nicnt);
        QVERIFY(banner.has_value());
        QCOMPARE(QImage::fromData(*banner).size(), QSize(587, 98));
        QCOMPARE(readNicntProduct(nicnt).name, u"Session Horns Pro"_s);
    }
};

QTEST_GUILESS_MAIN(TestKontaktLibraries)
#include "tst_kontakt_libraries.moc"
