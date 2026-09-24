#include "PluginEditorHost.h"

#include <QLoggingCategory>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace openstage::ui {

PluginEditorHost::PluginEditorHost(QQuickItem* parent) : QQuickItem(parent)
{
    m_followTimer.setInterval(50);
    connect(&m_followTimer, &QTimer::timeout, this, &PluginEditorHost::place);
    m_fitTimer.setSingleShot(true);
    m_fitTimer.setInterval(700); // the window has stopped resizing
    connect(&m_fitTimer, &QTimer::timeout, this, [this] {
        if (!m_editor || !m_service || !m_fixedSize || window() == nullptr) return;
        const double dpr = window()->devicePixelRatio();
        const QSize area(static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
        m_service->fitToArea(m_editorSize, area);
    });

}

PluginEditorHost::~PluginEditorHost()
{
    teardown();
}

void PluginEditorHost::setService(EditorService* service)
{
    if (m_service == service) return;
    if (m_service) disconnect(m_service, nullptr, this, nullptr);
    m_service = service;
    if (m_service) connect(m_service, &EditorService::targetChanged, this, &PluginEditorHost::rebuild);
    emit serviceChanged();
    rebuild();
}

void PluginEditorHost::setSuspended(bool suspended)
{
    if (m_suspended == suspended) return;
    m_suspended = suspended;
    emit suspendedChanged();
    updateVisibility();
}

QString PluginEditorHost::emptyReason() const
{
    return m_service ? m_service->emptyReason() : QString();
}

void PluginEditorHost::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    place();
}

void PluginEditorHost::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange) {
        // Moving to another window (or out of one): start over there.
        rebuild();
    } else if (change == ItemVisibleHasChanged) {
        updateVisibility();
    }
}

void PluginEditorHost::rebuild()
{
    teardown();
    QQuickWindow* host = window();
    if (!m_service || host == nullptr) {
        emit editorChanged();
        return;
    }

    auto created = m_service->createForSelection();
    if (!created || !*created) {
        emit editorChanged(); // error already shown and logged, or nothing to show
        return;
    }

    // viewport (clips) -> child (the plugin draws here); both Qt-owned by the main window.
    auto viewport = new QWindow(host);
    viewport->setFlag(Qt::FramelessWindowHint);
    viewport->create();
    auto child = new QWindow(viewport);
    child->setFlag(Qt::FramelessWindowHint);
    child->create();
    std::unique_ptr<engine::IPluginEditor> editor = std::move(*created);
    // Scale before opening too: some plugins size their window from it. Whether
    // the plugin supports zoom is checked after attach (m_scalable).
    (void)editor->setContentScale(host->devicePixelRatio());
    if (auto attached = editor->attach(static_cast<quintptr>(child->winId())); !attached) {
        viewport->deleteLater(); // deletes its child window too
        m_service->reportFailure(attached.error().message); // already logged by the editor; now shown too
        emit editorChanged();
        return;
    }
    m_editor = std::move(editor);
    m_viewport = viewport;
    m_child = child;
    m_scroll = {};
    m_zoom = 1.0;
    m_scalable = !m_editor->canResize() && m_editor->setContentScale(host->devicePixelRatio());
    m_fixedSize = !m_editor->canResize() && !m_scalable;
    m_editorSize = m_editor->preferredSize();
    m_baseSize = m_editorSize;
    m_editor->setResizeHandler([this](QSize requested) {
        // The plugin changed its own size (a panel opened, or its own zoom
        // menu): follow it, and treat it as the new 100 % for scalable ones.
        m_editorSize = requested;
        if (m_scalable && m_zoom > 0.0) {
            m_baseSize = QSize(static_cast<int>(requested.width() / m_zoom), static_cast<int>(requested.height() / m_zoom));
        }
        m_placedArea = {}; // refit
        place();
    });
    m_placedArea = {};
    place();
    updateVisibility();
    m_followTimer.start();
    emit editorChanged();
}

void PluginEditorHost::teardown()
{
    m_followTimer.stop();
    m_fitTimer.stop();
    m_fixedSize = false;
    if (m_editor) {
        m_editor->detach(); // must happen before its window is destroyed
        m_editor.reset();
    }
    if (m_viewport) {
        m_viewport->hide();
        m_viewport->deleteLater(); // deletes the plugin's window with it
        m_viewport = nullptr;
    }
    m_child = nullptr;
    m_placement = {};
    emit placementChanged();
    m_editorSize = {};
    m_placedArea = {};
}

void PluginEditorHost::place()
{
    if (!m_editor || !m_child || !m_viewport || window() == nullptr) return;
    const QRectF area = mapRectToScene(boundingRect());
    if (area == m_placedArea) return;
    m_placedArea = area;

    const double dpr = window()->devicePixelRatio();
    if (m_editor->canResize()) {
        // Resizable editors fill the area (the plugin may adjust the size).
        const QSize wanted(static_cast<int>(area.width() * dpr), static_cast<int>(area.height() * dpr));
        m_editorSize = m_editor->setSize(wanted);
    } else if (m_scalable && !m_baseSize.isEmpty()) {
        // Scalable editors zoom to fit, within sensible limits.
        constexpr double kMinZoom = 0.4;
        constexpr double kMaxZoom = 1.5;
        const double fit = std::min(area.width() * dpr / m_baseSize.width(), area.height() * dpr / m_baseSize.height());
        const double zoom = std::clamp(fit, kMinZoom, kMaxZoom);
        if (std::abs(zoom - m_zoom) > 0.01 && m_editor->setContentScale(dpr * zoom)) m_zoom = zoom;
        m_editorSize = m_editor->preferredSize();
    }
    // Editors that cannot shrink to the area (e.g. Arturia, which only zooms
    // from its own menu) keep their size and scroll inside a clipping viewport.
    const QSizeF editorSize(m_editorSize.width() / dpr, m_editorSize.height() / dpr);
    if (m_fixedSize) m_fitTimer.start(); // restarts while the area keeps changing
    m_placement = placeEditor(area, editorSize, m_scroll);
    m_viewport->setGeometry(m_placement.viewport.toAlignedRect());
    const QPointF inside = m_placement.editor.topLeft() - m_placement.viewport.topLeft();
    m_child->setGeometry(static_cast<int>(std::lround(inside.x())), static_cast<int>(std::lround(inside.y())),
                         static_cast<int>(std::lround(editorSize.width())),
                         static_cast<int>(std::lround(editorSize.height())));
    emit placementChanged();
}

void PluginEditorHost::setScrollX(double x)
{
    if (std::abs(x - m_scroll.x()) < 0.5) return;
    m_scroll.setX(x);
    m_placedArea = {};
    place();
}

void PluginEditorHost::setScrollY(double y)
{
    if (std::abs(y - m_scroll.y()) < 0.5) return;
    m_scroll.setY(y);
    m_placedArea = {};
    place();
}

void PluginEditorHost::updateVisibility()
{
    if (!m_viewport || !m_child) return;
    const bool show = isVisible() && !m_suspended && width() > 0 && height() > 0;
    if (show) {
        m_placedArea = {};
        place();
        m_child->show();
        m_viewport->show();
    } else {
        m_viewport->hide();
    }
}

} // namespace openstage::ui
