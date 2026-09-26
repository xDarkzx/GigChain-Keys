#include "SetlistModel.h"

#include "DocumentController.h"

using namespace Qt::StringLiterals;

namespace gigchain::ui {

SetlistModel::SetlistModel(const DocumentController& document, QObject* parent)
    : QAbstractListModel(parent), m_document(document)
{
    connect(&m_document, &DocumentController::structureChanged, this, &SetlistModel::rebuild);
    connect(&m_document, &DocumentController::currentChanged, this, &SetlistModel::refreshCurrent);
    rebuild();
}

int SetlistModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant SetlistModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) return {};
    const Row& row = m_rows.at(static_cast<std::size_t>(index.row()));
    const auto& songs = m_document.setlist().songs;
    if (row.song < 0 || static_cast<std::size_t>(row.song) >= songs.size()) return {};
    const auto& song = songs.at(static_cast<std::size_t>(row.song));
    const bool isSong = row.patch < 0;
    if (!isSong && static_cast<std::size_t>(row.patch) >= song.patches.size()) return {};
    const core::Cursor cursor = m_document.cursor();

    switch (role) {
    case KindRole: return isSong ? u"song"_s : u"patch"_s;
    case NameRole: return isSong ? song.name : song.patches.at(static_cast<std::size_t>(row.patch)).name;
    case SongIndexRole: return row.song;
    case PatchIndexRole: return row.patch;
    case NumberRole: return (isSong ? row.song : row.patch) + 1;
    case IsCurrentRole: return !isSong && cursor.song == row.song && cursor.patch == row.patch;
    case IsCurrentSongRole: return cursor.song == row.song;
    default: return {};
    }
}

QHash<int, QByteArray> SetlistModel::roleNames() const
{
    return {
        {KindRole, "kind"},           {NameRole, "name"},     {SongIndexRole, "songIndex"},
        {PatchIndexRole, "patchIndex"}, {NumberRole, "number"}, {IsCurrentRole, "isCurrent"},
        {IsCurrentSongRole, "isCurrentSong"},
    };
}

void SetlistModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    const auto& songs = m_document.setlist().songs;
    for (std::size_t s = 0; s < songs.size(); ++s) {
        m_rows.push_back(Row{.song = static_cast<int>(s), .patch = -1});
        for (std::size_t p = 0; p < songs.at(s).patches.size(); ++p) {
            m_rows.push_back(Row{.song = static_cast<int>(s), .patch = static_cast<int>(p)});
        }
    }
    endResetModel();
}

void SetlistModel::refreshCurrent()
{
    if (m_rows.empty()) return;
    emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1), {IsCurrentRole, IsCurrentSongRole});
}

} // namespace gigchain::ui
