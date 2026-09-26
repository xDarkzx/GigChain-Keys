#include "OfficialArtwork.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QUrl>

#include <algorithm>
#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

// The VST3 bundle's folder icon (Windows): the name the VST3 format gives it.
const QString& kFolderIcon()
{
    static const QString name = u"PlugIn.ico"_s;
    return name;
}

// The first existing file among `names` in `folder`, matched without regard
// to case (makers differ: PlugIn.ico vs Plugin.ico). Hidden and system files
// count: installers hide a folder's icon.
QString firstExisting(const QString& folder, const QStringList& names)
{
    const QDir dir(folder);
    if (!dir.exists()) return {};
    const QStringList files = dir.entryList(QDir::Files | QDir::Hidden | QDir::System);
    for (const QString& wanted : names) {
        const auto found = std::ranges::find_if(files, [&wanted](const QString& file) {
            return file.compare(wanted, Qt::CaseInsensitive) == 0;
        });
        if (found != files.end()) return dir.filePath(*found);
    }
    return {};
}

} // namespace

OfficialArtwork::OfficialArtwork(const QString& pluginFolder)
    : m_pluginFolder(pluginFolder.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(pluginFolder).absoluteFilePath()))
{
}

PluginArtwork OfficialArtwork::find(const engine::PluginInfo& plugin) const
{
    return PluginArtwork{.banner = snapshot(plugin), .icon = icon(plugin)};
}

QString OfficialArtwork::snapshot(const engine::PluginInfo& plugin)
{
    if (plugin.classId.isEmpty()) return {};
    const QString folder = plugin.id + u"/Contents/Resources/Snapshots"_s;
    return firstExisting(folder, {plugin.classId + u"_snapshot_2.0x.png"_s, plugin.classId + u"_snapshot.png"_s});
}

QString OfficialArtwork::icon(const engine::PluginInfo& plugin) const
{
    const QFileInfo bundle(plugin.id);
    if (bundle.isDir()) {
        if (QString own = firstExisting(bundle.absoluteFilePath(), {kFolderIcon()}); !own.isEmpty()) return own;
    }
    if (m_pluginFolder.isEmpty()) return {};
    // The folders around the plugin, nearest first, up to the plugin folder
    // (never the plugin folder itself: an icon there belongs to no plugin).
    const QString inside = m_pluginFolder.endsWith(u'/') ? m_pluginFolder : m_pluginFolder + u'/';
    for (QString dir = QDir::cleanPath(bundle.absolutePath()); dir.startsWith(inside, Qt::CaseInsensitive);
         dir = QFileInfo(dir).absolutePath()) {
        if (QString shared = firstExisting(dir, {kFolderIcon()}); !shared.isEmpty()) return shared;
    }
    return {};
}

void PluginIconProvider::install(QQmlEngine& engine)
{
    // The engine takes ownership of the provider (QQmlEngine::addImageProvider).
    engine.addImageProvider(QLatin1StringView(kName), std::make_unique<PluginIconProvider>().release());
}

QString PluginIconProvider::url(const QString& iconPath)
{
    if (iconPath.isEmpty()) return {};
    return u"image://"_s + QLatin1StringView(kName) + u'/' + QString::fromLatin1(QUrl::toPercentEncoding(iconPath));
}

QImage PluginIconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString path = QUrl::fromPercentEncoding(id.toLatin1());
    QImageReader reader(path);
    QImage largest;
    // imageCount() is 0 for a single image; the loop still reads that one.
    const int count = std::max(reader.imageCount(), 1);
    for (int i = 0; i < count; ++i) {
        if (i > 0 && !reader.jumpToImage(i)) break;
        QImage image = reader.read();
        if (!image.isNull() && image.width() * image.height() > largest.width() * largest.height()) {
            largest = std::move(image);
        }
    }
    if (largest.isNull()) {
        qCWarning(lcUi).noquote() << "Cannot read the plugin icon" << path << ":" << reader.errorString();
        return {};
    }
    // Only when both sides are asked for (0 means "any"); QML scales the rest.
    if (!requestedSize.isEmpty() && (largest.width() > requestedSize.width() || largest.height() > requestedSize.height())) {
        largest = largest.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size != nullptr) *size = largest.size();
    return largest;
}

} // namespace gigchain::ui
