#pragma once

#include "EffectWindows.h"

#include "gigchain/core/Model.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QSettings;
class QWindow;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;

// The master bus's effects (EQ, compressor, limiter plugins on everything).
// They belong to the rig, not to a setlist: kept in the app's settings with
// each plugin's own settings, loaded at start-up and whatever setlist is
// open. Saved when the list changes, when a master effect's window closes,
// and when the app quits. Problems go to the banner and the log.
class MasterBus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(QStringList effectNames READ effectNames NOTIFY effectsChanged)
    Q_PROPERTY(QVariantList effectBypassed READ effectBypassed NOTIFY effectsChanged)

public:
    // `bus`: the master's (on everything) or the aux's (fed by the channels' sends).
    MasterBus(engine::IEngine& engine, DocumentController& document, QSettings& settings, EffectWindows& windows,
              EffectWindows::Bus bus = EffectWindows::Bus::Master, QObject* parent = nullptr);
    ~MasterBus() override;
    MasterBus(const MasterBus&) = delete;
    MasterBus& operator=(const MasterBus&) = delete;
    MasterBus(MasterBus&&) = delete;
    MasterBus& operator=(MasterBus&&) = delete;

    [[nodiscard]] const std::vector<core::PluginSlot>& effects() const { return m_effects; }
    [[nodiscard]] QStringList effectNames() const;
    [[nodiscard]] QVariantList effectBypassed() const;

    // Reads the saved master effects and loads them (at start-up).
    void load();

    Q_INVOKABLE bool addEffect(const QString& pluginId, const QString& name);
    Q_INVOKABLE bool removeEffect(int effect);
    Q_INVOKABLE bool replaceEffect(int effect, const QString& pluginId, const QString& name);
    Q_INVOKABLE bool setEffectBypass(int effect, bool bypass);
    Q_INVOKABLE bool openEffect(int effect, QWindow* owner);

    // A master effect's settings changed in its window: saved when its window
    // closes or the app quits (EngineStatus polls the engine).
    void noteEdited() { m_edited = true; }
    // Stores every master effect's settings and writes the list.
    void save();

signals:
    void effectsChanged();

private:
    [[nodiscard]] bool validIndex(int effect) const;
    [[nodiscard]] QString busName() const;     // "master" or "aux", for messages
    [[nodiscard]] QString settingsKey() const; // where its effects are kept
    void playInEngine();                       // the engine plays the effects as they are now
    std::vector<QString> storeStates();        // the effects' settings into m_effects (problems returned)
    // After the list changed: the engine plays it, it is saved.
    void commit();

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QSettings& m_settings;
    EffectWindows& m_windows;
    EffectWindows::Bus m_bus;
    std::vector<core::PluginSlot> m_effects;
    bool m_edited = false;
};

} // namespace gigchain::ui
