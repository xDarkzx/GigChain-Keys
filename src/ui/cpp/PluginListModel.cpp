#include "PluginListModel.h"

#include "openstage/engine/IEngine.h"

#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace openstage::ui {

PluginListModel::PluginListModel(const engine::IEngine& engine, QObject* parent)
    : QAbstractListModel(parent), m_all(engine.availablePlugins())
{
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
    default: return {};
    }
}

QHash<int, QByteArray> PluginListModel::roleNames() const
{
    return {{PluginIdRole, "pluginId"}, {NameRole, "name"}, {VendorRole, "vendor"}, {KindRole, "kind"}};
}

void PluginListModel::setFilterText(const QString& text)
{
    if (text == m_filterText) return;
    m_filterText = text;
    applyFilter();
    emit filterTextChanged();
}

QVariantList PluginListModel::effects() const
{
    QVariantList list;
    for (const auto& plugin : m_all) {
        if (plugin.kind != engine::PluginKind::Effect) continue;
        list.append(QVariantMap{{u"pluginId"_s, plugin.id}, {u"name"_s, plugin.name}});
    }
    return list;
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
