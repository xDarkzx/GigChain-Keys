#pragma once

#include "EditorService.h"

#include "gigchain/engine/IPluginEditor.h"

#include <QPointer>
#include <QQuickItem>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace gigchain::ui {

// Shows the selected channel's plugin editor inside this item's area, as
// Audacity 4's VstView (muse/framework/vst/qml/Muse/Vst/vstview.cpp): one
// native window, the plugin's own size but never bigger than the area, so it
// never covers anything else; the plugin is told the size it got, and one
// that cannot shrink is clipped by its window. Sized again when the plugin
// asks, when the area changes (maximize, restore, the mixer divider) and on
// another screen. Native windows sit above Qt Quick content, so the window is
// hidden whenever the item is invisible or `suspended` is set (e.g. while a
// dialog is open).
class PluginEditorHost : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(EditorService* service READ service WRITE setService NOTIFY serviceChanged)
    Q_PROPERTY(bool suspended READ isSuspended WRITE setSuspended NOTIFY suspendedChanged)
    Q_PROPERTY(bool hasEditor READ hasEditor NOTIFY editorChanged)
    Q_PROPERTY(QString title READ title NOTIFY editorChanged)
    Q_PROPERTY(QString emptyReason READ emptyReason NOTIFY editorChanged)

public:
    explicit PluginEditorHost(QQuickItem* parent = nullptr);
    ~PluginEditorHost() override;
    PluginEditorHost(const PluginEditorHost&) = delete;
    PluginEditorHost& operator=(const PluginEditorHost&) = delete;
    PluginEditorHost(PluginEditorHost&&) = delete;
    PluginEditorHost& operator=(PluginEditorHost&&) = delete;

    [[nodiscard]] EditorService* service() const { return m_service; }
    void setService(EditorService* service);
    [[nodiscard]] bool isSuspended() const { return m_suspended; }
    void setSuspended(bool suspended);
    [[nodiscard]] bool hasEditor() const { return m_editor != nullptr; }
    [[nodiscard]] QString title() const { return m_editor ? m_editor->title() : QString(); }
    [[nodiscard]] QString emptyReason() const;

signals:
    void serviceChanged();
    void suspendedChanged();
    void editorChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void rebuild();
    void teardown();
    void place();
    void updateVisibility();
    // The window side of VstView::resizeView: the size the window takes for
    // the size the plugin wants (physical pixels in and out).
    QSize fit(QSize wanted);

    // VstView::nativeEventFilter: the plugin's window is never erased (no flicker).
    class EraseFilter;
    std::unique_ptr<EraseFilter> m_eraseFilter;

    QPointer<EditorService> m_service;
    std::unique_ptr<engine::IPluginEditor> m_editor;
    QPointer<QWindow> m_window; // the plugin draws here; Qt-owned by the main window
    QSize m_windowSize;         // the window's size, in the screen's units
    // Every frame the scene changes (afterAnimating) the window follows its
    // item: ancestors move it without a geometry change (splitter drags).
    QMetaObject::Connection m_frameConnection;
    QMetaObject::Connection m_screenConnection; // another screen: its scaling
    bool m_stale = false; // the editor to show changed while hidden: open it when shown
    bool m_suspended = false;
};

} // namespace gigchain::ui
