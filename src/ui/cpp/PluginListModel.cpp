#include "PluginListModel.h"

#include "PluginIcons.h"

#include "openstage/engine/IEngine.h"

#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace openstage::ui {

PluginListModel::PluginListModel(const engine::IEngine& engine, const ArtworkCache& artwork, QObject* parent)
    : QAbstractListModel(parent), m_artwork(artwork), m_all(engine.availablePlugins())
{
    connect(&m_artwork, &ArtworkCache::artworkChanged, this, [this](const QString& pluginId) {
        for (std::size_t row = 0; row < m_visible.size(); ++row) {
            if (m_all[m_visible[row]].id != pluginId) continue;
            const QModelIndex changed = index(static_cast<int>(row));
            emit dataChanged(changed, changed, {ImageUrlRole});
        }
    });
    std::stable_sort(m_all.begin(), m_all.end(), [](const engine::PluginInfo& a, const engine::PluginInfo& b) {
        if (a.kind != b.kind) return a.kind == engine::PluginKind::Instrument;
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    applyFilter();
}

int PluginListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

QVariant PluginListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) return {};
    const engine::PluginInfo& plugin = m_all[m_visible[static_cast<std::size_t>(index.row())]];
    switch (role) {
    case PluginIdRole: return plugin.id;
    case NameRole: return plugin.name;
    case VendorRole: return plugin.vendor;
    case KindRole: return plugin.kind == engine::PluginKind::Instrument ? u"instrument"_s : u"effect"_s;
    case VersionRole: return plugin.version;
    case CategoryRole: {
        const QStringList parts = plugin.subCategories.split(u'|', Qt::SkipEmptyParts);
        return parts.size() > 1 ? parts.last() : QString();
    }
    case IconRole: return iconUrl(iconFor(plugin));
    case ImageUrlRole: return m_artwork.urlFor(plugin.id);
    default: return {};
    }
}

QHash<int, QByteArray> PluginListModel::roleNames() const
{
    return {{PluginIdRole, "pluginId"}, {NameRole, "name"},       {VendorRole, "vendor"},
            {KindRole, "kind"},         {VersionRole, "version"}, {CategoryRole, "category"},
            {IconRole, "icon"},         {ImageUrlRole, "imageUrl"}};
}

void PluginListModel::setFilterText(const QString& text)
{
    if (text == m_filterText) return;
    m_filterText = text;
    applyFilter();
    emit filterTextChanged();
}

namespace {

QVariantList pluginsOfKind(const std::vector<engine::PluginInfo>& plugins, engine::PluginKind kind)
{
    QVariantList list;
    for (const auto& plugin : plugins) {
        if (plugin.kind != kind) continue;
        list.append(QVariantMap{{u"pluginId"_s, plugin.id}, {u"name"_s, plugin.name}});
    }
    return list;
}

} // namespace

QVariantList PluginListModel::effects() const
{
    return pluginsOfKind(m_all, engine::PluginKind::Effect);
}

QVariantList PluginListModel::instruments() const
{
    return pluginsOfKind(m_all, engine::PluginKind::Instrument);
}

void PluginListModel::applyFilter()
{
    beginResetModel();
    m_visible.clear();
    const QString needle = m_filterText.trimmed();
    for (std::size_t i = 0; i < m_all.size(); ++i) {
        const auto& plugin = m_all[i];
        if (needle.isEmpty() || plugin.name.contains(needle, Qt::CaseInsensitive) ||
            plugin.vendor.contains(needle, Qt::CaseInsensitive)) {
            m_visible.push_back(i);
        }
    }
    endResetModel();
}

} // namespace openstage::ui
