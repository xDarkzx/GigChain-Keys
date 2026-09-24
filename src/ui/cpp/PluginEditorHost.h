#pragma once

#include "EditorService.h"

#include "openstage/engine/IPluginEditor.h"

#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace openstage::ui {

// Shows the selected channel's plugin editor inside this item's area. The
// plugin draws into a native child window placed exactly over the item
// (native windows always sit above Qt Quick content, so the window is hidden
// whenever the item is invisible or `suspended` is set, e.g. while a dialog
// is open).
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
    void captureArtworkIfMissing();

    QPointer<EditorService> m_service;
    std::unique_ptr<engine::IPluginEditor> m_editor;
    QPointer<QWindow> m_child; // owned by the main window (Qt parent)
    QSize m_editorSize;        // physical pixels, as the plugin reports
    QSize m_baseSize;          // physical pixels at 100 % zoom (scalable editors)
    bool m_scalable = false;   // the plugin accepts host zoom
    double m_zoom = 1.0;       // current zoom applied to a scalable editor
    QRectF m_placedArea;       // last scene rect the editor was fitted to
    QTimer m_followTimer;      // catches moves of ancestors (splitter drags)
    bool m_suspended = false;
    QString m_pluginId;        // plugin whose editor is shown
    QTimer m_captureTimer;     // waits for the plugin to draw before taking its picture
};

} // namespace openstage::ui
