// The live test control's commands for the app's own actions and state (the
// screen's controllers, generically), its life and its files.
#include "DevControl.h"
#include "DevLog.h"
#include "PluginLoadGuard.h"

#include "gigchain/engine/IEngine.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <array>

using namespace Qt::StringLiterals;

namespace gigchain::devcontrol {
namespace {

// The spec's names for the screen's controllers, where the QML calls them otherwise.
const std::map<QString, QString>& aliases()
{
    static const std::map<QString, QString> names{
        {u"document"_s, u"doc"_s}, {u"loop"_s, u"loops"_s}, {u"mixer"_s, u"mixerLayout"_s}, {u"editor"_s, u"editorService"_s}};
    return names;
}

// "document" or "document.notifications": a controller, then QObject properties of it.
QObject* resolve(const DevControl& control, const QString& path, QString& error)
{
    const QStringList parts = path.split(u'.');
    QString first = parts.front();
    if (const auto alias = aliases().find(first); alias != aliases().end()) first = alias->second;
    QObject* object = control.context().objects.value(first).value<QObject*>();
    if (object == nullptr) {
        error = u"No object %1 (objects lists them)"_s.arg(parts.front());
        return nullptr;
    }
    for (qsizetype i = 1; i < parts.size(); ++i) {
        QObject* next = object->property(parts.at(i).toUtf8().constData()).value<QObject*>();
        if (next == nullptr) {
            error = u"%1 has no object property %2"_s.arg(parts.mid(0, i).join(u'.'), parts.at(i));
            return nullptr;
        }
        object = next;
    }
    return object;
}

QJsonValue toJson(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) return QJsonValue::Null;
    if (QObject* object = value.value<QObject*>(); object != nullptr) {
        return QJsonObject{{u"object"_s, QString::fromLatin1(object->metaObject()->className())}, {u"objectName"_s, object->objectName()}};
    }
    const QJsonValue json = QJsonValue::fromVariant(value);
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

// The methods a controller offers (its own and its classes', not QObject's).
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

QJsonArray modelRows(const QAbstractItemModel& model)
{
    QJsonArray rows;
    const QHash<int, QByteArray> roles = model.roleNames();
    for (int row = 0; row < model.rowCount(); ++row) {
        QJsonObject fields;
        for (auto role = roles.cbegin(); role != roles.cend(); ++role) {
            fields.insert(QString::fromLatin1(role.value()), toJson(model.data(model.index(row, 0), role.key())));
        }
        rows.append(fields);
    }
    return rows;
}

QString argText(const DevRequest& request, qsizetype at)
{
    const QJsonValue value = request.args.at(at);
    return value.isString() ? value.toString() : value.toVariant().toString();
}

void addObjectCommands(DevControl& control)
{
    control.add(u"objects"_s, u"objects: the screen's controllers and models, by name"_s, [&control](const DevRequest&, const Reply& reply) {
        QJsonArray names;
        for (auto it = control.context().objects.cbegin(); it != control.context().objects.cend(); ++it) {
            if (QObject* object = it.value().value<QObject*>(); object != nullptr) {
                names.append(QJsonObject{{u"name"_s, it.key()}, {u"class"_s, QString::fromLatin1(object->metaObject()->className())}});
            }
        }
        QJsonObject alias;
        for (const auto& [name, real] : aliases()) alias.insert(name, real);
        reply.ok({{u"objects"_s, names}, {u"aliases"_s, alias}});
    });
    control.add(u"methods"_s, u"methods <object>: what it can be asked to do"_s, [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.isEmpty()) return reply.fail(u"methods <object>"_s);
        QString error;
        QObject* object = resolve(control, argText(request, 0), error);
        if (object == nullptr) return reply.fail(error);
        QJsonArray list;
        for (const QMetaMethod& method : callable(*object)) list.append(signature(method));
        reply.ok({{u"methods"_s, list}});
    });
    control.add(u"props"_s, u"props <object>: its properties and their values"_s, [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.isEmpty()) return reply.fail(u"props <object>"_s);
        QString error;
        QObject* object = resolve(control, argText(request, 0), error);
        if (object == nullptr) return reply.fail(error);
        QJsonArray list;
        const QMetaObject* meta = object->metaObject();
        for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
            const QMetaProperty property = meta->property(i);
            list.append(QJsonObject{{u"name"_s, QString::fromLatin1(property.name())},
                                    {u"type"_s, QString::fromLatin1(property.typeName())},
                                    {u"writable"_s, property.isWritable()},
                                    {u"value"_s, toJson(property.read(object))}});
        }
        reply.ok({{u"props"_s, list}});
    });
    control.add(u"call"_s, u"call <object> <method> [args...]: an action, as the screen asks it"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.size() < 2) return reply.fail(u"call <object> <method> [args...]"_s);
        QString error;
        QObject* object = resolve(control, argText(request, 0), error);
        if (object == nullptr) return reply.fail(error);
        const QByteArray name = argText(request, 1).toUtf8();
        const auto count = static_cast<int>(request.args.size() - 2);
        QStringList named;
        std::optional<QMetaMethod> found;
        for (const QMetaMethod& method : callable(*object)) {
            if (method.name() != name) continue;
            named << signature(method);
            if (method.parameterCount() == count) found = method;
        }
        if (!found) {
            return reply.fail(named.isEmpty() ? u"%1 has no method %2 (methods lists them)"_s.arg(argText(request, 0), QString::fromLatin1(name))
                                              : u"%1 takes: %2"_s.arg(QString::fromLatin1(name), named.join(u" or "_s)));
        }
        constexpr int kMaxArgs = 10;
        if (count > kMaxArgs) return reply.fail(u"At most %1 arguments"_s.arg(kMaxArgs));
        std::vector<QVariant> values;
        values.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            QVariant value = request.args.at(i + 2).toVariant();
            if (!value.convert(found->parameterMetaType(i))) {
                return reply.fail(u"Argument %1 of %2 cannot be a %3"_s.arg(i + 1).arg(signature(*found), QString::fromLatin1(found->parameterTypeName(i))));
            }
            values.push_back(std::move(value));
        }
        QVariant result = found->returnMetaType().id() == QMetaType::Void ? QVariant() : QVariant(found->returnMetaType(), nullptr);
        std::array<void*, kMaxArgs + 1> argv{};
        argv.at(0) = result.isValid() ? result.data() : nullptr;
        for (std::size_t i = 0; i < values.size(); ++i) argv.at(i + 1) = values.at(i).data();
        if (QMetaObject::metacall(object, QMetaObject::InvokeMetaMethod, found->methodIndex(), argv.data()) >= 0) {
            return reply.fail(u"%1 could not be called"_s.arg(signature(*found)));
        }
        if (result.metaType() == QMetaType::fromType<QVariant>()) result = result.value<QVariant>(); // (a method returning a QVariant)
        reply.ok({{u"result"_s, toJson(result)}});
    });
    control.add(u"get"_s, u"get <object> <property>: a property's value"_s, [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.size() != 2) return reply.fail(u"get <object> <property>"_s);
        QString error;
        QObject* object = resolve(control, argText(request, 0), error);
        if (object == nullptr) return reply.fail(error);
        const QByteArray name = argText(request, 1).toUtf8();
        if (object->metaObject()->indexOfProperty(name.constData()) < 0 && !object->dynamicPropertyNames().contains(name)) {
            return reply.fail(u"%1 has no property %2 (props lists them)"_s.arg(argText(request, 0), QString::fromLatin1(name)));
        }
        reply.ok({{u"value"_s, toJson(object->property(name.constData()))}});
    });
    control.add(u"set"_s, u"set <object> <property> <value>: changes a property"_s, [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.size() != 3) return reply.fail(u"set <object> <property> <value>"_s);
        QString error;
        QObject* object = resolve(control, argText(request, 0), error);
        if (object == nullptr) return reply.fail(error);
        const QByteArray name = argText(request, 1).toUtf8();
        const int index = object->metaObject()->indexOfProperty(name.constData());
        if (index < 0) return reply.fail(u"%1 has no property %2 (props lists them)"_s.arg(argText(request, 0), QString::fromLatin1(name)));
        const QMetaProperty property = object->metaObject()->property(index);
        QVariant value = request.args.at(2).toVariant();
        if (!property.isWritable() || !value.convert(property.metaType()) || !property.write(object, value)) {
            return reply.fail(u"%1 could not be set to %2"_s.arg(QString::fromLatin1(name), argText(request, 2)));
        }
        reply.ok({{u"value"_s, toJson(property.read(object))}});
    });
    control.add(u"model"_s, u"model <object>: a list's rows (setlistModel, channelModel, pluginModel...)"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.isEmpty()) return reply.fail(u"model <object>"_s);
        QString error;
        QObject* object = resolve(control, argText(request, 0), error);
        if (object == nullptr) return reply.fail(error);
        const auto* model = qobject_cast<const QAbstractItemModel*>(object);
        if (model == nullptr) return reply.fail(u"%1 is not a list"_s.arg(argText(request, 0)));
        reply.ok({{u"rows"_s, modelRows(*model)}});
    });
    control.add(u"status"_s, u"status: setlist, song, sound, channels, plugins, audio and MIDI in one answer"_s,
                [&control](const DevRequest&, const Reply& reply) {
        QJsonObject status;
        QString error;
        if (QObject* doc = resolve(control, u"document"_s, error)) {
            status.insert(u"setlist"_s, doc->property("filePath").toString());
            status.insert(u"song"_s, doc->property("currentSongName").toString());
            status.insert(u"sound"_s, doc->property("currentPatchName").toString());
            status.insert(u"songIndex"_s, doc->property("songIndex").toInt());
            status.insert(u"soundIndex"_s, doc->property("patchIndex").toInt());
            status.insert(u"tempo"_s, doc->property("songTempo").toDouble());
            status.insert(u"unsaved"_s, doc->property("dirty").toBool());
        }
        if (const auto* channels = qobject_cast<const QAbstractItemModel*>(resolve(control, u"channelModel"_s, error))) {
            status.insert(u"channels"_s, modelRows(*channels));
        }
        if (engine::IEngine* engine = control.context().engine) {
            status.insert(u"loadedPlugins"_s, static_cast<qint64>(engine->loadedPluginCount()));
            status.insert(u"blockedPlugins"_s, QJsonArray::fromStringList(engine->blockedPlugins()));
            const engine::AudioSetup audio = engine->audioSetup();
            status.insert(u"audio"_s, QJsonObject{{u"status"_s, engine->statusText()},
                                                  {u"device"_s, audio.device},
                                                  {u"sampleRate"_s, static_cast<qint64>(audio.sampleRate)},
                                                  {u"buffer"_s, static_cast<qint64>(audio.bufferFrames)}});
            QJsonArray inputs;
            for (const engine::MidiPort& port : engine->midiInputs()) {
                inputs.append(QJsonObject{{u"name"_s, port.name},
                                          {u"mode"_s, port.enabled ? u"plays"_s : port.controlsOnly ? u"buttons and knobs"_s : u"off"_s}});
            }
            status.insert(u"midiInputs"_s, inputs);
        }
        reply.ok(status);
    });
}

