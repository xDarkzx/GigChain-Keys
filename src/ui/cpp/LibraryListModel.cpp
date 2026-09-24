#include "LibraryListModel.h"

#include <QDir>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace openstage::ui {
namespace {

constexpr auto kFoldersKey = "libraries/folders"_L1;

} // namespace

LibraryListModel::LibraryListModel(QSettings& settings, QString cacheDir, QObject* parent)
    : QAbstractListModel(parent),
      m_settings(settings),
      m_cacheDir(std::move(cacheDir)),
      m_folders(settings.value(kFoldersKey).toStringList())
{
    connect(&m_watcher, &QFutureWatcher<std::vector<KontaktLibrary>>::finished, this, &LibraryListModel::onScanned);
    rescan();
}

LibraryListModel::~LibraryListModel()
{
    m_watcher.waitForFinished(); // the scan reads only files; it finishes promptly
}

QString LibraryListModel::defaultCacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/library-banners"_s;
}

int LibraryListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_libraries.size());
}

QVariant LibraryListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) return {};
    const KontaktLibrary& library = m_libraries[static_cast<std::size_t>(index.row())];
    switch (role) {
    case NameRole: return library.name;
    case CompanyRole: return library.company;
    case BannerUrlRole: return library.bannerPath.isEmpty() ? QString() : QUrl::fromLocalFile(library.bannerPath).toString();
    case FolderRole: return library.folder;
    default: return {};
    }
}

QHash<int, QByteArray> LibraryListModel::roleNames() const
{
    return {{NameRole, "name"}, {CompanyRole, "company"}, {BannerUrlRole, "bannerUrl"}, {FolderRole, "folder"}};
}

void LibraryListModel::addFolder(const QUrl& folder)
{
    const QString path = QDir::cleanPath(folder.toLocalFile());
    if (path.isEmpty() || m_folders.contains(path, Qt::CaseInsensitive)) return;
    if (!QDir(path).exists()) {
        qCWarning(lcUi).noquote() << "Library folder not found:" << path;
        return;
    }
    m_folders << path;
    saveFolders();
    rescan();
}

void LibraryListModel::removeFolder(const QString& folder)
{
    if (m_folders.removeAll(folder) == 0) return;
    saveFolders();
    rescan();
}

void LibraryListModel::rescan()
{
    if (m_watcher.isRunning()) {
        m_rescanPending = true; // run again when the current scan ends
        return;
    }
    const QStringList folders = m_folders;
    const QString cacheDir = m_cacheDir;
    m_watcher.setFuture(QtConcurrent::run([folders, cacheDir] { return scanKontaktLibraries(folders, cacheDir); }));
    emit scanningChanged();
}

void LibraryListModel::saveFolders()
{
    m_settings.setValue(kFoldersKey, m_folders);
    emit foldersChanged();
}

void LibraryListModel::onScanned()
{
    beginResetModel();
    m_libraries = m_watcher.result();
    endResetModel();
    emit scanningChanged();
    if (m_rescanPending) {
        m_rescanPending = false;
        rescan();
        return;
    }
    emit scanFinished();
}

} // namespace openstage::ui
