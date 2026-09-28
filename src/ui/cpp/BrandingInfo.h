#pragma once

#include "gigchain/core/Branding.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gigchain::ui {

// branding.cmake for QML: `Branding.name`, `Branding.brand`, ...
class BrandingInfo : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Branding)
    QML_SINGLETON

    Q_PROPERTY(QString brand READ brand CONSTANT)
    Q_PROPERTY(QString edition READ edition CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString website READ website CONSTANT)
    Q_PROPERTY(QString setlistSuffix READ setlistSuffix CONSTANT)
    Q_PROPERTY(QString jsonSetlistSuffix READ jsonSetlistSuffix CONSTANT)

public:
    using QObject::QObject;

    [[nodiscard]] static QString brand() { return branding::brand(); }
    [[nodiscard]] static QString edition() { return branding::edition(); }
    [[nodiscard]] static QString name() { return branding::name(); }
    [[nodiscard]] static QString version() { return branding::version(); }
    [[nodiscard]] static QString website() { return branding::website(); }
    [[nodiscard]] static QString setlistSuffix() { return branding::setlistSuffix(); }
    [[nodiscard]] static QString jsonSetlistSuffix() { return branding::jsonSetlistSuffix(); }
};

} // namespace gigchain::ui