void addLifeCommands(DevControl& control)
{
    control.add(u"quit"_s, u"quit: closes the app the normal way"_s, [](const DevRequest&, const Reply& reply) {
        reply.ok();
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    });
    control.add(u"crash"_s, u"crash: crashes the app on purpose (the crash reporter's path)"_s, [](const DevRequest&, const Reply& reply) {
        reply.ok();
        QTimer::singleShot(100, qApp, [] {
            qCCritical(lcDev) << "Dev control: crashing on purpose";
            volatile int* nowhere = nullptr;
            *nowhere = 1; // NOLINT(clang-analyzer-core.NullDereference): the crash asked for
        });
    });
    control.add(u"freeze"_s, u"freeze <ms>: holds the main thread that long (the freeze watchdog's path)"_s,
                [](const DevRequest& request, const Reply& reply) {
        const int ms = request.args.isEmpty() ? -1 : request.args.at(0).toInt(-1);
        if (ms < 0 || ms > 60'000) return reply.fail(u"freeze <ms>: 0-60000"_s);
        reply.ok();
        QTimer::singleShot(0, qApp, [ms] { QThread::msleep(static_cast<unsigned long>(ms)); });
    });
}

void addFileCommands(DevControl& control)
{
    const auto guardFolder = [&control] { return control.context().dataFolder + u"/plugin-guard"_s; };
    control.add(u"paths"_s, u"paths: where the log, settings, crash reports and plugin guard live"_s, [&control, guardFolder](const DevRequest&, const Reply& reply) {
        const auto& context = control.context();
        reply.ok({{u"log"_s, QDir::toNativeSeparators(context.logFile)},
                  {u"data"_s, QDir::toNativeSeparators(context.dataFolder)},
                  {u"settings"_s, QSettings().fileName()},
                  {u"crashReports"_s, QDir::toNativeSeparators(context.dataFolder + u"/crash-reports"_s)},
                  {u"pluginGuard"_s, QDir::toNativeSeparators(guardFolder())}});
    });
    control.add(u"read"_s, u"read <blocked|guard|settings|crash-reports>: one of the app's own files"_s,
                [&control, guardFolder](const DevRequest& request, const Reply& reply) {
        const QString which = request.args.isEmpty() ? QString() : argText(request, 0);
        if (which == u"blocked"_s) {
            QFile file(guardFolder() + u"/blocked.json"_s);
            if (!file.exists()) return reply.ok({{u"text"_s, u"[]"_s}});
            if (!file.open(QIODevice::ReadOnly)) return reply.fail(u"Could not read %1: %2"_s.arg(file.fileName(), file.errorString()));
            return reply.ok({{u"text"_s, QString::fromUtf8(file.readAll())}});
        }
        const auto listing = [&reply](const QString& folder, const QStringList& filters) {
            reply.ok({{u"files"_s, QJsonArray::fromStringList(QDir(folder).entryList(filters, QDir::Files, QDir::Time))}});
        };
        if (which == u"guard"_s) return listing(guardFolder() + u"/loading"_s, {u"*.loading"_s, u"*.cutshort"_s});
        if (which == u"crash-reports"_s) return listing(control.context().dataFolder + u"/crash-reports"_s, {});
        if (which == u"settings"_s) {
            const QSettings settings;
            QJsonObject values;
            for (const QString& key : settings.allKeys()) values.insert(key, toJson(settings.value(key)));
            return reply.ok({{u"settings"_s, values}});
        }
        reply.fail(u"read <blocked|guard|settings|crash-reports>"_s);
    });
    control.add(u"guard-block"_s, u"guard-block <plugin path>: switches a plugin off as a crash while loading would (from the next start)"_s,
                [guardFolder](const DevRequest& request, const Reply& reply) {
        if (request.args.isEmpty()) return reply.fail(u"guard-block <plugin path>"_s);
        const QString plugin = argText(request, 0);
        {
            const engine::PluginLoadGuard loading(guardFolder());
            const auto marker = loading.loading(plugin); // the app "dies" loading it...
            engine::PluginLoadGuard nextStart(guardFolder());
            (void)nextStart.takeCrashed(true); // ...and the next start finds it: blocked
        }
        reply.ok({{u"blocked"_s, plugin}, {u"note"_s, u"switched off from the next start (restart the app)"_s}});
    });
}

} // namespace

void addAppCommands(DevControl& control)
{
    addObjectCommands(control);
    addLifeCommands(control);
    addFileCommands(control);
}

} // namespace gigchain::devcontrol
