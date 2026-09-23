#pragma once

#include "ChannelModel.h"
#include "DocumentController.h"
#include "EngineStatus.h"
#include "PluginListModel.h"
#include "SelectedChannel.h"
#include "SetlistModel.h"

#include <QVariantMap>

class QSettings;

namespace openstage::engine {
class IEngine;
}

namespace openstage::ui {

// Everything the QML UI binds to, created in dependency order and destroyed
// in reverse. The engine and settings must outlive the session; the QML
// engine must be destroyed before it.
class Session
{
public:
    Session(engine::IEngine& engine, QSettings& settings);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    ~Session() = default;

    // The required properties of Main.qml.
    [[nodiscard]] QVariantMap initialProperties();
    [[nodiscard]] DocumentController& document() { return m_document; }

private:
    DocumentController m_document;
    SetlistModel m_setlistModel;
    ChannelModel m_channelModel;
    SelectedChannel m_selectedChannel;
    PluginListModel m_pluginModel;
    EngineStatus m_engineStatus;
};

} // namespace openstage::ui
