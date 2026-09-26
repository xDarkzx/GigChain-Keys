#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gigchain::ui {

// Messages for the user, each with how much it matters. The window shows
// them in their level's colour for a while (longer for worse news), then
// they go by themselves; each is logged where it was raised. The same
// message again is counted, not repeated; only the latest few are kept.
class Notifications : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the document")
    // How many are shown: what the window's visibility follows (a view's own
    // count does not update while its window is hidden).
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Level { Info, Warning, Error };
    Q_ENUM(Level)

    enum Role { TextRole = Qt::UserRole + 1, LevelRole, IdRole, RepeatsRole };

    static constexpr int kMaxShown = 4;

    explicit Notifications(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void post(const QString& message, gigchain::ui::Notifications::Level severity);
    // Gone (its time is up, or closed); an id no longer shown is ignored.
    Q_INVOKABLE void dismiss(int id);
    // How long a message of this level stays on screen, in milliseconds.
    Q_INVOKABLE static int shownFor(gigchain::ui::Notifications::Level level);

    [[nodiscard]] QString text(int row) const;
    [[nodiscard]] Level level(int row) const;
    [[nodiscard]] int count() const { return rowCount(); }

signals:
    void countChanged();

private:
    struct Entry
    {
        int id = 0;
        QString text;
        Level level = Info;
        int repeats = 1;
    };

    std::vector<Entry> m_entries;
    int m_nextId = 1;
};

} // namespace gigchain::ui
