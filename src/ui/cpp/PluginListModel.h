#pragma once

#include "OfficialArtwork.h"

#include "gigchain/engine/EngineTypes.h"

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QSettings;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

// The plugin browser: instruments first, then effects, each sorted by name,
// filtered by a search text that matches name or vendor.
class PluginListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    // Show only instruments (the browser list); effects stay available through effects().
    Q_PROPERTY(bool instrumentsOnly READ instrumentsOnly WRITE setInstrumentsOnly NOTIFY instrumentsOnlyChanged)
    // The picker menus (see effectMenu()/instrumentMenu()); they change when plugins are hidden or shown.
    Q_PROPERTY(QVariantMap effectMenu READ effectMenu NOTIFY menusChanged)
    Q_PROPERTY(QVariantMap instrumentMenu READ instrumentMenu NOTIFY menusChanged)

public:
    enum Role
    {
        PluginIdRole = Qt::UserRole + 1,
        NameRole,
        VendorRole,
        KindRole,     // "instrument" or "effect"
        VersionRole,
        CategoryRole, // the most specific VST3 sub-category, e.g. "Piano"
        IconRole,     // qrc URL
        ImageUrlRole, // file: URL of the plugin's own art (installed by its maker), or empty
        FavoriteRole, // starred by the user: listed first
        RatingRole,   // the user's 1-5 stars, 0 = not rated
        WebsiteRole,  // the maker's website, from the plugin
        EmailRole,    // the maker's support address, from the plugin
        SdkVersionRole,
        TagsRole,     // every VST3 sub-category, e.g. ["Instrument", "Piano"]
        LocationRole, // the installed plugin file
        SizeRole,     // "12.4 MB", or empty when the file cannot be read
        InstalledRole // the file's date, "2026-03-14", or empty
    };
    Q_ENUM(Role)

    // `settings` (optional) remembers plugins the user hid from the list.
    PluginListModel(const engine::IEngine& engine, const OfficialArtwork& artwork, QSettings* settings = nullptr,
                    QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);
    [[nodiscard]] bool instrumentsOnly() const { return m_instrumentsOnly; }
    void setInstrumentsOnly(bool only);

    // Every effect as {pluginId, name}, for the mixer's "+" menu.
    Q_INVOKABLE QVariantList effects() const;
    // Effects grouped like Logic's plug-in menu:
    // {categories: [{title, plugins: [{pluginId, name}]}], vendors: [...same]}.
    // Category = the VST3 sub-category (EQ, Dynamics, Reverb...), else "Other".
    [[nodiscard]] QVariantMap effectMenu() const;
    // Hidden plugins are left out of both.
    // Instruments grouped by maker: {vendors: [{title, plugins: [{pluginId, name}]}]}.
    [[nodiscard]] QVariantMap instrumentMenu() const;
    // Every instrument as {pluginId, name}, for the mixer's "+ Instrument" menu.
    Q_INVOKABLE QVariantList instruments() const;
    // The first instrument whose name contains `text` as {pluginId, name}, or
    // an empty map (e.g. to find Kontakt for a library).
    Q_INVOKABLE QVariantMap findInstrument(const QString& text) const;

    // Favourites are listed first; ratings are 0 (none) to 5. Both remembered.
    Q_INVOKABLE void setFavorite(const QString& pluginId, bool favorite);
    Q_INVOKABLE void setRating(const QString& pluginId, int stars);

    // Hide a plugin from the browser list and the pickers (remembered); showAll() undoes every hide.
    Q_INVOKABLE void hide(const QString& pluginId);
    Q_INVOKABLE void showAll();

signals:
    void filterTextChanged();
    void instrumentsOnlyChanged();
    void menusChanged();

private:
    void applyFilter();

    std::vector<engine::PluginInfo> m_all;
    std::vector<QString> m_images; // parallel to m_all: file URL of the maker's art, or empty
    std::vector<std::size_t> m_visible;
    QSettings* m_settings = nullptr; // not owned
    QStringList m_hidden;
    QStringList m_favorites;
    QVariantMap m_ratings; // plugin id -> stars
    QString m_filterText;
    bool m_instrumentsOnly = false;
};

} // namespace gigchain::ui
