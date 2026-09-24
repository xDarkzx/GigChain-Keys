#include "ArtworkCache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUrl>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace openstage::ui {
namespace {

// The file that changes when a plugin is updated: the DLL inside a VST3
// bundle folder, or the file itself.
QFileInfo pluginBinary(const QString& pluginId)
{
    const QFileInfo info(pluginId);
    if (info.isDir()) {
        const QFileInfo inner(pluginId + u"/Contents/x86_64-win/"_s + info.fileName());
        if (inner.exists()) return inner;
    }
    return info;
}

} // namespace

ArtworkCache::ArtworkCache(QString folder, QObject* parent) : QObject(parent), m_folder(std::move(folder)) {}

QString ArtworkCache::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/artwork"_s;
}

QString ArtworkCache::pathFor(const QString& pluginId) const
{
    const QFileInfo binary = pluginBinary(pluginId);
    const QString stamp = u"%1|%2|%3"_s.arg(pluginId)
                              .arg(binary.lastModified().toMSecsSinceEpoch())
                              .arg(binary.size());
    const QByteArray hash = QCryptographicHash::hash(stamp.toUtf8(), QCryptographicHash::Sha1).toHex();
    return m_folder + u'/' + QString::fromLatin1(hash) + u".png"_s;
}

bool ArtworkCache::has(const QString& pluginId) const
{
    return QFileInfo::exists(pathFor(pluginId));
}

QString ArtworkCache::urlFor(const QString& pluginId) const
{
    const QString path = pathFor(pluginId);
    return QFileInfo::exists(path) ? QUrl::fromLocalFile(path).toString() : QString();
}

core::Result<void> ArtworkCache::store(const QString& pluginId, const QImage& image)
{
    const auto failed = [&pluginId](const QString& why) {
        const QString message = tr("Could not save the picture of %1: %2").arg(pluginId, why);
        qCWarning(lcUi).noquote() << message;
        return core::fail(core::ErrorCode::FileWriteFailed, message);
    };
    if (image.isNull()) return failed(tr("empty picture"));
    if (!QDir().mkpath(m_folder)) return failed(tr("cannot create %1").arg(m_folder));

    const QImage scaled = image.width() > kMaxWidth
                              ? image.scaledToWidth(kMaxWidth, Qt::SmoothTransformation)
                              : image;
    const QString path = pathFor(pluginId);
    if (!scaled.save(path, "PNG")) return failed(tr("cannot write %1").arg(path));
    qCInfo(lcUi).noquote() << "Saved artwork for" << pluginId;
    emit artworkChanged(pluginId);
    return {};
}

} // namespace openstage::ui
