#include "MasterBus.h"

#include "DocumentController.h"
#include "EffectWindows.h"

#include "gigchain/core/Limits.h"
#include "gigchain/core/Checks.h"
#include "gigchain/engine/IEngine.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QVariantMap>

#include <exception>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

const QString kEffectsKey = u"master/effects"_s;

} // namespace

MasterBus::MasterBus(engine::IEngine& engine, DocumentController& document, QSettings& settings,
                     EffectWindows& windows, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document), m_settings(settings), m_windows(windows)
{
    // Closing a master effect's window is when its new settings are kept.
    connect(&m_windows, &EffectWindows::masterWindowClosed, this, [this] {
        if (m_edited) save();
    });
}

MasterBus::~MasterBus()
{
    // The app is quitting: keep what was changed. A destructor must not
    // throw (the app would be ended on the spot): a failure is logged.
    try {
        if (m_edited) save();
    } catch (const std::exception& e) {
        qCWarning(lcUi).noquote() << "The master effects could not be saved on quitting:" << e.what();
    } catch (...) {
        qCWarning(lcUi) << "The master effects could not be saved on quitting (unknown error)";
    }
}

QStringList MasterBus::effectNames() const
{
    QStringList names;
    for (const core::PluginSlot& slot : m_effects) names << slot.displayName;
    return names;
}

QVariantList MasterBus::effectBypassed() const
{
    QVariantList bypassed;
    for (const core::PluginSlot& slot : m_effects) bypassed << slot.bypass;
    return bypassed;
}

void MasterBus::load()
{
    GC_ONLY_MAIN_THREAD();
    m_effects.clear();
    const QVariantList saved = m_settings.value(kEffectsKey).toList();
    for (const QVariant& item : saved) {
        const QVariantMap map = item.toMap();
        core::PluginSlot slot{map.value(u"pluginId"_s).toString(), map.value(u"displayName"_s).toString(),
                              map.value(u"bypass"_s).toBool(), map.value(u"state"_s).toByteArray()};
        // Settings are a file too: anything unusable is skipped and said.
        if (slot.pluginId.isEmpty() || slot.pluginId.size() > core::limits::kMaxPluginIdLength
            || slot.displayName.size() > core::limits::kMaxNameLength
            || slot.state.size() > core::limits::kMaxPluginStateBytes
            || static_cast<int>(m_effects.size()) >= core::limits::kMaxEffectsPerChannel) {
            qCWarning(lcUi).noquote() << "Skipped an unusable master effect in the settings:" << slot.displayName;
            m_document.reportMessage(tr("A saved master effect could not be used and was left out (see the log)"),
                                     Notifications::Warning);
            continue;
        }
        m_effects.push_back(std::move(slot));
    }
    m_engine.setMasterEffects(m_effects);
    m_edited = false;
    qCInfo(lcUi) << "Master effects:" << effectNames();
    emit effectsChanged();
}

bool MasterBus::validIndex(int effect) const
{
    if (effect >= 0 && static_cast<std::size_t>(effect) < m_effects.size()) return true;
    // A menu used after the list changed: nothing to do, but not silently.
    qCWarning(lcUi) << "Ignored: master effect" << effect << "does not exist (" << m_effects.size() << "effects)";
    return false;
}

bool MasterBus::addEffect(const QString& pluginId, const QString& name)
{
    GC_ONLY_MAIN_THREAD();
    if (pluginId.isEmpty()) {
        qCWarning(lcUi) << "Ignored: a master effect with no plugin";
        return false;
    }
    if (static_cast<int>(m_effects.size()) >= core::limits::kMaxEffectsPerChannel) {
        m_document.reportMessage(tr("The master already has %1 effects").arg(core::limits::kMaxEffectsPerChannel),
                                 Notifications::Warning);
        return false;
    }
    m_effects.push_back(core::PluginSlot{pluginId, name, false, {}});
    commit();
    return true;
}

bool MasterBus::removeEffect(int effect)
{
    GC_ONLY_MAIN_THREAD();
    if (!validIndex(effect)) return false;
    (void)m_engine.storeMasterEffectStates(m_effects); // the others keep their current settings
    m_effects.erase(m_effects.begin() + effect);
    commit();
    return true;
}

bool MasterBus::replaceEffect(int effect, const QString& pluginId, const QString& name)
{
    GC_ONLY_MAIN_THREAD();
    if (!validIndex(effect)) return false;
    if (pluginId.isEmpty()) {
        qCWarning(lcUi) << "Ignored: replacing a master effect with no plugin";
        return false;
    }
    (void)m_engine.storeMasterEffectStates(m_effects);
    m_effects[static_cast<std::size_t>(effect)] = core::PluginSlot{pluginId, name, false, {}};
    commit();
    return true;
}

bool MasterBus::setEffectBypass(int effect, bool bypass)
{
    GC_ONLY_MAIN_THREAD();
    if (!validIndex(effect)) return false;
    // Switching off unloads it: keep its settings for when it comes back.
    (void)m_engine.storeMasterEffectStates(m_effects);
    m_effects[static_cast<std::size_t>(effect)].bypass = bypass;
    commit();
    return true;
}

bool MasterBus::openEffect(int effect, QWindow* owner)
{
    GC_ONLY_MAIN_THREAD();
    return m_windows.openMaster(effect, m_effects, owner);
}

void MasterBus::commit()
{
    m_engine.setMasterEffects(m_effects);
    m_windows.sweepMaster(m_effects);
    save();
    emit effectsChanged();
}

void MasterBus::save()
{
    GC_ONLY_MAIN_THREAD();
    for (const QString& problem : m_engine.storeMasterEffectStates(m_effects)) {
        m_document.reportMessage(problem, Notifications::Warning);
    }
    QVariantList list;
    for (const core::PluginSlot& slot : m_effects) {
        list << QVariantMap{{u"pluginId"_s, slot.pluginId},
                            {u"displayName"_s, slot.displayName},
                            {u"bypass"_s, slot.bypass},
                            {u"state"_s, slot.state}};
    }
    m_settings.setValue(kEffectsKey, list);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        const QString problem = tr("The master effects could not be saved (%1)").arg(m_settings.fileName());
        qCWarning(lcUi).noquote() << problem;
        m_document.reportMessage(problem);
        return;
    }
    m_edited = false;
}

} // namespace gigchain::ui
