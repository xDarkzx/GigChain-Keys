#include "OfficialArtwork.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>
#include <QXmlStreamReader>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace openstage::ui {
namespace {

// The first existing file among `names` in `folder`, matched without regard
// to case (vendors differ: MST_Artwork.png vs MST_artwork.png).
QString firstExisting(const QString& folder, const QStringList& names)
{
    const QDir dir(folder);
    if (!dir.exists()) return {};
    const QStringList files = dir.entryList(QDir::Files);
    for (const QString& wanted : names) {
        for (const QString& file : files) {
            if (file.compare(wanted, Qt::CaseInsensitive) == 0) return dir.filePath(file);
        }
    }
    return {};
}

// Reads <ProductHints><Product><Name/><Company/><RegKey/><BinName/> files.
struct ParsedProduct
{
    QString name;
    QString company;
    QString regKey;
    QString binName;
};

ParsedProduct parseServiceCenterXml(const QString& path)
{
    ParsedProduct product;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcUi).noquote() << "Cannot read" << path << ":" << file.errorString();
        return product;
    }
    QXmlStreamReader xml(&file);
    // Only the Product's own Name/Company/RegKey/BinName are read; nested
    // blocks (FactoryLibrary, Relevance...) are skipped whole.
    if (xml.readNextStartElement() && xml.name() == "ProductHints"_L1) {
        while (xml.readNextStartElement()) {
            if (xml.name() != "Product"_L1) {
                xml.skipCurrentElement();
                continue;
            }
            while (xml.readNextStartElement()) {
                const auto element = xml.name();
                if (element == "Name"_L1) product.name = xml.readElementText();
                else if (element == "Company"_L1) product.company = xml.readElementText();
                else if (element == "RegKey"_L1) product.regKey = xml.readElementText();
                else if (element == "BinName"_L1) product.binName = xml.readElementText();
                else xml.skipCurrentElement();
            }
        }
    }
    if (xml.hasError()) {
        qCWarning(lcUi).noquote() << "Malformed NI product file" << path << ":" << xml.errorString();
    }
    return product;
}

} // namespace

OfficialArtwork::Sources OfficialArtwork::defaultSources()
{
    Sources sources;
    sources.arturiaRoot = u"C:/ProgramData/Arturia"_s;
    sources.niServiceCenter = u"C:/Program Files/Common Files/Native Instruments/Service Center"_s;
    sources.niResources = u"C:/Users/Public/Documents/NI Resources"_s;
    sources.niContentDir = [](const QString& regKey) {
        const QSettings key(u"HKEY_LOCAL_MACHINE\\SOFTWARE\\Native Instruments\\"_s + regKey, QSettings::NativeFormat);
        return key.value(u"ContentDir"_s).toString();
    };
    return sources;
}

OfficialArtwork::OfficialArtwork(Sources sources) : m_sources(std::move(sources))
{
    const QDir serviceCenter(m_sources.niServiceCenter);
    if (!serviceCenter.exists()) return;
    for (const QString& file : serviceCenter.entryList({u"*.xml"_s}, QDir::Files)) {
        const ParsedProduct product = parseServiceCenterXml(serviceCenter.filePath(file));
        if (product.binName.isEmpty()) continue;
        m_niProducts.insert(product.binName.toLower(), NiProduct{product.name, product.company, product.regKey});
    }
}

PluginArtwork OfficialArtwork::find(const engine::PluginInfo& plugin) const
{
    if (PluginArtwork art = fromSnapshot(plugin); !art.isEmpty()) return art;
    if (PluginArtwork art = fromNks(plugin); !art.isEmpty()) return art;
    return fromArturia(plugin);
}

PluginArtwork OfficialArtwork::fromSnapshot(const engine::PluginInfo& plugin) const
{
    if (plugin.classId.isEmpty()) return {};
    const QString folder = plugin.id + u"/Contents/Resources/Snapshots"_s;
    const QString banner = firstExisting(folder, {plugin.classId + u"_snapshot_2.0x.png"_s,
                                                  plugin.classId + u"_snapshot.png"_s});
    if (banner.isEmpty()) return {};
    return PluginArtwork{banner, {}, {}, u"VST3 snapshot"_s};
}

PluginArtwork OfficialArtwork::fromNks(const engine::PluginInfo& plugin) const
{
    const QString binName = QFileInfo(plugin.id).completeBaseName().toLower();
    const auto it = m_niProducts.constFind(binName);
    if (it == m_niProducts.constEnd()) return {};
    const NiProduct& product = *it;

    QStringList folders;
    const QString contentDir = m_sources.niContentDir ? m_sources.niContentDir(product.regKey) : QString();
    if (!contentDir.isEmpty()) {
        folders << contentDir + u"/PAResources/image/"_s + product.company + u'/' + product.name;
    }
    folders << m_sources.niResources + u"/image/"_s + product.company + u'/' + product.name
            << m_sources.niResources + u"/image/"_s + product.company.toLower() + u'/' + product.name.toLower();

    for (const QString& folder : folders) {
        PluginArtwork art;
        art.banner = firstExisting(folder, {u"MST_Artwork.png"_s, u"VB_Artwork.png"_s});
        art.logo = firstExisting(folder, {u"MST_Logo.png"_s, u"OSO_Logo.png"_s, u"VB_Logo.png"_s});
        art.icon = firstExisting(folder, {u"MST_Plugin.png"_s});
        if (!art.isEmpty()) {
            art.source = u"NKS"_s;
            return art;
        }
    }
    return {};
}

PluginArtwork OfficialArtwork::fromArturia(const engine::PluginInfo& plugin) const
{
    if (!plugin.vendor.contains(u"Arturia"_s, Qt::CaseInsensitive)) return {};
    const QString folder = m_sources.arturiaRoot + u'/' + plugin.name + u"/resources/images"_s;
    PluginArtwork art;
    art.banner = firstExisting(folder, {u"banner_browser.png"_s});
    art.icon = firstExisting(folder, {u"desktop-icon.png"_s});
    if (art.isEmpty()) return {};
    art.source = u"Arturia"_s;
    return art;
}

} // namespace openstage::ui
