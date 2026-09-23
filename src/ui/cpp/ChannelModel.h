#pragma once

#include "openstage/core/Model.h"
#include "openstage/engine/EngineTypes.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace openstage::engine {
class IEngine;
}

namespace openstage::ui {

class DocumentController;

// The mixer: one row per channel of the current patch, plus live meters.
class ChannelModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

public:
    enum Role
    {
        ChannelIdRole = Qt::UserRole + 1,
        NameRole,
        InstrumentNameRole,
        EffectNamesRole,
        VolumeDbRole,
        MuteRole,
        SoloRole,
        KeyLowRole,
        KeyHighRole,
        TransposeRole,
        MidiChannelRole,
        PeakRole,
        RmsRole,
        SelectedRole,
    };
    Q_ENUM(Role)

    ChannelModel(const DocumentController& document, engine::IEngine& engine, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

public slots:
    // Reads the engine's meters for every row (the UI calls this ~30 Hz).
    void refreshLevels();

private:
    void reset();
    [[nodiscard]] const core::Channel* channelAt(int row) const;

    const DocumentController& m_document;
    engine::IEngine& m_engine;
    int m_rowCount = 0;
    std::vector<engine::LevelReading> m_levels;
};

} // namespace openstage::ui
