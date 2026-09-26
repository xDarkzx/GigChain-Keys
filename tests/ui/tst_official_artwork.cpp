#include "OfficialArtwork.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

void writePng(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(8, 4, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(path, "PNG"));
}

void writeText(const QString& path, const QByteArray& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

engine::PluginInfo plugin(const QString& id, const QString& name, const QString& vendor, const QString& classId = {})
{
    engine::PluginInfo info{id, name, vendor, engine::PluginKind::Instrument, u"Instrument"_s, u"1.0"_s};
    info.classId = classId;
    return info;
}

} // namespace

class TestOfficialArtwork : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;

    [[nodiscard]] OfficialArtwork::Sources sources(const QHash<QString, QString>& registry = {}) const
    {
        OfficialArtwork::Sources s;
        s.arturiaRoot = m_root.filePath(u"Arturia"_s);
        s.niServiceCenter = m_root.filePath(u"ServiceCenter"_s);
        s.niResources = m_root.filePath(u"NI Resources"_s);
        s.niContentDir = [registry](const QString& key) { return registry.value(key); };
        return s;
    }

private slots:
    void vst3SnapshotComesFromTheBundle()
    {
        // Steinberg's format: Contents/Resources/Snapshots/<class id>_snapshot[_2.0x].png
        const QString bundle = m_root.filePath(u"VST3/Synth.vst3"_s);
        const QString cid = u"84E8DE5F92554F5396FAE4133C935A18"_s;
        writePng(bundle + u"/Contents/Resources/Snapshots/"_s + cid + u"_snapshot.png"_s);
        writePng(bundle + u"/Contents/Resources/Snapshots/"_s + cid + u"_snapshot_2.0x.png"_s);

        const OfficialArtwork artwork(sources());
        const PluginArtwork found = artwork.find(plugin(bundle, u"Synth"_s, u"Maker"_s, cid));
        QVERIFY(found.banner.endsWith(u"_snapshot_2.0x.png"_s)); // sharper one preferred
        QCOMPARE(found.source, u"VST3 snapshot"_s);
    }

    void nksArtworkFoundThroughServiceCenterAndRegistry()
    {
        // Layout copied from Spitfire's BBC Symphony Orchestra on this machine.
        writeText(m_root.filePath(u"ServiceCenter/Spitfire Audio - BBC Symphony Orchestra (64 Bit).xml"_s),
                  "<?xml version=\"1.0\"?><ProductHints><Product version=\"1.1.9.0\">"
                  "<UPID>ce541ff7</UPID><Name>Spitfire Audio - BBC Symphony Orchestra</Name>"
                  "<AuthAppID>500</AuthAppID><PluginID fx=\"false\">0x53616e74</PluginID>"
                  "<Company>Spitfire Audio</Company>"
                  "<FactoryLibrary><Name>Spitfire Audio - BBC Symphony Orchestra</Name><Relevance>"
                  "<Application nativeContent=\"false\" minVersion=\"1.5\">KKontrol</Application>"
                  "</Relevance></FactoryLibrary><Type>Plugin</Type>"
                  "<RegKey>Spitfire Audio - BBC Symphony Orchestra</RegKey>"
                  "<BinName>BBC Symphony Orchestra (64 Bit)</BinName></Product></ProductHints>");
        const QString content = m_root.filePath(u"Spitfire/NKS"_s);
        const QString images = content + u"/PAResources/image/Spitfire Audio/Spitfire Audio - BBC Symphony Orchestra/"_s;
        writePng(images + u"MST_Artwork.png"_s);
        writePng(images + u"VB_Artwork.png"_s);
        writePng(images + u"MST_Logo.png"_s);

        const OfficialArtwork artwork(sources({{u"Spitfire Audio - BBC Symphony Orchestra"_s, content}}));
        const PluginArtwork found = artwork.find(plugin(u"C:/VST3/BBC Symphony Orchestra (64 Bit).vst3"_s,
                                                        u"BBC Symphony Orchestra"_s, u"Spitfire Audio"_s));
        QVERIFY(found.banner.endsWith(u"MST_Artwork.png"_s));
        QVERIFY(found.logo.endsWith(u"MST_Logo.png"_s));
        QCOMPARE(found.source, u"NKS"_s);
    }

    void nksArtworkInSharedNiResources()
    {
        writeText(m_root.filePath(u"ServiceCenter/Maker - Keys.xml"_s),
                  "<ProductHints><Product><Name>Keys</Name><Company>Maker</Company><RegKey>Keys</RegKey>"
                  "<BinName>Keys</BinName></Product></ProductHints>");
        writePng(m_root.filePath(u"NI Resources/image/maker/keys/VB_Artwork.png"_s));
        const OfficialArtwork artwork(sources());
        const PluginArtwork found = artwork.find(plugin(u"C:/VST3/Keys.vst3"_s, u"Keys"_s, u"Maker"_s));
        QVERIFY(found.banner.endsWith(u"VB_Artwork.png"_s));
    }

    void arturiaBannerAndIcon()
    {
        // Layout copied from C:/ProgramData/Arturia/Piano V2 on this machine.
        writePng(m_root.filePath(u"Arturia/Piano V2/resources/images/banner_browser.png"_s));
        writePng(m_root.filePath(u"Arturia/Piano V2/resources/images/desktop-icon.png"_s));
        const OfficialArtwork artwork(sources());
        const PluginArtwork found = artwork.find(plugin(u"C:/VST3/Arturia/Piano V2.vst3"_s, u"Piano V2"_s, u"Arturia"_s));
        QVERIFY(found.banner.endsWith(u"banner_browser.png"_s));
        QVERIFY(found.icon.endsWith(u"desktop-icon.png"_s));
        QCOMPARE(found.source, u"Arturia"_s);

        // The same folder name from another vendor is not Arturia's artwork.
        const PluginArtwork other = artwork.find(plugin(u"C:/VST3/Piano V2.vst3"_s, u"Piano V2"_s, u"Someone"_s));
        QVERIFY(other.isEmpty());
    }

    void nothingOfficialMeansEmpty()
    {
        const OfficialArtwork artwork(sources());
        QVERIFY(artwork.find(plugin(u"C:/VST3/Serum2.vst3"_s, u"Serum 2"_s, u"Xfer Records"_s)).isEmpty());
    }

    void realMachineArtwork()
    {
        // The real sources on this machine (skips elsewhere).
        const OfficialArtwork artwork(OfficialArtwork::defaultSources());
        const QString piano = u"C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"_s;
        if (!QFileInfo::exists(piano)) QSKIP("Arturia Piano V2 not installed");
        const PluginArtwork arturia = artwork.find(plugin(piano, u"Piano V2"_s, u"Arturia"_s));
        QVERIFY2(QFileInfo::exists(arturia.banner), qPrintable(arturia.banner));

        const QString bbc = u"C:/Program Files/Common Files/VST3/BBC Symphony Orchestra (64 Bit).vst3"_s;
        if (!QFileInfo::exists(bbc)) QSKIP("BBC Symphony Orchestra not installed");
        const PluginArtwork nks = artwork.find(plugin(bbc, u"BBC Symphony Orchestra"_s, u"Spitfire Audio"_s));
        QVERIFY2(QFileInfo::exists(nks.banner), qPrintable(u"no NKS artwork for BBC SO"_s));
    }
};

QTEST_GUILESS_MAIN(TestOfficialArtwork)
#include "tst_official_artwork.moc"
