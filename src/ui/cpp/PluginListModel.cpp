#include "PluginListModel.h"

#include "PluginIcons.h"

#include "openstage/engine/IEngine.h"

#include <QUrl>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace openstage::ui {

PluginListModel::PluginListModel(const engine::IEngine& engine, const OfficialArtwork& artwork, QObject* parent)
    : QAbstractListModel(parent), m_all(engine.availablePlugins())
{
    std::stable_sort(m_all.begin(), m_all.end(), [](const engine::PluginInfo& a, const engine::PluginInfo& b) {
        if (a.kind != b.kind) return a.kind == engine::PluginKind::Instrument;
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    m_images.reserve(m_all.size());
    for (const auto& plugin : m_all) {
        const QString banner = artwork.find(plugin).banner; // reads installed files only
        m_images.push_back(banner.isEmpty() ? QString() : QUrl::fromLocalFile(banner).toString());
    }
    applyFilter();
}

int PluginListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

QVariant PluginListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) return {};
    const std::size_t at = m_visible[static_cast<std::size_t>(index.row())];
    const engine::PluginInfo& plugin = m_all[at];
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
    case ImageUrlRole: return m_images[at];
    default: return {};
    }
}

QHash<int, QByteArray> PluginListModel::roleNames() const
{
    return {{PluginIdRole, "pluginId"}, {NameRole, "name"},       {VendorRole, "vendor"},
            {KindRole, "kind"},         {VersionRole, "version"}, {CategoryRole, "category"},
            {IconRole, "icon"},         {ImageUrlRole, "imageUrl"}};
}

void PluginListModel::setInstrumentsOnly(bool only)
{
    if (only == m_instrumentsOnly) return;
    m_instrumentsOnly = only;
    applyFilter();
    emit instrumentsOnlyChanged();
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

QVariantMap PluginListModel::findInstrument(const QString& text) const
{
    for (const auto& plugin : m_all) {
        if (plugin.kind == engine::PluginKind::Instrument && plugin.name.contains(text, Qt::CaseInsensitive)) {
            return QVariantMap{{u"pluginId"_s, plugin.id}, {u"name"_s, plugin.name}};
        }
    }
    return {};
}

void PluginListModel::applyFilter()
{
    beginResetModel();
    m_visible.clear();
    const QString needle = m_filterText.trimmed();
    for (std::size_t i = 0; i < m_all.size(); ++i) {
        const auto& plugin = m_all[i];
        if (m_instrumentsOnly && plugin.kind != engine::PluginKind::Instrument) continue;
        if (needle.isEmpty() || plugin.name.contains(needle, Qt::CaseInsensitive) ||
            plugin.vendor.contains(needle, Qt::CaseInsensitive)) {
            m_visible.push_back(i);
        }
    }
    endResetModel();
}

} // namespace openstage::ui
