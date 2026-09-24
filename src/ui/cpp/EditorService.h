#pragma once

#include "openstage/core/Error.h"
#include "openstage/engine/IPluginEditor.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace openstage::engine {
class IEngine;
}

namespace openstage::ui {

class DocumentController;

// Decides which plugin editor the main area shows: the instrument of the
// selected channel of the current patch.
class EditorService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

public:
    EditorService(engine::IEngine& engine, DocumentController& document, QObject* parent = nullptr);

    // nullptr when there is nothing to show. Failures are shown to the user
    // (banner) and logged, and returned.
    core::Result<std::unique_ptr<engine::IPluginEditor>> createForSelection();
    // Shows an editor problem to the user (it has already been logged).
    void reportFailure(const QString& message);
    // Why nothing is shown, for the placeholder text.
    [[nodiscard]] QString emptyReason() const;

signals:
    // The editor to show may have changed (selection, patch or channels).
    void targetChanged();

private:
    engine::IEngine& m_engine;
    DocumentController& m_document;
};

} // namespace openstage::ui
