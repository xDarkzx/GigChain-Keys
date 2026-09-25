#include "PluginEditorHost.h"

#include "FreezeWatchdog.h"

#include "gigchain/core/Checks.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QQuickWindow>

#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {

PluginEditorHost::PluginEditorHost(QQuickItem* parent) : QQuickItem(parent) {}

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
    if (!m_suspended && m_stale) rebuild(); // the song changed while hidden
    else updateVisibility();
}

QString PluginEditorHost::emptyReason() const
{
    return m_service ? m_service->emptyReason() : QString();
}

void PluginEditorHost::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    // An editor opened before the layout gave this area a size (the setlist
    // loads before the main window appears) is shown once it has one.
    if (m_viewport && !m_viewport->isVisible()) updateVisibility();
    else place();
}

void PluginEditorHost::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange) {
        // Moving to another window (or out of one): start over there.
        rebuild();
    } else if (change == ItemVisibleHasChanged) {
        if (isVisible() && !m_suspended && m_stale) rebuild(); // the song changed while hidden
        else updateVisibility();
    }
}

void PluginEditorHost::rebuild()
{
    GC_ONLY_MAIN_THREAD();
    QElapsedTimer timer;
    timer.start();
    teardown();
    const qint64 closing = timer.elapsed();
    QQuickWindow* host = window();
    if (!m_service || host == nullptr) {
        emit editorChanged();
        return;
    }
    // Opening a plugin's window is slow (Arturia's take ~3 s): only open one
    // that can be seen. Switching songs on the Chart tab opens nothing; the
    // window opens when the Instrument tab is shown.
    if (m_suspended || !isVisible()) {
        m_stale = true;
        emit editorChanged();
        return;
    }
    m_stale = false;

    FreezeWatchdog::mark(u"opening a plugin window"_s);
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
    m_editorSize = m_editor->preferredSize();
    // The host never sizes the plugin: only the plugin changes its size (its
    // resize corner, its size menu, a panel opening), and the window follows.
    m_editor->setResizeHandler([this](QSize requested) {
        qCInfo(lcUi).noquote() << m_editor->title() << "resized itself to" << requested.width() << "x"
                               << requested.height();
        m_editorSize = requested;
        m_placedArea = {};
        place();
    });
    m_placedArea = {};
    place();
    updateVisibility();
    qCInfo(lcUi).noquote() << "Plugin window" << m_editor->title() << ": closing the previous" << closing
                           << "ms, opening" << timer.elapsed() - closing << "ms";
    m_frameConnection = connect(host, &QQuickWindow::afterAnimating, this, &PluginEditorHost::place);
    emit editorChanged();
}

void PluginEditorHost::teardown()
{
    GC_ONLY_MAIN_THREAD();
    disconnect(m_frameConnection);
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

    // The editor at its own size, centred, scrolling inside a clipping
    // viewport when it is bigger than the area.
    const double dpr = window()->devicePixelRatio();
    const QSizeF editorSize(m_editorSize.width() / dpr, m_editorSize.height() / dpr);
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

} // namespace gigchain::ui
