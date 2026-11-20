#pragma once

#include "gigchain/core/Error.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QMetaMethod>
#include <QVariant>

#include <vector>

class QObject;

namespace gigchain::devcontrol {

// Qt's meta-object system for the live test control: any object's methods and
// properties, by name, with JSON values (the screen's controllers and items).

// A value as JSON: an object as its class and objectName; anything with no
// JSON form as its text, or its type in brackets.
[[nodiscard]] QJsonValue toJson(const QVariant& value);
// "bool renamePatch(int song, int patch, QString name)".
[[nodiscard]] QString signature(const QMetaMethod& method);
// Its public methods and slots (its own classes', not QObject's).
[[nodiscard]] std::vector<QMetaMethod> callable(const QObject& object);
// Calls `name` with `args` (converted to its parameters' types); its return
// value. Errors: no such method (its overloads named), an argument of the wrong kind.
[[nodiscard]] core::Result<QJsonValue> callMethod(QObject& object, const QByteArray& name, const QJsonArray& args);
[[nodiscard]] core::Result<QJsonValue> readProperty(const QObject& object, const QByteArray& name);
// Sets it (converted to its type); its value afterwards.
[[nodiscard]] core::Result<QJsonValue> writeProperty(QObject& object, const QByteArray& name, const QJsonValue& value);
// What the object is, for messages: "QQuickButton settingsButton".
[[nodiscard]] QString describe(const QObject& object);

} // namespace gigchain::devcontrol
