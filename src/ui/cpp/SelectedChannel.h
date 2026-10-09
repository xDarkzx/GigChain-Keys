#pragma once

#include "gigchain/core/Model.h"

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace gigchain::ui {

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
    Q_PROPERTY(int velocityLow READ velocityLow NOTIFY changed)
    Q_PROPERTY(int velocityHigh READ velocityHigh NOTIFY changed)
    // What it takes from the keyboard (DocumentController::setChannelTakes).
    Q_PROPERTY(bool takesSustain READ takesSustain NOTIFY changed)
    Q_PROPERTY(bool takesExpression READ takesExpression NOTIFY changed)
    Q_PROPERTY(bool takesModWheel READ takesModWheel NOTIFY changed)
    Q_PROPERTY(bool takesPitchBend READ takesPitchBend NOTIFY changed)
    Q_PROPERTY(bool takesAftertouch READ takesAftertouch NOTIFY changed)
    // 1-based audio input it plays (0 = an instrument channel); right 0 = mono.
    Q_PROPERTY(int inputLeft READ inputLeft NOTIFY changed)
    Q_PROPERTY(int inputRight READ inputRight NOTIFY changed)

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
    [[nodiscard]] int velocityLow() const;
    [[nodiscard]] int velocityHigh() const;
    [[nodiscard]] bool takesSustain() const { return takes(&core::Channel::takesSustain); }
    [[nodiscard]] bool takesExpression() const { return takes(&core::Channel::takesExpression); }
    [[nodiscard]] bool takesModWheel() const { return takes(&core::Channel::takesModWheel); }
    [[nodiscard]] bool takesPitchBend() const { return takes(&core::Channel::takesPitchBend); }
    [[nodiscard]] bool takesAftertouch() const { return takes(&core::Channel::takesAftertouch); }
    [[nodiscard]] int inputLeft() const;
    [[nodiscard]] int inputRight() const;

signals:
    void changed();

private:
    [[nodiscard]] const core::Channel* channel() const;
    [[nodiscard]] bool takes(bool core::Channel::* field) const
    {
        const core::Channel* c = channel();
        return c == nullptr || c->*field; // (no channel: everything, as a new one)
    }

    const DocumentController& m_document;
};

} // namespace gigchain::ui
