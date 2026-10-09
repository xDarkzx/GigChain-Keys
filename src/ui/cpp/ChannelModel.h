#pragma once

#include "OfficialArtwork.h"

#include "gigchain/core/Model.h"
#include "gigchain/engine/EngineTypes.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <QHash>

#include <vector>

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

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
        EffectBypassedRole, // list of bools, parallel to effectNames
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
        PanRole,
        IconRole,  // the maker's own icon (file URL) when installed, else a category icon (qrc URL)
        OfficialIconRole, // true when IconRole is the maker's own icon
        ColorRole, // the strip's colour tag, "#rrggbb"
        VelocityLowRole,
        VelocityHighRole,
        InputLeftRole,  // 1-based audio input the channel plays, 0 = none (an instrument channel)
        InputRightRole, // 0 = mono
        MappingCountRole, // keyboard knobs mapped to its plugins' parameters
        OutputPairRole,   // 0 the mix; n = the interface's outputs 2n+1-2n+2
        AuxSendDbRole,    // how much goes to the aux effects (kMinVolumeDb = none)
    };
    Q_ENUM(Role)

    ChannelModel(const DocumentController& document, engine::IEngine& engine, const OfficialArtwork& artwork,
                 QObject* parent = nullptr);

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
    QHash<QString, engine::PluginInfo> m_plugins; // by plugin id, for icons
    QHash<QString, QString> m_officialIcons;      // by plugin id: image URL of the plugin's own icon
};

} // namespace gigchain::ui
