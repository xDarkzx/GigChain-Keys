#include "ChannelModel.h"

#include "DocumentController.h"
#include "PluginIcons.h"

#include <QUrl>

#include <array>

#include "gigchain/engine/IEngine.h"

using namespace Qt::StringLiterals;

namespace gigchain::ui {

ChannelModel::ChannelModel(const DocumentController& document, engine::IEngine& engine,
                           const OfficialArtwork& artwork, QObject* parent)
    : QAbstractListModel(parent), m_document(document), m_engine(engine)
{
    for (const auto& plugin : m_engine.availablePlugins()) {
        m_plugins.insert(plugin.id, plugin);
        const PluginArtwork art = artwork.find(plugin);
        if (!art.icon.isEmpty()) m_officialIcons.insert(plugin.id, PluginIconProvider::url(art.icon));
    }
    connect(&m_document, &DocumentController::channelsChanged, this, &ChannelModel::reset);
    connect(&m_document, &DocumentController::channelUpdated, this, [this](int row) {
        if (row >= 0 && row < m_rowCount) emit dataChanged(index(row), index(row));
    });
    connect(&m_document, &DocumentController::selectedChannelChanged, this, [this] {
        if (m_rowCount > 0) emit dataChanged(index(0), index(m_rowCount - 1), {SelectedRole});
    });
    reset();
}

int ChannelModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rowCount;
}

const core::Channel* ChannelModel::channelAt(int row) const
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || row < 0 || row >= m_rowCount || static_cast<std::size_t>(row) >= patch->channels.size()) {
        return nullptr;
    }
    return &patch->channels.at(static_cast<std::size_t>(row));
}

QVariant ChannelModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) return {};
    const core::Channel* channel = channelAt(index.row());
    if (channel == nullptr) return {};
    const auto row = static_cast<std::size_t>(index.row());

    switch (role) {
    case ChannelIdRole: return channel->id.value();
    case NameRole: return channel->name;
    case InstrumentNameRole: return channel->instrument ? channel->instrument->displayName : QString();
    case EffectNamesRole: {
        QStringList names;
        for (const auto& effect : channel->effects) names << effect.displayName;
        return names;
    }
    case EffectBypassedRole: {
        QVariantList bypassed;
        for (const auto& effect : channel->effects) bypassed << effect.bypass;
        return bypassed;
    }
    case VolumeDbRole: return channel->volumeDb;
    case MuteRole: return channel->mute;
    case SoloRole: return channel->solo;
    case KeyLowRole: return channel->keyLow;
    case KeyHighRole: return channel->keyHigh;
    case TransposeRole: return channel->transpose;
    case MidiChannelRole: return channel->midiChannel;
    case VelocityLowRole: return channel->velocityLow;
    case VelocityHighRole: return channel->velocityHigh;
    case InputLeftRole: return channel->inputLeft;
    case InputRightRole: return channel->inputRight;
    case OutputPairRole: return channel->outputPair;
    case AuxSendDbRole: return channel->auxSendDb;
    case MappingCountRole: return static_cast<int>(channel->mappings.size());
    case PeakRole: return row < m_levels.size() ? m_levels.at(row).peak : 0.0F;
    case RmsRole: return row < m_levels.size() ? m_levels.at(row).rms : 0.0F;
    case SelectedRole: return index.row() == m_document.selectedChannel();
    case PanRole: return channel->pan;
    case IconRole: {
        if (!channel->instrument && channel->inputLeft > 0) return iconUrl(u"microphone"_s); // an audio input
        if (!channel->instrument) return iconUrl(u"plus"_s);
        if (const auto official = m_officialIcons.constFind(channel->instrument->pluginId);
            official != m_officialIcons.constEnd()) {
            return *official; // the maker's own icon
        }
        // Unknown ids (plugin removed since) still get an icon from the name.
        const engine::PluginInfo plugin = m_plugins.value(
            channel->instrument->pluginId,
            engine::PluginInfo{.id = channel->instrument->pluginId,
                               .name = channel->instrument->displayName,
                               .vendor = {},
                               .kind = engine::PluginKind::Instrument,
                               .subCategories = {},
                               .version = {},
                               .classId = {},
                               .website = {},
                               .email = {},
                               .sdkVersion = {}});
        return iconUrl(iconFor(plugin));
    }
    case OfficialIconRole: return channel->instrument && m_officialIcons.contains(channel->instrument->pluginId);
    case ColorRole: {
        static const std::array<const char*, 8> palette = {"#4a8fe7", "#45b36b", "#e0a526", "#e5484d",
                                                           "#9b6dff", "#2bb5c9", "#f07b3f", "#c96dd8"};
        return QString::fromLatin1(palette.at(row % palette.size()));
    }
    default: return {};
    }
}

QHash<int, QByteArray> ChannelModel::roleNames() const
{
    return {
        {ChannelIdRole, "channelId"}, {NameRole, "name"},          {InstrumentNameRole, "instrumentName"},
        {EffectNamesRole, "effectNames"}, {EffectBypassedRole, "effectBypassed"}, {VolumeDbRole, "volumeDb"}, {MuteRole, "mute"},
        {SoloRole, "solo"},           {KeyLowRole, "keyLow"},      {KeyHighRole, "keyHigh"},
        {TransposeRole, "transpose"}, {MidiChannelRole, "midiChannel"}, {PeakRole, "peak"},
        {RmsRole, "rms"},             {SelectedRole, "selected"},    {PanRole, "pan"},
        {IconRole, "icon"},           {ColorRole, "color"},          {OfficialIconRole, "officialIcon"},
        {VelocityLowRole, "velocityLow"}, {VelocityHighRole, "velocityHigh"}, {InputLeftRole, "inputLeft"},
        {InputRightRole, "inputRight"},   {MappingCountRole, "mappingCount"}, {OutputPairRole, "outputPair"},
        {AuxSendDbRole, "auxSendDb"},
    };
}

void ChannelModel::refreshLevels()
{
    if (m_rowCount == 0) return;
    for (int row = 0; row < m_rowCount; ++row) {
        const core::Channel* channel = channelAt(row);
        m_levels.at(static_cast<std::size_t>(row)) = channel != nullptr ? m_engine.channelLevel(channel->id)
                                                                        : engine::LevelReading{};
    }
    emit dataChanged(index(0), index(m_rowCount - 1), {PeakRole, RmsRole});
}

void ChannelModel::reset()
{
    beginResetModel();
    const core::Patch* patch = m_document.currentPatch();
    m_rowCount = patch != nullptr ? static_cast<int>(patch->channels.size()) : 0;
    m_levels.assign(static_cast<std::size_t>(m_rowCount), engine::LevelReading{});
    endResetModel();
}

} // namespace gigchain::ui
