#include "Reflection.h"

#include <QJsonObject>
#include <QMetaProperty>
#include <QObject>
#include <QStringList>

#include <array>
#include <optional>

using namespace Qt::StringLiterals;

namespace gigchain::devcontrol {

QJsonValue toJson(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) return QJsonValue::Null;
    if (auto* object = value.value<QObject*>(); object != nullptr) {
        return QJsonObject{{u"object"_s, QString::fromLatin1(object->metaObject()->className())}, {u"objectName"_s, object->objectName()}};
    }
    QJsonValue json = QJsonValue::fromVariant(value);
    if (!json.isNull()) return json;
    if (value.canConvert<QString>()) return value.toString();
    return u"(%1)"_s.arg(QString::fromLatin1(value.typeName()));
}

QString signature(const QMetaMethod& method)
{
    QStringList params;
    const QList<QByteArray> names = method.parameterNames();
    for (int i = 0; i < method.parameterCount(); ++i) {
        params << (QString::fromLatin1(method.parameterTypeName(i)) + u' ' + QString::fromLatin1(names.value(i))).trimmed();
    }
    return u"%1 %2(%3)"_s.arg(QString::fromLatin1(method.typeName()), QString::fromLatin1(method.name()), params.join(u", "_s));
}

std::vector<QMetaMethod> callable(const QObject& object)
{
    std::vector<QMetaMethod> methods;
    const QMetaObject* meta = object.metaObject();
    for (int i = QObject::staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.access() == QMetaMethod::Public && method.methodType() != QMetaMethod::Signal) methods.push_back(method);
    }
    return methods;
}

core::Result<QJsonValue> callMethod(QObject& object, const QByteArray& name, const QJsonArray& args)
{
    constexpr int kMaxArgs = 10;
    const auto count = static_cast<int>(args.size());
    if (count > kMaxArgs) return core::fail(core::ErrorCode::InvalidData, u"At most %1 arguments"_s.arg(kMaxArgs));
    QStringList named;
    std::optional<QMetaMethod> found;
    for (const QMetaMethod& method : callable(object)) {
        if (method.name() != name) continue;
        named << signature(method);
        if (method.parameterCount() == count) found = method;
    }
    if (!found) {
        return core::fail(core::ErrorCode::InvalidData, named.isEmpty()
                                                            ? u"%1 has no method %2 (methods lists them)"_s.arg(describe(object), QString::fromLatin1(name))
                                                            : u"%1 takes: %2"_s.arg(QString::fromLatin1(name), named.join(u" or "_s)));
    }
    std::vector<QVariant> values;
    values.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        QVariant value = args.at(i).toVariant();
        const QMetaType wanted = found->parameterMetaType(i);
        // (A QML function's parameters take anything: QVariant.)
        if (wanted != QMetaType::fromType<QVariant>() && !value.convert(wanted)) {
            return core::fail(core::ErrorCode::InvalidData, u"Argument %1 of %2 cannot be a %3"_s.arg(i + 1).arg(signature(*found), QString::fromLatin1(found->parameterTypeName(i))));
        }
        values.push_back(std::move(value));
    }
    QVariant result = found->returnMetaType().id() == QMetaType::Void ? QVariant() : QVariant(found->returnMetaType(), nullptr);
    std::array<void*, kMaxArgs + 1> argv{};
    argv.at(0) = result.isValid() ? result.data() : nullptr;
    for (std::size_t i = 0; i < values.size(); ++i) {
        // A QVariant parameter takes the QVariant itself; any other, the value it holds.
        argv.at(i + 1) = found->parameterMetaType(static_cast<int>(i)) == QMetaType::fromType<QVariant>()
                             ? static_cast<void*>(&values.at(i))
                             : values.at(i).data();
    }
    if (QMetaObject::metacall(&object, QMetaObject::InvokeMetaMethod, found->methodIndex(), argv.data()) >= 0) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 could not be called"_s.arg(signature(*found)));
    }
    if (result.metaType() == QMetaType::fromType<QVariant>()) result = result.value<QVariant>(); // (it returned a QVariant)
    return toJson(result);
}

core::Result<QJsonValue> readProperty(const QObject& object, const QByteArray& name)
{
    if (object.metaObject()->indexOfProperty(name.constData()) < 0 && !object.dynamicPropertyNames().contains(name)) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 has no property %2 (props lists them)"_s.arg(describe(object), QString::fromLatin1(name)));
    }
    return toJson(object.property(name.constData()));
}

core::Result<QJsonValue> writeProperty(QObject& object, const QByteArray& name, const QJsonValue& value)
{
    const int index = object.metaObject()->indexOfProperty(name.constData());
    if (index < 0) return core::fail(core::ErrorCode::InvalidData, u"%1 has no property %2 (props lists them)"_s.arg(describe(object), QString::fromLatin1(name)));
    const QMetaProperty property = object.metaObject()->property(index);
    QVariant converted = value.toVariant();
    const bool takesAnything = property.metaType() == QMetaType::fromType<QVariant>();
    if (!property.isWritable() || (!takesAnything && !converted.convert(property.metaType())) || !property.write(&object, converted)) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 of %2 could not be set to %3"_s.arg(QString::fromLatin1(name), describe(object), value.toVariant().toString()));
    }
    return toJson(property.read(&object));
}

QString describe(const QObject& object)
{
    const QString name = object.objectName();
    return QString::fromLatin1(object.metaObject()->className()) + (name.isEmpty() ? QString() : u' ' + name);
}

} // namespace gigchain::devcontrol
