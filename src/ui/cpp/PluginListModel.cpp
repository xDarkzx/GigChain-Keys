#include "PluginListModel.h"

#include "PluginIcons.h"

#include "gigchain/engine/IEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QProcess>
#include <QSettings>
#include <QUrl>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <map>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {

PluginListModel::PluginListModel(const engine::IEngine& engine, const OfficialArtwork& artwork, QSettings* settings,
                                 QObject* parent)
    : QAbstractListModel(parent), m_all(engine.availablePlugins()), m_settings(settings)
{
    if (m_settings != nullptr) {
        m_hidden = m_settings->value(u"plugins/hidden"_s).toStringList();
        m_favorites = m_settings->value(u"plugins/favorites"_s).toStringList();
        m_ratings = m_settings->value(u"plugins/ratings"_s).toMap();
    }
    std::ranges::stable_sort(m_all, [](const engine::PluginInfo& a, const engine::PluginInfo& b) {
        if (a.kind != b.kind) return a.kind == engine::PluginKind::Instrument;
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    m_images.reserve(m_all.size());
    m_icons.reserve(m_all.size());
    for (const auto& plugin : m_all) {
        const PluginArtwork art = artwork.find(plugin); // reads installed files only
        m_images.push_back(art.banner.isEmpty() ? QString() : QUrl::fromLocalFile(art.banner).toString());
        m_icons.push_back(PluginIconProvider::url(art.icon));
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
    const std::size_t at = m_visible.at(static_cast<std::size_t>(index.row()));
    const engine::PluginInfo& plugin = m_all.at(at);
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
    case IconRole: return m_icons.at(at).isEmpty() ? iconUrl(iconFor(plugin)) : m_icons.at(at);
    case OfficialIconRole: return !m_icons.at(at).isEmpty();
    case ImageUrlRole: return m_images.at(at);
    case FavoriteRole: return m_favorites.contains(plugin.id);
    case RatingRole: return m_ratings.value(plugin.id, 0).toInt();
    case WebsiteRole: {
        // Offered as a link (opened by Windows): only a web address, never a
        // file or a program a plugin names as its "website".
        const QUrl url(plugin.website, QUrl::StrictMode);
        const bool web = url.isValid() && (url.scheme() == u"https"_s || url.scheme() == u"http"_s) && !url.host().isEmpty();
        return web ? plugin.website : QString();
    }
    case EmailRole: return plugin.email;
    case SdkVersionRole: return plugin.sdkVersion;
    case TagsRole: return plugin.subCategories.split(u'|', Qt::SkipEmptyParts);
    case LocationRole: return QDir::toNativeSeparators(plugin.id);
    case SizeRole: {
        const QFileInfo file(plugin.id);
        return file.isFile() ? QLocale::system().formattedDataSize(file.size()) : QString();
    }
    case InstalledRole: {
        const QFileInfo file(plugin.id);
        return file.exists() ? file.lastModified().date().toString(Qt::ISODate) : QString();
    }
    default: return {};
    }
}

QHash<int, QByteArray> PluginListModel::roleNames() const
{
    return {{PluginIdRole, "pluginId"},     {NameRole, "name"},         {VendorRole, "vendor"},
            {KindRole, "kind"},             {VersionRole, "version"},   {CategoryRole, "category"},
            {IconRole, "icon"},             {ImageUrlRole, "imageUrl"}, {FavoriteRole, "favorite"},
            {RatingRole, "rating"},         {WebsiteRole, "website"},   {EmailRole, "email"},
            {SdkVersionRole, "sdkVersion"}, {TagsRole, "tags"},         {LocationRole, "location"},
            {SizeRole, "size"},             {InstalledRole, "installed"}, {OfficialIconRole, "officialIcon"}};
}

void PluginListModel::setFavorite(const QString& pluginId, bool favorite)
{
    if (m_favorites.contains(pluginId) == favorite) return;
    if (favorite) m_favorites << pluginId;
    else m_favorites.removeAll(pluginId);
    if (m_settings != nullptr) m_settings->setValue(u"plugins/favorites"_s, m_favorites);
    applyFilter(); // favourites move to the top
}

QString PluginListModel::showInFolder(const QString& pluginId) const
{
    const auto plugin = std::ranges::find_if(m_all, [&](const auto& p) { return p.id == pluginId; });
    if (plugin == m_all.end() || !QFileInfo::exists(pluginId)) {
        qCWarning(lcUi).noquote() << "Show in folder: no plugin" << pluginId;
        return tr("No installed plugin %1").arg(pluginId);
    }
    const QString path = QDir::toNativeSeparators(pluginId);
    if (!QProcess::startDetached(u"explorer.exe"_s, {u"/select,"_s + path})) {
        qCWarning(lcUi).noquote() << "Show in folder: could not start Explorer for" << path;
        return tr("Could not open Explorer for %1").arg(path);
    }
    return {};
}

void PluginListModel::setRating(const QString& pluginId, int stars)
{
    stars = std::clamp(stars, 0, 5);
    if (m_ratings.value(pluginId, 0).toInt() == stars) return;
    if (stars == 0) m_ratings.remove(pluginId);
    else m_ratings.insert(pluginId, stars);
    if (m_settings != nullptr) m_settings->setValue(u"plugins/ratings"_s, m_ratings);
    for (std::size_t row = 0; row < m_visible.size(); ++row) {
        if (m_all.at(m_visible.at(row)).id != pluginId) continue;
        const QModelIndex at = index(static_cast<int>(row));
        emit dataChanged(at, at, {RatingRole});
    }
}

void PluginListModel::hide(const QString& pluginId)
{
    if (m_hidden.contains(pluginId)) return;
    m_hidden << pluginId;
    if (m_settings != nullptr) m_settings->setValue(u"plugins/hidden"_s, m_hidden);
    applyFilter();
    emit menusChanged();
}

void PluginListModel::showAll()
{
    if (m_hidden.isEmpty()) return;
    m_hidden.clear();
    if (m_settings != nullptr) m_settings->remove(u"plugins/hidden"_s);
    applyFilter();
    emit menusChanged();
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

namespace {

using Groups = std::map<QString, QVariantList>;

// Groups as a list of {title, plugins}, sorted by title, "Other" last.
QVariantList groupList(const Groups& groups)
{
    QVariantList list;
    QVariantMap other;
    for (const auto& [title, plugins] : groups) {
        const QVariantMap group{{u"title"_s, title}, {u"plugins"_s, plugins}};
        if (title == u"Other"_s) other = group;
        else list.append(group);
    }
    if (!other.isEmpty()) list.append(other);
    return list;
}

QVariantMap menuEntry(const engine::PluginInfo& plugin)
{
    return QVariantMap{{u"pluginId"_s, plugin.id}, {u"name"_s, plugin.name}};
}

QString vendorOf(const engine::PluginInfo& plugin)
{
    return plugin.vendor.isEmpty() ? u"Other"_s : plugin.vendor;
}

} // namespace

QVariantMap PluginListModel::effectMenu() const
{
    Groups byCategory;
    Groups byVendor;
    for (const auto& plugin : m_all) {
        if (plugin.kind != engine::PluginKind::Effect || m_hidden.contains(plugin.id)) continue;
        const QStringList parts = plugin.subCategories.split(u'|', Qt::SkipEmptyParts);
        byCategory[parts.size() > 1 ? parts.at(1) : u"Other"_s].append(menuEntry(plugin));
        byVendor[vendorOf(plugin)].append(menuEntry(plugin));
    }
    return QVariantMap{{u"categories"_s, groupList(byCategory)}, {u"vendors"_s, groupList(byVendor)}};
}

QVariantMap PluginListModel::instrumentMenu() const
{
    Groups byVendor;
    for (const auto& plugin : m_all) {
        if (plugin.kind == engine::PluginKind::Instrument && !m_hidden.contains(plugin.id)) byVendor[vendorOf(plugin)].append(menuEntry(plugin));
    }
    return QVariantMap{{u"vendors"_s, groupList(byVendor)}};
}

QVariantList PluginListModel::instruments() const
{
    return pluginsOfKind(m_all, engine::PluginKind::Instrument);
}

QVariantMap PluginListModel::findInstrument(const QString& text) const
{
    const auto found = std::ranges::find_if(m_all, [&text](const auto& plugin) {
        return plugin.kind == engine::PluginKind::Instrument && plugin.name.contains(text, Qt::CaseInsensitive);
    });
    if (found == m_all.end()) return {};
    return QVariantMap{{u"pluginId"_s, found->id}, {u"name"_s, found->name}};
}

void PluginListModel::applyFilter()
{
    beginResetModel();
    m_visible.clear();
    const QString needle = m_filterText.trimmed();
    for (std::size_t i = 0; i < m_all.size(); ++i) {
        const auto& plugin = m_all.at(i);
        if (m_instrumentsOnly && plugin.kind != engine::PluginKind::Instrument) continue;
        if (m_hidden.contains(plugin.id)) continue;
        if (needle.isEmpty() || plugin.name.contains(needle, Qt::CaseInsensitive) ||
            plugin.vendor.contains(needle, Qt::CaseInsensitive)) {
            m_visible.push_back(i);
        }
    }
    // Favourites first, each group keeping its order.
    std::ranges::stable_partition(m_visible, [this](std::size_t i) { return m_favorites.contains(m_all.at(i).id); });
    endResetModel();
}

} // namespace gigchain::ui
