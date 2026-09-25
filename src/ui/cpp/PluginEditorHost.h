#pragma once

#include "EditorPlacement.h"
#include "EditorService.h"

#include "gigchain/engine/IPluginEditor.h"

#include <QPointer>
#include <QQuickItem>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace gigchain::ui {

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
    // Scrolling for editors bigger than the area (plugins that cannot be scaled).
    Q_PROPERTY(bool scrollHorizontally READ scrollHorizontally NOTIFY placementChanged)
    Q_PROPERTY(bool scrollVertically READ scrollVertically NOTIFY placementChanged)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY placementChanged)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY placementChanged)
    Q_PROPERTY(double viewportWidth READ viewportWidth NOTIFY placementChanged)
    Q_PROPERTY(double viewportHeight READ viewportHeight NOTIFY placementChanged)
    Q_PROPERTY(double scrollX READ scrollX WRITE setScrollX NOTIFY placementChanged)
    Q_PROPERTY(double scrollY READ scrollY WRITE setScrollY NOTIFY placementChanged)

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
    [[nodiscard]] bool scrollHorizontally() const { return m_placement.scrollHorizontally; }
    [[nodiscard]] bool scrollVertically() const { return m_placement.scrollVertically; }
    [[nodiscard]] double contentWidth() const { return m_placement.contentSize.width(); }
    [[nodiscard]] double contentHeight() const { return m_placement.contentSize.height(); }
    [[nodiscard]] double viewportWidth() const { return m_placement.viewport.width(); }
    [[nodiscard]] double viewportHeight() const { return m_placement.viewport.height(); }
    [[nodiscard]] double scrollX() const { return m_placement.scroll.x(); }
    [[nodiscard]] double scrollY() const { return m_placement.scroll.y(); }
    void setScrollX(double x);
    void setScrollY(double y);

signals:
    void serviceChanged();
    void suspendedChanged();
    void editorChanged();
    void placementChanged();

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
    QPointer<QWindow> m_viewport; // clips the editor; owned by the main window (Qt parent)
    QPointer<QWindow> m_child;    // the plugin draws here; child of m_viewport
    QPointF m_scroll;             // requested scroll offset
    EditorPlacement m_placement;  // last applied placement
    QSize m_editorSize;        // physical pixels, as the plugin reports
    QSize m_baseSize;          // physical pixels at 100 % zoom (scalable editors)
    bool m_scalable = false;   // the plugin accepts host zoom
    double m_zoom = 1.0;       // current zoom applied to a scalable editor
    QRectF m_placedArea;       // last scene rect the editor was fitted to
    // Event-driven, no timers: every frame the scene changes (afterAnimating)
    // the editor follows its item (ancestors move it without a geometry
    // change, e.g. splitter drags).
    QMetaObject::Connection m_frameConnection;
    QMetaObject::Connection m_stateConnection;
    // Fixed-size editors (Arturia): asked to fit when the editor opens, on the
    // first layout after maximize / restore / full screen, and when a window
    // edge drag ends (WM_EXITSIZEMOVE). Never while a drag is in progress.
    bool m_fixedSize = false;
    bool m_stale = false; // the editor to show changed while hidden: open it when shown
    bool m_fitOnNextArea = false;
    void fitNow();
    class DragEndFilter;
    std::unique_ptr<DragEndFilter> m_dragEnd;
    bool m_suspended = false;
};

} // namespace gigchain::ui
