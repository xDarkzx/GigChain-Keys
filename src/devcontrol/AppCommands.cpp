// The live test control's commands for the app's own actions and state (the
// screen's controllers, generically), its life and its files.
#include "DevControl.h"
#include "DevLog.h"
#include "PluginLoadGuard.h"
#include "Reflection.h"

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

} // namespace

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

namespace {

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
        QJsonArray args = request.args;
        args.removeFirst(); // (the object)
        args.removeFirst(); // (the method)
        const auto result = callMethod(*object, argText(request, 1).toUtf8(), args);
        if (!result) return reply.fail(result.error().message);
        reply.ok({{u"result"_s, *result}});
    });
    control.add(u"get"_s, u"get <object> <property>: a property's value"_s, [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.size() != 2) return reply.fail(u"get <object> <property>"_s);
        QString error;
        QObject* object = resolve(control, argText(request, 0), error);
        if (object == nullptr) return reply.fail(error);
        const auto value = readProperty(*object, argText(request, 1).toUtf8());
        if (!value) return reply.fail(value.error().message);
        reply.ok({{u"value"_s, *value}});
    });
    control.add(u"set"_s, u"set <object> <property> <value>: changes a property"_s, [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.size() != 3) return reply.fail(u"set <object> <property> <value>"_s);
        QString error;
        QObject* object = resolve(control, argText(request, 0), error);
        if (object == nullptr) return reply.fail(error);
        const auto value = writeProperty(*object, argText(request, 1).toUtf8(), request.args.at(2));
        if (!value) return reply.fail(value.error().message);
        reply.ok({{u"value"_s, *value}});
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
    control.add(u"quit"_s, u"quit [discard]: closes the app the normal way; unsaved changes: refused, or dropped with discard"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        // (Closing with unsaved changes would wait on "save changes?" with nobody to answer it.)
        QString error;
        if (QObject* doc = resolve(control, u"document"_s, error); doc != nullptr && doc->property("dirty").toBool()) {
            if (argText(request, 0) != u"discard"_s) {
                return reply.fail(u"The setlist has unsaved changes: quit discard drops them (or save it first: call document save)"_s);
            }
            QMetaObject::invokeMethod(doc, "newSetlist"); // (the changes dropped)
        }
        reply.ok();
        const std::function<void()> quit = control.context().quit;
        QTimer::singleShot(0, qApp, [quit] {
            if (quit) quit();
            else QCoreApplication::quit();
        });
    });
    control.add(u"pid"_s, u"pid: the app's process id (gck-ctl kill uses it)"_s, [](const DevRequest&, const Reply& reply) {
        reply.ok({{u"pid"_s, QCoreApplication::applicationPid()}});
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
