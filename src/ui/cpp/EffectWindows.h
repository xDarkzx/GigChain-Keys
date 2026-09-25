#pragma once

#include "gigchain/core/Model.h"
#include "gigchain/engine/IPluginEditor.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;

// Effects' own windows, each in a floating window of its own (as DAWs do),
// kept above the main window. Clicking an effect that is already open brings
// its window to the front. A window closes by itself when its effect leaves
// the sound: removed, replaced, switched off, or another patch is chosen.
class EffectWindows : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(int openCount READ openCount NOTIFY openCountChanged)

public:
    EffectWindows(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);
    ~EffectWindows() override;
    EffectWindows(const EffectWindows&) = delete;
    EffectWindows& operator=(const EffectWindows&) = delete;
    EffectWindows(EffectWindows&&) = delete;
    EffectWindows& operator=(EffectWindows&&) = delete;

    [[nodiscard]] int openCount() const { return static_cast<int>(m_open.size()); }

    // Opens (or raises) the window of effect `effect` of channel `channel` of
    // the current patch, above `owner` (the main window). False when it has
    // no window; why is shown in the banner and logged.
    Q_INVOKABLE bool open(int channel, int effect, QWindow* owner);
    // The same for effect `effect` of the master bus (`masterSlots` as it is now).
    bool openMaster(int effect, const std::vector<core::PluginSlot>& masterSlots, QWindow* owner);
    // Closes master windows whose effect is no longer there as it was.
    void sweepMaster(const std::vector<core::PluginSlot>& masterSlots);
    Q_INVOKABLE void closeAll();

signals:
    void openCountChanged();
    // A master effect's window was closed (its settings may have changed).
    void masterWindowClosed();

private:
    struct Entry;
    void close(Entry& entry);
    // Shows an editor in a new floating window; takes it on success.
    bool show(std::unique_ptr<Entry> entry, const QString& title, QWindow* owner);
    // Closes the windows whose effect is no longer sounding as it was.
    void sweep();

    engine::IEngine& m_engine;
    DocumentController& m_document;
    std::vector<std::unique_ptr<Entry>> m_open;
};

} // namespace gigchain::ui
