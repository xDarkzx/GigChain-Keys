#include "PluginEditorHost.h"

#include "FreezeWatchdog.h"

#include "gigchain/core/Checks.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QQuickWindow>


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
    if (m_window && !m_window->isVisible()) updateVisibility();
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

    // One window, owned by the main window; the plugin draws in it.
    auto* pluginWindow = new QWindow(host);
    pluginWindow->setFlag(Qt::FramelessWindowHint);
    pluginWindow->create();
    std::unique_ptr<engine::IPluginEditor> editor = std::move(*created);
    if (auto attached = editor->attach(static_cast<quintptr>(pluginWindow->winId())); !attached) {
        pluginWindow->deleteLater();
        m_service->reportFailure(attached.error().message); // already logged by the editor; now shown too
        emit editorChanged();
        return;
    }
    m_editor = std::move(editor);
    m_window = pluginWindow;
    m_editorSize = m_editor->preferredSize();
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
    if (m_window) {
        m_window->hide();
        m_window->deleteLater();
        m_window = nullptr;
    }
    m_editorSize = {};
}

void PluginEditorHost::place()
{
    if (!m_editor || !m_window || window() == nullptr) return;
    const QPointF topLeft = mapToScene(QPointF(0, 0));
    const double dpr = window()->devicePixelRatio();
    const QRect wanted(qRound(topLeft.x()), qRound(topLeft.y()), qRound(m_editorSize.width() / dpr),
                       qRound(m_editorSize.height() / dpr));
    if (m_window->geometry() != wanted) m_window->setGeometry(wanted);
}

void PluginEditorHost::updateVisibility()
{
    if (!m_window) return;
    if (isVisible() && !m_suspended && width() > 0 && height() > 0) {
        place();
        m_window->show();
    } else {
        m_window->hide();
    }
}

} // namespace gigchain::ui
