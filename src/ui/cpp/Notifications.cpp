#include "Notifications.h"

#include "gigchain/core/Checks.h"

#include <algorithm>

namespace gigchain::ui {

Notifications::Notifications(QObject* parent) : QAbstractListModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &Notifications::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &Notifications::countChanged);
}

int Notifications::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant Notifications::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const Entry& entry = m_entries.at(static_cast<std::size_t>(index.row()));
    switch (role) {
    case TextRole: return entry.text;
    case LevelRole: return entry.level;
    case IdRole: return entry.id;
    case RepeatsRole: return entry.repeats;
    default: return {};
    }
}

QHash<int, QByteArray> Notifications::roleNames() const
{
    return {{TextRole, "text"}, {LevelRole, "level"}, {IdRole, "notificationId"}, {RepeatsRole, "repeats"}};
}

void Notifications::post(const QString& message, Level severity)
{
    GC_ONLY_MAIN_THREAD();
    if (message.isEmpty()) return;
    const auto same =
        std::ranges::find_if(m_entries, [&](const Entry& e) { return e.text == message && e.level == severity; });
    if (same != m_entries.end()) {
        ++same->repeats; // shown again: its time starts over
        const QModelIndex at = index(static_cast<int>(same - m_entries.begin()));
        emit dataChanged(at, at, {RepeatsRole});
        return;
    }
    if (static_cast<int>(m_entries.size()) >= kMaxShown) {
        beginRemoveRows({}, 0, 0); // the oldest makes room
        m_entries.erase(m_entries.begin());
        endRemoveRows();
    }
    const int row = rowCount();
    beginInsertRows({}, row, row);
    m_entries.push_back(Entry{.id = m_nextId++, .text = message, .level = severity, .repeats = 1});
    endInsertRows();
}

void Notifications::dismiss(int id)
{
    GC_ONLY_MAIN_THREAD();
    const auto it = std::ranges::find_if(m_entries, [id](const Entry& e) { return e.id == id; });
    if (it == m_entries.end()) return;
    const int row = static_cast<int>(it - m_entries.begin());
    beginRemoveRows({}, row, row);
    m_entries.erase(it);
    endRemoveRows();
}

int Notifications::shownFor(Level level)
{
    switch (level) {
    case Info: return 4000;
    case Warning: return 6000;
    case Error: return 8000;
    }
    return 8000;
}

QString Notifications::text(int row) const
{
    return data(index(row), TextRole).toString();
}

Notifications::Level Notifications::level(int row) const
{
    return static_cast<Level>(data(index(row), LevelRole).toInt());
}

} // namespace gigchain::ui
