#include "Session.h"

using namespace Qt::StringLiterals;

namespace gigchain::ui {

Session::Session(engine::IEngine& engine, QSettings& settings)
    : m_artwork(engine.pluginFolder()),
      m_document(engine, settings),
      m_setlistModel(m_document),
      m_channelModel(m_document, engine, m_artwork),
      m_selectedChannel(m_document),
      m_pluginModel(engine, m_artwork, &settings),
      m_engineStatus(engine, m_document),
      m_loops(engine, m_document, settings),
      m_editorService(engine, m_document),
      m_effectWindows(engine, m_document),
      m_masterBus(engine, m_document, settings, m_effectWindows),
      m_settingsController(engine, m_document, settings),
      m_practice(engine, m_document),
      m_warmup(engine, m_practice, settings)
{
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_channelModel, &ChannelModel::refreshLevels);
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_settingsController, &SettingsController::pollLearning);
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_loops, &LoopController::poll);
    // The rig's master effects load with the app (behind the splash).
    m_masterBus.load();
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_masterBus, [this, &engine] {
        if (engine.takeMasterEdits()) m_masterBus.noteEdited();
    });
    // An audio input from the last session: may the app hear it (the Mac asks)?
    m_settingsController.checkInputPermission();
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
        {u"loops"_s, QVariant::fromValue(&m_loops)},
        {u"editorService"_s, QVariant::fromValue(&m_editorService)},
        {u"effectWindows"_s, QVariant::fromValue(&m_effectWindows)},
        {u"masterBus"_s, QVariant::fromValue(&m_masterBus)},
        {u"settings"_s, QVariant::fromValue(&m_settingsController)},
        {u"loading"_s, QVariant::fromValue(&m_loading)},
        {u"practice"_s, QVariant::fromValue(&m_practice)},
        {u"warmup"_s, QVariant::fromValue(&m_warmup)},
    };
}

} // namespace gigchain::ui
