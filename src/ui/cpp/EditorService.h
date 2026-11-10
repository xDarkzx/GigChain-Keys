#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/core/Ids.h"
#include "gigchain/engine/IPluginEditor.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>

namespace gigchain::engine {
class IEngine;
}
namespace gigchain::core {
struct Channel;
}

namespace gigchain::ui {

class DocumentController;

// Decides which plugin editor a view shows: the instrument of the selected
// channel of the current patch (linked, as the main area and MainStage's
// linked plug-in window), or of one channel it is kept on (pinned).
class EditorService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    // The selected channel plays one of the app's own instruments (Saw Stack
    // Pro): the app draws its window (builtinParameters and the like).
    Q_PROPERTY(bool builtinSelected READ builtinSelected NOTIFY targetChanged)
    Q_PROPERTY(QString builtinName READ builtinName NOTIFY targetChanged)
    // Which one ("builtin:saw-stack-pro", "builtin:piano-pro"): its window is chosen by it.
    Q_PROPERTY(QString builtinId READ builtinId NOTIFY targetChanged)
    // The channel shown: its place in the current patch (-1: none) and its name.
    Q_PROPERTY(int channelIndex READ channelIndex NOTIFY targetChanged)
    Q_PROPERTY(QString channelName READ channelName NOTIFY labelChanged)
    // Follows the selected channel (true) or stays on one (false).
    Q_PROPERTY(bool linked READ linked NOTIFY linkedChanged)
    // Its instrument is shown in a window of its own, so not here (a plugin
    // draws in one place at a time): the view opens nothing.
    Q_PROPERTY(bool shownElsewhere READ shownElsewhere NOTIFY shownElsewhereChanged)

public:
    EditorService(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);

    // Kept on `channel` from now on, or (nullopt) following the selection again.
    void pin(const std::optional<core::ChannelId>& channel);
    [[nodiscard]] bool linked() const { return !m_pinned.has_value(); }
    // The channel shown, when it is in the current patch.
    [[nodiscard]] std::optional<core::ChannelId> targetId() const;
    [[nodiscard]] int channelIndex() const;
    [[nodiscard]] QString channelName() const;
    // Asked fresh each time (by InstrumentWindows): is the target shown elsewhere?
    void setElsewhereCheck(std::function<bool()> check);
    [[nodiscard]] bool shownElsewhere() const { return m_elsewhere && m_elsewhere(); }
    // The answer may have changed: says so when it did.
    void recheckElsewhere();

    [[nodiscard]] bool builtinSelected() const;
    [[nodiscard]] QString builtinName() const;
    [[nodiscard]] QString builtinId() const;
    // The selected built-in instrument's parameters: [{id, name, choices}]
    // (choices: a stepped one's settings by name); empty when none is selected or loaded.
    Q_INVOKABLE [[nodiscard]] QVariantList builtinParameters() const;
    // A parameter's value now (0-1), below 0 when not known.
    Q_INVOKABLE [[nodiscard]] double builtinValue(int parameter) const;
    // Sets it as the instrument's window does. Problems are shown and logged.
    Q_INVOKABLE void setBuiltinValue(int parameter, double value);
    // Saw Stack Pro's sidechain curve `curve` at `slope`, as `points` levels
    // (0-1) over one cycle: the engine's own shapes, for its window to draw.
    Q_INVOKABLE [[nodiscard]] QVariantList sidechainCurve(int curve, double slope, int points) const;
    // Where the selected Saw Stack Pro's sidechain is in its cycle (0-1), below 0 when not known.
    Q_INVOKABLE [[nodiscard]] double sidechainPosition() const;
    // What the selected built-in shows besides its settings (engine::readings),
    // by name: "pianoRecordings", "pianoSource". Below 0 when not known.
    Q_INVOKABLE [[nodiscard]] double builtinReading(const QString& name) const;
    // The file the selected built-in plays (the SoundFont Player's .sf2), as
    // a path; empty when none. Choosing one (a path or a file: URL) reads it
    // in the background; problems are shown and logged.
    Q_INVOKABLE [[nodiscard]] QString builtinFile() const;
    Q_INVOKABLE void setBuiltinFile(const QString& pathOrUrl);

    // nullptr when there is nothing to show. Failures are shown to the user
    // (banner) and logged, and returned.
    core::Result<std::unique_ptr<engine::IPluginEditor>> createForSelection();
    // Shows an editor problem to the user (it has already been logged).
    void reportFailure(const QString& message);
    // Why nothing is shown, for the placeholder text.
    [[nodiscard]] QString emptyReason() const;

signals:
    // The editor to show may have changed (selection, patch or channels).
    // Emitted once per burst of document changes (opening an editor is slow).
    void targetChanged();
    void labelChanged();
    void linkedChanged();
    void shownElsewhereChanged();

private:
    // The channel shown, when it is in the current patch.
    [[nodiscard]] const core::Channel* targetChannel() const;
    // The target channel, when its instrument is one of the app's own.
    [[nodiscard]] const core::Channel* builtinChannel() const;

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QTimer m_coalesce; // merges the several signals one edit emits
    std::optional<core::ChannelId> m_pinned;
    QString m_pinnedName; // its name when pinned (for "not in this song")
    std::function<bool()> m_elsewhere;
    bool m_wasElsewhere = false;
};

} // namespace gigchain::ui
