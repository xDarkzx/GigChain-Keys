#pragma once

#include "gigchain/core/Model.h"
#include "gigchain/engine/IPluginEditor.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
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
    // The buses with effects of their own (outside the setlist).
    // (SustainPads: the instrument loaded into it; SustainPadsFx: its strip's effects.)
    enum class Bus : int { Master = 0, Aux = 1, SustainPads = 2, SustainPadsFx = 3 };
    // The same for effect `effect` of a bus (`busSlots` as it is now).
    bool openBus(Bus bus, int effect, const std::vector<core::PluginSlot>& busSlots, QWindow* owner);
    // Closes a bus's windows whose effect is no longer there as it was.
    void sweepBus(Bus bus, const std::vector<core::PluginSlot>& busSlots);
    // The window of the instrument loaded into Sustain Pads (`pluginId`,
    // called `name`), opened or raised; false when it has none (said).
    bool openSustainPads(const QString& pluginId, const QString& name, QWindow* owner);
    // Closes it (another instrument was loaded, or Pads Pro again).
    void closeSustainPads();
    Q_INVOKABLE void closeAll();

    // The app's own effects (OTT, Enhance, Sidechain) have no plugin window:
    // the app draws theirs (BuiltinFxWindow.qml), through these. `channel` and
    // `effect` are places in the current patch, as open() takes them.
    // The effect's plugin id there ("builtin:ott"), empty when there is none.
    Q_INVOKABLE [[nodiscard]] QString effectPluginId(int channel, int effect) const;
    // Its settings: [{id, name, choices}].
    Q_INVOKABLE [[nodiscard]] QVariantList builtinEffectParameters(int channel, int effect) const;
    // A setting's (or a reading's) value now, 0-1; below 0 when not known.
    Q_INVOKABLE [[nodiscard]] double builtinEffectValue(int channel, int effect, int parameter) const;
    // Sets it as its window does; problems are shown and logged.
    Q_INVOKABLE void setBuiltinEffectValue(int channel, int effect, int parameter, double value);
    // Sidechain curve `curve` at `slope` as `points` levels (0-1) over one cycle, for drawing.
    Q_INVOKABLE [[nodiscard]] QVariantList sidechainCurve(int curve, double slope, int points) const;

signals:
    void openCountChanged();
    // One of the app's own effects was clicked: its window (the app's) is
    // to open (or come to the front), titled `title`, above `owner`.
    void builtinEffectRequested(int channel, int effect, const QString& pluginId, const QString& title, QWindow* owner);
    // A bus effect's window was closed (its settings may have changed): `bus` an EffectWindows::Bus.
    void busWindowClosed(int bus);

private:
    struct Entry;
    void close(Entry& entry);
    // Shows an editor in a new floating window; takes it on success.
    bool show(std::unique_ptr<Entry> entry, const QString& title, QWindow* owner);
    // Closes the windows whose effect is no longer sounding as it was.
    void sweep();
    // The channel at `channel` in the current patch, when it is there.
    [[nodiscard]] const core::Channel* channelAt(int channel) const;

    engine::IEngine& m_engine;
    DocumentController& m_document;
    std::vector<std::unique_ptr<Entry>> m_open;
};

} // namespace gigchain::ui
