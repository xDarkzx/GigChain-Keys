// The live test control's commands for the screen: find, read and press any
// named item (dialogs and popups included), keys, the window's size, pictures.
#include "DevControl.h"
#include "DevLog.h"
#include "Reflection.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QKeySequence>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

// What Qt's own test library uses to deliver input the way the window system
// does (exported by Qt GUI): shortcuts, hover and popups' Escape all see it.
QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow* window, const QPointF& local, const QPointF& global, Qt::MouseButtons state,
                                      Qt::MouseButton button, QEvent::Type type, Qt::KeyboardModifiers mods, int timestamp);
Q_GUI_EXPORT void qt_handleKeyEvent(QWindow* w, QEvent::Type t, int k, Qt::KeyboardModifiers mods, const QString& text, bool autorep,
                                    ushort count);
Q_GUI_EXPORT bool qt_sendShortcutOverrideEvent(QObject* o, ulong timestamp, int k, Qt::KeyboardModifiers mods, const QString& text,
                                               bool autorep, ushort count);
QT_END_NAMESPACE

namespace gigchain::devcontrol {
namespace {

int timestamp()
{
    static QElapsedTimer clock;
    if (!clock.isValid()) clock.start();
    return static_cast<int>(clock.elapsed() + 1000); // (later than anything the window system sent)
}

// Children in the order they are painted (later: on top).
QList<QQuickItem*> paintOrder(const QQuickItem& item)
{
    QList<QQuickItem*> children = item.childItems();
    std::ranges::stable_sort(children, [](const QQuickItem* a, const QQuickItem* b) { return a->z() < b->z(); });
    return children;
}

void walk(QQuickItem* item, int depth, const std::function<void(QQuickItem*, int)>& visit)
{
    visit(item, depth);
    for (QQuickItem* child : paintOrder(*item)) walk(child, depth + 1, visit);
}

// A popup or dialog (not an item itself): the item it shows.
QQuickItem* itemOf(QObject* object)
{
    if (auto* item = qobject_cast<QQuickItem*>(object)) return item;
    return object != nullptr ? object->property("contentItem").value<QQuickItem*>() : nullptr;
}

// A named object: a visible item first (the window's, its dialogs' and popups'),
// then a hidden one, then any object of the window (a dialog, a shortcut).
QObject* findNamed(QQuickWindow& window, const QString& name)
{
    QQuickItem* visible = nullptr;
    QQuickItem* hidden = nullptr;
    walk(window.contentItem(), 0, [&](QQuickItem* item, int) {
        if (item->objectName() != name) return;
        if (item->isVisible() && visible == nullptr) visible = item;
        if (!item->isVisible() && hidden == nullptr) hidden = item;
    });
    if (visible != nullptr) return visible;
    if (hidden != nullptr) return hidden;
    return window.findChild<QObject*>(name);
}

QJsonObject geometry(const QQuickItem& item)
{
    const QRectF rect = item.mapRectToScene(QRectF(0, 0, item.width(), item.height()));
    return QJsonObject{{u"x"_s, qRound(rect.x())}, {u"y"_s, qRound(rect.y())}, {u"w"_s, qRound(rect.width())}, {u"h"_s, qRound(rect.height())}};
}

QString className(const QObject& object)
{
    QString name = QString::fromLatin1(object.metaObject()->className());
    if (const qsizetype qml = name.indexOf(u"_QMLTYPE_"_s); qml > 0) name.truncate(qml); // (a QML file's own type)
    return name;
}

bool isWithin(const QQuickItem* item, const QQuickItem* ancestor)
{
    for (const QQuickItem* at = item; at != nullptr; at = at->parentItem()) {
        if (at == ancestor) return true;
    }
    return false;
}

// The topmost item at `scene` that takes the mouse.
QQuickItem* topAt(QQuickItem* item, QPointF scene)
{
    if (!item->isVisible()) return nullptr;
    const bool inside = item->contains(item->mapFromScene(scene));
    if (item->clip() && !inside) return nullptr;
    const QList<QQuickItem*> children = paintOrder(*item);
    for (auto child = children.crbegin(); child != children.crend(); ++child) {
        if (QQuickItem* top = topAt(*child, scene)) return top;
    }
    // (The popup layer takes the mouse even with nothing open; an open modal dialog is checked on its own.)
    if (item->inherits("QQuickOverlay")) return nullptr;
    return inside && item->acceptedMouseButtons() != Qt::NoButton ? item : nullptr;
}

// Why `item` cannot be pressed now (empty: it can): hidden, disabled, behind an open modal dialog or another control.
QString blocked(QQuickWindow& window, QQuickItem& item)
{
    if (!item.isVisible()) return u"%1 is not visible"_s.arg(describe(item));
    if (!item.isEnabled()) return u"%1 is disabled"_s.arg(describe(item));
    for (QObject* popup : window.findChildren<QObject*>()) {
        if (!popup->inherits("QQuickPopup") || !popup->property("opened").toBool() || !popup->property("modal").toBool()) continue;
        const QQuickItem* shown = itemOf(popup);
        if (shown != nullptr && !isWithin(&item, shown->parentItem())) {
            return u"%1 is covered by the open dialog %2"_s.arg(describe(item), describe(*popup));
        }
    }
    const QPointF centre = item.mapToScene(QPointF(item.width() / 2, item.height() / 2));
    QQuickItem* top = topAt(window.contentItem(), centre);
    if (top != nullptr && !isWithin(top, &item) && !isWithin(&item, top)) {
        return u"%1 is covered by %2"_s.arg(describe(item), describe(*top));
    }
    return {};
}

void mouse(QQuickWindow& window, QPointF scene, QEvent::Type type, Qt::MouseButtons state, Qt::MouseButton button)
{
    qt_handleMouseEvent(&window, scene, window.mapToGlobal(scene), state, button, type, Qt::NoModifier, timestamp());
}

} // namespace

void addUiCommands(DevControl& control)
{
    // The item named in argument `at`, or why there is none.
    const auto named = [&control](const DevRequest& request, qsizetype at, QString& error) -> QObject* {
        QQuickWindow* window = control.context().window;
        if (window == nullptr) {
            error = u"No main window"_s;
            return nullptr;
        }
        const QString name = argText(request, at);
        if (name.isEmpty()) {
            error = u"Name an item (ui-tree lists them)"_s;
            return nullptr;
        }
        QObject* found = findNamed(*window, name);
        if (found == nullptr) error = u"Nothing named %1 on the screen (ui-tree lists them)"_s.arg(name);
        return found;
    };

    control.add(u"ui-tree"_s, u"ui-tree [name] [--all]: the visible items (under one): name, kind, text, place"_s,
                [&control, named](const DevRequest& request, const Reply& reply) {
        const bool all = std::ranges::any_of(request.args, [](const QJsonValue& v) { return v.toString() == u"--all"_s; });
        QQuickItem* root = control.context().window != nullptr ? control.context().window->contentItem() : nullptr;
        if (!request.args.isEmpty() && argText(request, 0) != u"--all"_s) {
            QString error;
            root = itemOf(named(request, 0, error));
            if (root == nullptr) return reply.fail(error.isEmpty() ? u"%1 shows nothing"_s.arg(argText(request, 0)) : error);
        }
        if (root == nullptr) return reply.fail(u"No main window"_s);
        constexpr qsizetype kMost = 5000;
        QJsonArray items;
        walk(root, 0, [&](QQuickItem* item, int depth) {
            if (items.size() >= kMost || (!all && !item->isVisible())) return;
            const QVariant text = item->property("text");
            const bool hasText = text.typeId() == QMetaType::QString && !text.toString().isEmpty();
            if (!all && item->objectName().isEmpty() && !hasText) return;
            QJsonObject entry = geometry(*item);
            entry.insert(u"name"_s, item->objectName());
            entry.insert(u"kind"_s, className(*item));
            if (hasText) entry.insert(u"text"_s, text.toString());
            entry.insert(u"visible"_s, item->isVisible());
            entry.insert(u"enabled"_s, item->isEnabled());
            entry.insert(u"depth"_s, depth);
            items.append(entry);
        });
        reply.ok({{u"items"_s, items}, {u"cut"_s, items.size() >= kMost}});
    });
    control.add(u"ui-get"_s, u"ui-get <name> <property>: a named item's property"_s, [named](const DevRequest& request, const Reply& reply) {
        QString error;
        QObject* object = named(request, 0, error);
        if (object == nullptr) return reply.fail(error);
        const auto value = readProperty(*object, argText(request, 1).toUtf8());
        if (!value) return reply.fail(value.error().message);
        reply.ok({{u"value"_s, *value}});
    });
    control.add(u"ui-set"_s, u"ui-set <name> <property> <value>: sets a named item's property"_s, [named](const DevRequest& request, const Reply& reply) {
        if (request.args.size() != 3) return reply.fail(u"ui-set <name> <property> <value>"_s);
        QString error;
        QObject* object = named(request, 0, error);
        if (object == nullptr) return reply.fail(error);
        const auto value = writeProperty(*object, argText(request, 1).toUtf8(), request.args.at(2));
        if (!value) return reply.fail(value.error().message);
        reply.ok({{u"value"_s, *value}});
    });
    control.add(u"ui-call"_s, u"ui-call <name> <method> [args...]: calls a named item's method (a dialog's open)"_s,
                [named](const DevRequest& request, const Reply& reply) {
        if (request.args.size() < 2) return reply.fail(u"ui-call <name> <method> [args...]"_s);
        QString error;
        QObject* object = named(request, 0, error);
        if (object == nullptr) return reply.fail(error);
        QJsonArray args = request.args;
        args.removeFirst();
        args.removeFirst();
        const auto result = callMethod(*object, argText(request, 1).toUtf8(), args);
        if (!result) return reply.fail(result.error().message);
        reply.ok({{u"result"_s, *result}});
    });

    // The mouse, on a named item's centre, as the window system delivers it.
    const auto pointer = [named](const std::function<void(QQuickWindow&, QPointF, const DevRequest&)>& act) {
        return [named, act](const DevRequest& request, const Reply& reply) {
            QString error;
            auto* item = qobject_cast<QQuickItem*>(named(request, 0, error));
            if (item == nullptr) return reply.fail(error.isEmpty() ? u"%1 is not on the screen (a dialog: ui-call it)"_s.arg(argText(request, 0)) : error);
            QQuickWindow* window = item->window();
            if (window == nullptr) return reply.fail(u"%1 is in no window"_s.arg(argText(request, 0)));
            if (const QString why = blocked(*window, *item); !why.isEmpty()) return reply.fail(why);
            const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
            act(*window, centre, request);
            reply.ok({{u"at"_s, QJsonObject{{u"x"_s, qRound(centre.x())}, {u"y"_s, qRound(centre.y())}}}});
        };
    };
    control.add(u"ui-click"_s, u"ui-click <name>: presses it with the mouse"_s, pointer([](QQuickWindow& window, QPointF at, const DevRequest&) {
        mouse(window, at, QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        mouse(window, at, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        mouse(window, at, QEvent::MouseButtonRelease, Qt::NoButton, Qt::LeftButton);
    }));
    control.add(u"ui-hover"_s, u"ui-hover <name>: points at it (a tooltip shows)"_s, pointer([](QQuickWindow& window, QPointF at, const DevRequest&) {
        mouse(window, at, QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
    }));
    control.add(u"ui-drag"_s, u"ui-drag <name> <dx> <dy>: drags it that far"_s, pointer([](QQuickWindow& window, QPointF at, const DevRequest& request) {
        const QPointF by(request.args.at(1).toDouble(), request.args.at(2).toDouble());
        mouse(window, at, QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        mouse(window, at, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        constexpr int kSteps = 10;
        for (int step = 1; step <= kSteps; ++step) {
            mouse(window, at + (by * step / kSteps), QEvent::MouseMove, Qt::LeftButton, Qt::NoButton);
        }
        mouse(window, at + by, QEvent::MouseButtonRelease, Qt::NoButton, Qt::LeftButton);
    }));

    control.add(u"key"_s, u"key <keys>: key presses to the window (\"Ctrl+S\", \"Escape\", \"Space\")"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        const QKeySequence keys(argText(request, 0));
        QQuickWindow* window = control.context().window;
        if (keys.isEmpty() || window == nullptr) return reply.fail(u"key <keys>, as \"Ctrl+S\" or \"Escape\""_s);
        // (A dialog or a plugin window in front gets them, as a player's would.)
        QWindow* target = QGuiApplication::focusWindow() != nullptr ? QGuiApplication::focusWindow() : window;
        for (int i = 0; i < keys.count(); ++i) {
            const QKeyCombination combination = keys[i];
            const int key = static_cast<int>(combination.key());
            const Qt::KeyboardModifiers mods = combination.keyboardModifiers();
            const QString text = mods & ~Qt::ShiftModifier ? QString() : QKeySequence(combination.key()).toString().left(1).toLower();
            const QString typed = key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde ? text : QString();
            if (!qt_sendShortcutOverrideEvent(target, static_cast<ulong>(timestamp()), key, mods, typed, false, 1)) {
                qt_handleKeyEvent(target, QEvent::KeyPress, key, mods, typed, false, 1);
            }
            qt_handleKeyEvent(target, QEvent::KeyRelease, key, mods, typed, false, 1);
        }
        reply.ok({{u"keys"_s, keys.toString()}});
    });
    control.add(u"resize"_s, u"resize <width> <height>: the main window's size (a small or HiDPI screen's)"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        QQuickWindow* window = control.context().window;
        const int width = request.args.size() == 2 ? request.args.at(0).toInt() : 0;
        const int height = request.args.size() == 2 ? request.args.at(1).toInt() : 0;
        if (window == nullptr || width < 100 || height < 100) return reply.fail(u"resize <width> <height>: at least 100 x 100"_s);
        window->resize(width, height);
        reply.ok({{u"width"_s, window->width()}, {u"height"_s, window->height()}, {u"scale"_s, window->devicePixelRatio()}});
    });
    control.add(u"windows"_s, u"windows: the app's open windows (plugin windows, the mixer) by title"_s, [](const DevRequest&, const Reply& reply) {
        QJsonArray list;
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            if (!window->isVisible()) continue;
            const QRect frame = window->geometry();
            list.append(QJsonObject{{u"title"_s, window->title()}, {u"x"_s, frame.x()}, {u"y"_s, frame.y()}, {u"w"_s, frame.width()}, {u"h"_s, frame.height()}});
        }
        reply.ok({{u"windows"_s, list}});
    });
    control.add(u"shot"_s, u"shot <file.png> [name | window:<title>]: a picture of the window, one item, or another window"_s,
                [&control, named](const DevRequest& request, const Reply& reply) {
        QQuickWindow* window = control.context().window;
        const QString file = QFileInfo(argText(request, 0)).absoluteFilePath();
        if (argText(request, 0).isEmpty() || window == nullptr) return reply.fail(u"shot <file.png> [name | window:<title>]"_s);
        QImage picture;
        const QString which = argText(request, 1);
        if (which.startsWith(u"window:"_s)) { // a plugin's own window: as the screen shows it
            const QString title = which.mid(7);
            const auto windows = QGuiApplication::topLevelWindows();
            const auto found = std::ranges::find_if(windows, [&title](const QWindow* w) { return w->isVisible() && w->title() == title; });
            if (found == windows.end()) return reply.fail(u"No open window titled %1 (windows lists them)"_s.arg(title));
            QScreen* screen = (*found)->screen();
            if (screen == nullptr) return reply.fail(u"%1 is on no screen"_s.arg(title));
            picture = screen->grabWindow((*found)->winId()).toImage();
        } else {
            picture = window->grabWindow();
            if (!which.isEmpty()) {
                QString error;
                auto* item = qobject_cast<QQuickItem*>(itemOf(named(request, 1, error)));
                if (item == nullptr) return reply.fail(error.isEmpty() ? u"%1 shows nothing"_s.arg(which) : error);
                const QRectF rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                const qreal scale = window->devicePixelRatio();
                picture = picture.copy(QRectF(rect.topLeft() * scale, rect.size() * scale).toAlignedRect());
            }
        }
        if (picture.isNull()) return reply.fail(u"The picture came out empty"_s);
        if (!QDir().mkpath(QFileInfo(file).path()) || !picture.save(file)) return reply.fail(u"Could not save %1"_s.arg(QDir::toNativeSeparators(file)));
        reply.ok({{u"file"_s, QDir::toNativeSeparators(file)}, {u"width"_s, picture.width()}, {u"height"_s, picture.height()}});
    });
    control.add(u"plugin-window"_s, u"plugin-window <channel> [effect]: opens a channel's instrument (or effect) window"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        if (request.args.isEmpty()) return reply.fail(u"plugin-window <channel> [effect]"_s);
        const int channel = request.args.at(0).toInt(-1);
        QString error;
        if (request.args.size() == 1) {
            QObject* windows = resolve(control, u"instrumentWindows"_s, error);
            if (windows == nullptr) return reply.fail(error);
            if (const auto opened = callMethod(*windows, "openFor", QJsonArray{channel}); !opened) return reply.fail(opened.error().message);
        } else {
            QObject* windows = resolve(control, u"effectWindows"_s, error);
            if (windows == nullptr) return reply.fail(error);
            bool opened = false;
            const int effect = request.args.at(1).toInt(-1);
            if (!QMetaObject::invokeMethod(windows, "open", Q_RETURN_ARG(bool, opened), Q_ARG(int, channel), Q_ARG(int, effect),
                                           Q_ARG(QWindow*, control.context().window)) || !opened) {
                return reply.fail(u"Channel %1's effect %2 has no window to open (see log)"_s.arg(channel).arg(effect));
            }
        }
        reply.ok({{u"note"_s, u"opening: windows lists it once it is up; shot <file> window:<title> takes it"_s}});
    });
}

} // namespace gigchain::devcontrol
