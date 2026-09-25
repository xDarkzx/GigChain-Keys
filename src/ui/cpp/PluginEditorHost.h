#pragma once

#include "EditorService.h"

#include "gigchain/engine/IPluginEditor.h"

#include <QPointer>
#include <QQuickItem>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace gigchain::ui {

// Shows the selected channel's plugin editor at this item's top-left corner.
// One native window, the plugin's own size: the host never sizes the plugin;
// when the plugin changes its size (its resize handle, its size menu) the
// window takes that size. Native windows sit above Qt Quick content, so the
// window is hidden whenever the item is invisible or `suspended` is set
// (e.g. while a dialog is open).
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

    QPointer<EditorService> m_service;
    std::unique_ptr<engine::IPluginEditor> m_editor;
    QPointer<QWindow> m_window; // the plugin draws here; Qt-owned by the main window
    QSize m_editorSize;         // physical pixels, as the plugin says
    // Every frame the scene changes (afterAnimating) the window follows its
    // item: ancestors move it without a geometry change (splitter drags).
    QMetaObject::Connection m_frameConnection;
    bool m_stale = false; // the editor to show changed while hidden: open it when shown
    bool m_suspended = false;
};

} // namespace gigchain::ui
