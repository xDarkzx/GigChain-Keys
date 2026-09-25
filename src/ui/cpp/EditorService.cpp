#include "EditorService.h"

#include "DocumentController.h"

#include "gigchain/engine/IEngine.h"

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
    auto editor = m_engine.createEditor(patch->channels[static_cast<std::size_t>(index)].id);
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
    if (patch == nullptr || patch->channels.empty()) return tr("Drag an instrument here to start this patch");
    if (index < 0 || static_cast<std::size_t>(index) >= patch->channels.size()) return tr("Select a channel in the mixer");
    const core::Channel& channel = patch->channels[static_cast<std::size_t>(index)];
    if (!channel.instrument) return tr("This channel has no instrument");
    return tr("%1 has no editor to show").arg(channel.instrument->displayName);
}

} // namespace gigchain::ui
