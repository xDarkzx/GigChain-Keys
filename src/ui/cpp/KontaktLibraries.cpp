#include "KontaktLibraries.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLoggingCategory>
#include <QXmlStreamReader>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace openstage::ui {
namespace {

constexpr qint64 kMaxNicntBytes = 64LL * 1024 * 1024; // real ones are ~1-2 MB
constexpr int kMaxDepth = 4;
const QByteArray kPngSignature("\x89PNG\r\n\x1a\n", 8);

core::Result<QByteArray> readNicnt(const QString& path)
{
    QFile file(path);
    if (file.size() > kMaxNicntBytes) {
        return core::fail(core::ErrorCode::FileTooLarge, u"%1 is too large to be a library file"_s.arg(path));
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return core::fail(core::ErrorCode::FileReadFailed, u"Cannot read %1: %2"_s.arg(path, file.errorString()));
    }
    return file.readAll();
}

// Each embedded PNG runs from its signature to the end of its IEND chunk.
std::vector<QByteArray> embeddedPngs(const QByteArray& data)
{
    std::vector<QByteArray> pngs;
    qsizetype pos = 0;
    while (true) {
        const qsizetype start = data.indexOf(kPngSignature, pos);
        if (start < 0) break;
        const qsizetype iend = data.indexOf("IEND", start);
        if (iend < 0) break;
        const qsizetype end = iend + 8; // "IEND" + 4-byte CRC
        pngs.push_back(data.mid(start, end - start));
        pos = end;
    }
    return pngs;
}

void findNicntFiles(const QString& folder, int depth, QStringList& found)
{
    if (depth > kMaxDepth) return;
    const QDir dir(folder);
    for (const QFileInfo& file : dir.entryInfoList({u"*.nicnt"_s}, QDir::Files)) found << file.absoluteFilePath();
    for (const QFileInfo& sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        findNicntFiles(sub.absoluteFilePath(), depth + 1, found);
    }
}

QString cachedBannerPath(const QString& cacheDir, const QFileInfo& nicnt)
{
    const QString stamp = u"%1|%2|%3"_s.arg(nicnt.absoluteFilePath())
                              .arg(nicnt.lastModified().toMSecsSinceEpoch())
                              .arg(nicnt.size());
    return cacheDir + u'/' +
           QString::fromLatin1(QCryptographicHash::hash(stamp.toUtf8(), QCryptographicHash::Sha1).toHex()) +
           u".png"_s;
}

} // namespace

NiProductHints parseProductHints(const QByteArray& xmlText, const QString& origin)
{
    NiProductHints product;
    QXmlStreamReader xml(xmlText);
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
            break; // the first Product is the one described
        }
    }
    if (xml.hasError() && product.name.isEmpty()) {
        qCWarning(lcUi).noquote() << "Malformed NI product information in" << origin << ":" << xml.errorString();
    }
    return product;
}

core::Result<QByteArray> extractNicntBanner(const QString& nicntPath)
{
    auto data = readNicnt(nicntPath);
    if (!data) {
        qCWarning(lcUi).noquote() << data.error().message;
        return tl::unexpected(data.error());
    }
    // The Kontakt banner is the wide one (about 6:1 to 9:1); the others are
    // NKS thumbnails and logos. Take the first wide image, else the widest.
    QByteArray best;
    int bestWidth = 0;
    for (const QByteArray& png : embeddedPngs(*data)) {
        const QImage image = QImage::fromData(png, "PNG");
        if (image.isNull()) continue;
        if (image.width() >= 4 * image.height()) return png;
        if (image.width() > bestWidth) {
            bestWidth = image.width();
            best = png;
        }
    }
    if (best.isEmpty()) {
        const QString message = u"%1 has no library banner"_s.arg(nicntPath);
        qCWarning(lcUi).noquote() << message;
        return core::fail(core::ErrorCode::InvalidData, message);
    }
    return best;
}

NiProductHints readNicntProduct(const QString& nicntPath)
{
    auto data = readNicnt(nicntPath);
    if (!data) {
        qCWarning(lcUi).noquote() << data.error().message;
        return {};
    }
    const qsizetype start = data->indexOf("<?xml");
    const qsizetype end = data->indexOf("</ProductHints>", start);
    if (start < 0 || end < 0) return {};
    return parseProductHints(data->mid(start, end + 15 - start), nicntPath);
}

std::vector<KontaktLibrary> scanKontaktLibraries(const QStringList& folders, const QString& cacheDir)
{
    QStringList nicntFiles;
    for (const QString& folder : folders) {
        if (!QFileInfo(folder).isDir()) {
            qCWarning(lcUi).noquote() << "Library folder not found:" << folder;
            continue;
        }
        findNicntFiles(folder, 0, nicntFiles);
    }
    if (!nicntFiles.isEmpty() && !QDir().mkpath(cacheDir)) {
        qCWarning(lcUi).noquote() << "Cannot create banner cache" << cacheDir;
    }

    std::vector<KontaktLibrary> libraries;
    for (const QString& path : nicntFiles) {
        const QFileInfo info(path);
        const NiProductHints hints = readNicntProduct(path);
        KontaktLibrary library;
        library.name = hints.name.isEmpty() ? info.completeBaseName() : hints.name;
        library.company = hints.company;
        library.folder = info.absolutePath();
        library.nicntPath = path;

        // Kontakt uses a wallpaper.png beside the .nicnt when there is one.
        const QString wallpaper = info.absolutePath() + u"/wallpaper.png"_s;
        if (QFileInfo::exists(wallpaper)) {
            library.bannerPath = wallpaper;
        } else {
            const QString cached = cachedBannerPath(cacheDir, info);
            if (QFileInfo::exists(cached)) {
                library.bannerPath = cached;
            } else if (auto banner = extractNicntBanner(path)) {
                QFile out(cached);
                if (out.open(QIODevice::WriteOnly) && out.write(*banner) == banner->size()) {
                    library.bannerPath = cached;
                } else {
                    qCWarning(lcUi).noquote() << "Cannot write banner" << cached << ":" << out.errorString();
                }
            }
        }
        libraries.push_back(std::move(library));
    }
    std::sort(libraries.begin(), libraries.end(), [](const KontaktLibrary& a, const KontaktLibrary& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    qCInfo(lcUi) << "Found" << libraries.size() << "Kontakt libraries";
    return libraries;
}

} // namespace openstage::ui
