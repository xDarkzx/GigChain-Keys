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
    editor->setContentScale(host->devicePixelRatio());
    if (auto attached = editor->attach(static_cast<quintptr>(child->winId())); !attached) {
        child->deleteLater();
        m_service->reportFailure(attached.error().message); // already logged by the editor; now shown too
        emit editorChanged();
        return;
    }
    m_editor = std::move(editor);
    m_child = child;
    m_editorSize = m_editor->preferredSize();
    m_editor->setResizeHandler([this](QSize requested) {
        m_editorSize = requested;
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
    }
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
