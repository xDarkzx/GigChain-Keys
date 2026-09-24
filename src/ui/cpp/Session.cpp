#include "Session.h"

using namespace Qt::StringLiterals;

namespace openstage::ui {

Session::Session(engine::IEngine& engine, QSettings& settings)
    : m_document(engine, settings),
      m_setlistModel(m_document),
      m_channelModel(m_document, engine),
      m_selectedChannel(m_document),
      m_pluginModel(engine),
      m_engineStatus(engine, m_document),
      m_editorService(engine, m_document)
{
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_channelModel, &ChannelModel::refreshLevels);
}

QVariantMap Session::initialProperties()
{
    return {
        {u"doc"_s, QVariant::fromValue(&m_document)},
        {u"setlistModel"_s, QVariant::fromValue(&m_setlistModel)},
        {u"channelModel"_s, QVariant::fromValue(&m_channelModel)},
        {u"selectedChannel"_s, QVariant::fromValue(&m_selectedChannel)},
        {u"pluginModel"_s, QVariant::fromValue(&m_pluginModel)},
        {u"engineStatus"_s, QVariant::fromValue(&m_engineStatus)},
        {u"editorService"_s, QVariant::fromValue(&m_editorService)},
    };
}

} // namespace openstage::ui
