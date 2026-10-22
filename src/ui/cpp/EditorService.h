#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/engine/IPluginEditor.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace gigchain::engine {
class IEngine;
}
namespace gigchain::core {
struct Channel;
}

namespace gigchain::ui {

class DocumentController;

// Decides which plugin editor the main area shows: the instrument of the
// selected channel of the current patch.
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

public:
    EditorService(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);

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

private:
    // The selected channel, when its instrument is one of the app's own.
    [[nodiscard]] const core::Channel* builtinChannel() const;

    engine::IEngine& m_engine;
    DocumentController& m_document;
    QTimer m_coalesce; // merges the several signals one edit emits
};

} // namespace gigchain::ui
