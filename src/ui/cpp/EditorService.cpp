#include "EditorService.h"

#include "DocumentController.h"

#include "gigchain/engine/BuiltinReadings.h"
#include "gigchain/engine/IEngine.h"
#include "gigchain/engine/SidechainCurves.h"

#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace gigchain::ui {

EditorService::EditorService(engine::IEngine& engine, DocumentController& document, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document)
{
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(0);
    connect(&m_coalesce, &QTimer::timeout, this, &EditorService::targetChanged);
    const auto schedule = [this] { m_coalesce.start(); };
    connect(&m_document, &DocumentController::channelsChanged, this, schedule);
    connect(&m_document, &DocumentController::selectedChannelChanged, this, schedule);
    // Not channelUpdated: volume, pan, mute, solo, name, key range and effects
    // never change which instrument plugin (and so which editor) is shown.
}

core::Result<std::unique_ptr<engine::IPluginEditor>> EditorService::createForSelection()
{
    const core::Patch* patch = m_document.currentPatch();
    const int index = m_document.selectedChannel();
    if (patch == nullptr || index < 0 || static_cast<std::size_t>(index) >= patch->channels.size()) {
        return std::unique_ptr<engine::IPluginEditor>();
    }
    auto editor = m_engine.createEditor(patch->channels.at(static_cast<std::size_t>(index)).id);
    if (!editor) m_document.reportMessage(editor.error().message);
    return editor;
}

void EditorService::reportFailure(const QString& message)
{
    m_document.reportMessage(message);
}

QString EditorService::emptyReason() const
{
    const core::Patch* patch = m_document.currentPatch();
    const int index = m_document.selectedChannel();
    if (patch == nullptr || patch->channels.empty()) return tr("Drag an instrument here to start this sound");
    if (index < 0 || static_cast<std::size_t>(index) >= patch->channels.size()) return tr("Select a channel in the mixer");
    const core::Channel& channel = patch->channels.at(static_cast<std::size_t>(index));
    if (!channel.instrument) return tr("This channel has no instrument");
    return tr("%1 has no editor to show").arg(channel.instrument->displayName);
}

const core::Channel* EditorService::builtinChannel() const
{
    const core::Patch* patch = m_document.currentPatch();
    const int index = m_document.selectedChannel();
    if (patch == nullptr || index < 0 || static_cast<std::size_t>(index) >= patch->channels.size()) return nullptr;
    const core::Channel& channel = patch->channels.at(static_cast<std::size_t>(index));
    const bool builtin = channel.instrument && channel.instrument->pluginId.startsWith(u"builtin:");
    return builtin ? &channel : nullptr;
}

bool EditorService::builtinSelected() const
{
    return builtinChannel() != nullptr;
}

QString EditorService::builtinName() const
{
    const core::Channel* channel = builtinChannel();
    return channel != nullptr && channel->instrument ? channel->instrument->displayName : QString();
}

QVariantList EditorService::builtinParameters() const
{
    QVariantList list;
    const core::Channel* channel = builtinChannel();
    if (channel == nullptr) return list;
    for (const engine::PluginParameter& p : m_engine.pluginParameters(channel->id, -1)) {
        list.append(QVariantMap{{u"id"_s, static_cast<int>(p.id)}, {u"name"_s, p.name}, {u"choices"_s, p.choices}});
    }
    return list;
}

double EditorService::builtinValue(int parameter) const
{
    const core::Channel* channel = builtinChannel();
    if (channel == nullptr || parameter < 0) return -1.0;
    return m_engine.pluginParameterValue(channel->id, -1, static_cast<uint32_t>(parameter));
}

void EditorService::setBuiltinValue(int parameter, double value)
{
    const core::Channel* channel = builtinChannel();
    if (channel == nullptr || parameter < 0) return;
    // (The engine logs the cause.)
    if (auto set = m_engine.setPluginParameter(channel->id, -1, static_cast<uint32_t>(parameter), value); !set) {
        m_document.reportMessage(set.error().message);
    }
}

QVariantList EditorService::sidechainCurve(int curve, double slope, int points) const
{
    QVariantList levels;
    const int count = std::clamp(points, 2, 1024);
    for (int i = 0; i < count; ++i) levels.append(engine::sidechain::curveGain(curve, i / static_cast<double>(count - 1), slope));
    return levels;
}

double EditorService::sidechainPosition() const
{
    return builtinValue(static_cast<int>(engine::sidechain::kPositionParameter));
}

double EditorService::builtinReading(const QString& name) const
{
    if (name == u"pianoRecordings"_s) return builtinValue(static_cast<int>(engine::readings::kPianoRecordings));
    if (name == u"pianoSource"_s) return builtinValue(static_cast<int>(engine::readings::kPianoSource));
    return -1.0;
}

QString EditorService::builtinId() const
{
    const core::Channel* channel = builtinChannel();
    return channel != nullptr && channel->instrument ? channel->instrument->pluginId : QString();
}

} // namespace gigchain::ui
