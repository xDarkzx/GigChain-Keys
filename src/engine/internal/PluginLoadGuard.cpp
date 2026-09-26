#include "PluginLoadGuard.h"

#include "EngineLog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

// Windows paths: one plugin, whatever the case or slashes.
QString normalised(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

constexpr qint64 kMaxListBytes = qint64{1024} * 1024;

} // namespace

PluginLoadGuard::PluginLoadGuard(QString folder) : m_folder(std::move(folder))
{
    if (!m_folder.isEmpty()) readBlocked();
}

PluginLoadGuard::Loading::~Loading()
{
    if (m_marker.isEmpty() || !QFileInfo::exists(m_marker)) return;
    if (!QFile::remove(m_marker)) {
        // Left behind, the next start would block a plugin that loaded fine.
        qCWarning(lcEngine).noquote() << "Could not remove the plugin loading marker" << m_marker;
    }
}

QString PluginLoadGuard::markerFor(const QString& pluginPath) const
{
    // The file name is a hash (paths have characters file names cannot);
    // the file holds the plugin's path.
    const QByteArray hash = QCryptographicHash::hash(normalised(pluginPath).toUtf8(), QCryptographicHash::Sha1).toHex();
    return m_folder + u"/loading/"_s + QString::fromLatin1(hash) + u".loading"_s;
}

PluginLoadGuard::Loading PluginLoadGuard::loading(const QString& pluginPath) const
{
    if (m_folder.isEmpty()) return Loading({});
    const QString marker = markerFor(pluginPath);
    if (!QDir().mkpath(QFileInfo(marker).path())) {
        qCWarning(lcEngine).noquote() << "Could not create the plugin loading guard folder" << QFileInfo(marker).path();
        return Loading({});
    }
    QFile file(marker);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(pluginPath.toUtf8()) < 0) {
        qCWarning(lcEngine).noquote() << "Could not write the plugin loading marker" << marker << ":" << file.errorString();
        return Loading({});
    }
    file.close(); // on disk before the plugin's code runs
    return Loading(marker);
}

QStringList PluginLoadGuard::takeCrashed()
{
    QStringList crashed;
    if (m_folder.isEmpty()) return crashed;
    const QDir markers(m_folder + u"/loading"_s);
    const QFileInfoList files = markers.entryInfoList({u"*.loading"_s}, QDir::Files);
    for (const QFileInfo& info : files) {
        QFile file(info.filePath());
        if (file.open(QIODevice::ReadOnly)) {
            const QString path = QString::fromUtf8(file.read(4096)).trimmed();
            file.close();
            if (!path.isEmpty() && !crashed.contains(path)) crashed << path;
        } else {
            qCWarning(lcEngine).noquote() << "Could not read the plugin loading marker" << info.filePath();
        }
        if (!QFile::remove(info.filePath())) {
            qCWarning(lcEngine).noquote() << "Could not remove the plugin loading marker" << info.filePath();
        }
    }
    for (const QString& path : crashed) {
        qCWarning(lcEngine).noquote() << "Plugin crashed the app while loading last time; switched off:" << path;
        if (!isBlocked(path)) m_blocked << path;
    }
    if (!crashed.isEmpty()) writeBlocked();
    return crashed;
}

bool PluginLoadGuard::isBlocked(const QString& pluginPath) const
{
    const QString wanted = normalised(pluginPath);
    for (const QString& path : m_blocked) {
        if (normalised(path) == wanted) return true;
    }
    return false;
}

void PluginLoadGuard::unblock(const QString& pluginPath)
{
    const QString wanted = normalised(pluginPath);
    const auto removed = m_blocked.removeIf([&](const QString& path) { return normalised(path) == wanted; });
    if (removed == 0) return;
    qCInfo(lcEngine).noquote() << "Plugin switched back on (it crashed the app before):" << pluginPath;
    writeBlocked();
}

void PluginLoadGuard::readBlocked()
{
    QFile file(m_folder + u"/blocked.json"_s);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcEngine).noquote() << "Could not read the blocked plugins list" << file.fileName() << ":"
                                      << file.errorString();
        return;
    }
    const QByteArray bytes = file.read(kMaxListBytes + 1);
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (bytes.size() > kMaxListBytes || error.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcEngine).noquote() << "The blocked plugins list" << file.fileName()
                                      << "is damaged; no plugin is blocked";
        return;
    }
    const QJsonArray entries = doc.array();
    for (const auto& value : entries) {
        if (value.isString() && !value.toString().isEmpty()) m_blocked << value.toString();
    }
}

void PluginLoadGuard::writeBlocked() const
{
    if (!QDir().mkpath(m_folder)) {
        qCWarning(lcEngine).noquote() << "Could not create the plugin guard folder" << m_folder;
        return;
    }
    QSaveFile out(m_folder + u"/blocked.json"_s);
    if (!out.open(QIODevice::WriteOnly)
        || out.write(QJsonDocument(QJsonArray::fromStringList(m_blocked)).toJson()) < 0 || !out.commit()) {
        qCWarning(lcEngine).noquote() << "Could not save the blocked plugins list" << out.fileName() << ":"
                                      << out.errorString();
    }
}

} // namespace gigchain::engine
