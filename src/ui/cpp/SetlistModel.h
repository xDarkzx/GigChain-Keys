#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace openstage::ui {

class DocumentController;

// The side panel's setlist: each song row followed by its patch rows.
class SetlistModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

public:
    enum Role
    {
        KindRole = Qt::UserRole + 1, // "song" or "patch"
        NameRole,
        SongIndexRole,
        PatchIndexRole, // -1 on song rows
        NumberRole,     // 1-based song or patch number
        IsCurrentRole,
        IsCurrentSongRole,
    };
    Q_ENUM(Role)

    explicit SetlistModel(const DocumentController& document, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    struct Row
    {
        int song;
        int patch; // -1 for a song row
    };

    void rebuild();
    void refreshCurrent();

    const DocumentController& m_document;
    std::vector<Row> m_rows;
};

} // namespace openstage::ui
