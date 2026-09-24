#include "PluginEditorHost.h"

#include "WindowCapture.h"

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
    m_captureTimer.setSingleShot(true);
    m_captureTimer.setInterval(1500);
    connect(&m_captureTimer, &QTimer::timeout, this, &PluginEditorHost::captureArtworkIfMissing);
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

    auto child = new QWindow(host); // Qt-owned native child of the main window
    child->setFlag(Qt::FramelessWindowHint);
    child->create();
    std::unique_ptr<engine::IPluginEditor> editor = std::move(*created);
    // Scale before opening too: some plugins size their window from it. Whether
    // the plugin supports zoom is checked after attach (m_scalable).
    (void)editor->setContentScale(host->devicePixelRatio());
    if (auto attached = editor->attach(static_cast<quintptr>(child->winId())); !attached) {
        child->deleteLater();
        m_service->reportFailure(attached.error().message); // already logged by the editor; now shown too
        emit editorChanged();
        return;
    }
    m_editor = std::move(editor);
    m_child = child;
    m_zoom = 1.0;
    m_scalable = !m_editor->canResize() && m_editor->setContentScale(host->devicePixelRatio());
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
    m_pluginId = m_service->selectedPluginId();
    if (!m_service->artwork().has(m_pluginId)) m_captureTimer.start();
    emit editorChanged();
}

void PluginEditorHost::captureArtworkIfMissing()
{
    if (!m_editor || !m_child || !m_child->isVisible() || !m_service || m_pluginId.isEmpty()) return;
    if (m_service->artwork().has(m_pluginId)) return;
    // Capture the part of the main window the plugin occupies.
    const double dpr = m_child->devicePixelRatio();
    const QRect logical = m_child->geometry();
    const QRect physical(static_cast<int>(logical.x() * dpr), static_cast<int>(logical.y() * dpr),
                         static_cast<int>(logical.width() * dpr), static_cast<int>(logical.height() * dpr));
    const QImage image = captureWindow(window(), physical);
    if (image.isNull()) return; // logged by captureWindow
    (void)m_service->artwork().store(m_pluginId, image); // a failure is logged by the cache
}

void PluginEditorHost::teardown()
{
    m_followTimer.stop();
    m_captureTimer.stop();
    m_pluginId.clear();
    if (m_editor) {
        m_editor->detach(); // must happen before its window is destroyed
        m_editor.reset();
    }
    if (m_child) {
        m_child->hide();
        m_child->deleteLater();
        m_child = nullptr;
    }
    m_editorSize = {};
    m_placedArea = {};
}

void PluginEditorHost::place()
{
    if (!m_editor || !m_child || window() == nullptr) return;
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
    // Fixed editors (e.g. Arturia) keep their own size and are centred;
    // they zoom from their own menu and OpenStage follows the resize.
    const double width = std::min(area.width(), m_editorSize.width() / dpr);
    const double height = std::min(area.height(), m_editorSize.height() / dpr);
    const double x = area.x() + std::max(0.0, (area.width() - width) / 2.0);
    const double y = area.y() + std::max(0.0, (area.height() - height) / 2.0);
    m_child->setGeometry(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)),
                         static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height)));
}

void PluginEditorHost::updateVisibility()
{
    if (!m_child) return;
    const bool show = isVisible() && !m_suspended && width() > 0 && height() > 0;
    if (show) {
        m_placedArea = {};
        place();
        m_child->show();
    } else {
        m_child->hide();
    }
}

} // namespace openstage::ui
