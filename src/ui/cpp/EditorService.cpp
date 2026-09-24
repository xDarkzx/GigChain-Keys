#include "EditorService.h"

#include "DocumentController.h"

#include "openstage/engine/IEngine.h"

namespace openstage::ui {

EditorService::EditorService(engine::IEngine& engine, DocumentController& document, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document)
{
    connect(&m_document, &DocumentController::channelsChanged, this, &EditorService::targetChanged);
    connect(&m_document, &DocumentController::selectedChannelChanged, this, &EditorService::targetChanged);
    connect(&m_document, &DocumentController::channelUpdated, this, [this](int channel) {
        if (channel == m_document.selectedChannel()) emit targetChanged(); // e.g. a key-range change re-applied the patch
    });
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

} // namespace openstage::ui
