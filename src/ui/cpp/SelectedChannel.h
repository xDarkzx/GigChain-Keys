#pragma once

#include "openstage/core/Model.h"

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace openstage::ui {

class DocumentController;

// The inspector's view of the selected mixer channel.
class SelectedChannel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(bool valid READ isValid NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString instrumentName READ instrumentName NOTIFY changed)
    Q_PROPERTY(QStringList effectNames READ effectNames NOTIFY changed)
    Q_PROPERTY(int keyLow READ keyLow NOTIFY changed)
    Q_PROPERTY(int keyHigh READ keyHigh NOTIFY changed)
    Q_PROPERTY(int transpose READ transpose NOTIFY changed)
    Q_PROPERTY(int midiChannel READ midiChannel NOTIFY changed)
    Q_PROPERTY(double volumeDb READ volumeDb NOTIFY changed)

public:
    explicit SelectedChannel(const DocumentController& document, QObject* parent = nullptr);

    [[nodiscard]] bool isValid() const { return channel() != nullptr; }
    [[nodiscard]] int index() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString instrumentName() const;
    [[nodiscard]] QStringList effectNames() const;
    [[nodiscard]] int keyLow() const;
    [[nodiscard]] int keyHigh() const;
    [[nodiscard]] int transpose() const;
    [[nodiscard]] int midiChannel() const;
    [[nodiscard]] double volumeDb() const;

signals:
    void changed();

private:
    [[nodiscard]] const core::Channel* channel() const;

    const DocumentController& m_document;
};

} // namespace openstage::ui
