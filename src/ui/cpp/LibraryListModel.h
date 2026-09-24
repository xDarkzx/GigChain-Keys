#pragma once

#include "KontaktLibraries.h"

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QSettings;

namespace openstage::ui {

// The Libraries tab: Kontakt libraries found in the user's library folders,
// each with its official banner (see KontaktLibraries.h). Folders are kept in
// settings; scanning runs in the background so the UI never waits for it.
class LibraryListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)

public:
    enum Role
    {
        NameRole = Qt::UserRole + 1,
        CompanyRole,
        BannerUrlRole, // file: URL, empty when the library has no banner
        FolderRole,
    };
    Q_ENUM(Role)

    // Starts a scan of the remembered folders right away.
    LibraryListModel(QSettings& settings, QString cacheDir, QObject* parent = nullptr);
    ~LibraryListModel() override;
    LibraryListModel(const LibraryListModel&) = delete;
    LibraryListModel& operator=(const LibraryListModel&) = delete;
    LibraryListModel(LibraryListModel&&) = delete;
    LibraryListModel& operator=(LibraryListModel&&) = delete;

    static QString defaultCacheDir();

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QStringList folders() const { return m_folders; }
    [[nodiscard]] bool isScanning() const { return m_watcher.isRunning(); }

    Q_INVOKABLE void addFolder(const QUrl& folder);
    Q_INVOKABLE void removeFolder(const QString& folder);
    Q_INVOKABLE void rescan();

signals:
    void foldersChanged();
    void scanningChanged();
    void scanFinished();

private:
    void saveFolders();
    void onScanned();

    QSettings& m_settings;
    QString m_cacheDir;
    QStringList m_folders;
    std::vector<KontaktLibrary> m_libraries;
    QFutureWatcher<std::vector<KontaktLibrary>> m_watcher;
    bool m_rescanPending = false;
};

} // namespace openstage::ui
