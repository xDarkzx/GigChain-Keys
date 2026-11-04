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
      m_panel(engine, m_document),
      m_editorService(engine, m_document),
      m_instrumentWindows(engine, m_document, m_editorService, settings),
      m_effectWindows(engine, m_document),
      m_masterBus(engine, m_document, settings, m_effectWindows, EffectWindows::Bus::Master),
      m_auxBus(engine, m_document, settings, m_effectWindows, EffectWindows::Bus::Aux),
      m_padsBus(engine, m_document, settings, m_effectWindows, EffectWindows::Bus::SustainPadsFx),
      m_mixerLayout(settings),
      m_sustainPads(engine, m_document, settings, m_effectWindows),
      m_settingsController(engine, m_document, settings),
      m_practice(engine, m_document),
      m_warmup(engine, m_practice, settings)
{
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_channelModel, &ChannelModel::refreshLevels);
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_settingsController, &SettingsController::pollLearning);
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_loops, &LoopController::poll);
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_panel, &PanelController::poll);
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_sustainPads, &SustainPadsController::poll);
    // Panic: every sound means the pad too (switched off here, silenced at once by the engine).
    QObject::connect(&m_engineStatus, &EngineStatus::panicking, &m_sustainPads, [this] { m_sustainPads.setActive(false); });
    // The rig's master effects load with the app (behind the splash).
    m_masterBus.load();
    m_auxBus.load();
    m_padsBus.load();
    QObject::connect(&m_engineStatus, &EngineStatus::polled, &m_masterBus, [this, &engine] {
        if (engine.takeMasterEdits()) m_masterBus.noteEdited();
        if (engine.takeAuxEdits()) m_auxBus.noteEdited();
        if (engine.takeSustainPadsEffectEdits()) m_padsBus.noteEdited();
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
        {u"panel"_s, QVariant::fromValue(&m_panel)},
        {u"editorService"_s, QVariant::fromValue(&m_editorService)},
        {u"instrumentWindows"_s, QVariant::fromValue(&m_instrumentWindows)},
        {u"effectWindows"_s, QVariant::fromValue(&m_effectWindows)},
        {u"masterBus"_s, QVariant::fromValue(&m_masterBus)},
        {u"auxBus"_s, QVariant::fromValue(&m_auxBus)},
        {u"padsBus"_s, QVariant::fromValue(&m_padsBus)},
        {u"mixerLayout"_s, QVariant::fromValue(&m_mixerLayout)},
        {u"sustainPads"_s, QVariant::fromValue(&m_sustainPads)},
        {u"settings"_s, QVariant::fromValue(&m_settingsController)},
        {u"loading"_s, QVariant::fromValue(&m_loading)},
        {u"practice"_s, QVariant::fromValue(&m_practice)},
        {u"warmup"_s, QVariant::fromValue(&m_warmup)},
    };
}

} // namespace gigchain::ui
