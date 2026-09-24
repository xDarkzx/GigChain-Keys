#pragma once

#include "OfficialArtwork.h"

#include "openstage/engine/EngineTypes.h"

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace openstage::engine {
class IEngine;
}

namespace openstage::ui {

// The plugin browser: instruments first, then effects, each sorted by name,
// filtered by a search text that matches name or vendor.
class PluginListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

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
        ImageUrlRole, // file: URL of the plugin's official banner, or empty
        LogoUrlRole,  // file: URL of its official logo, or empty
    };
    Q_ENUM(Role)

    PluginListModel(const engine::IEngine& engine, const OfficialArtwork& artwork, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);

    // Every effect as {pluginId, name}, for the mixer's "+" menu.
    Q_INVOKABLE QVariantList effects() const;
    // Every instrument as {pluginId, name}, for the mixer's "+ Instrument" menu.
    Q_INVOKABLE QVariantList instruments() const;
    // The first instrument whose name contains `text` as {pluginId, name}, or
    // an empty map (e.g. to find Kontakt for a library).
    Q_INVOKABLE QVariantMap findInstrument(const QString& text) const;

signals:
    void filterTextChanged();

private:
    void applyFilter();

    std::vector<engine::PluginInfo> m_all;
    std::vector<PluginArtwork> m_art; // parallel to m_all
    std::vector<std::size_t> m_visible;
    QString m_filterText;
};

} // namespace openstage::ui
