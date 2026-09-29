#include "PluginEditorHost.h"

#include "FreezeWatchdog.h"

#include "gigchain/core/Checks.h"
#include "gigchain/platform/Windows.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QScreen>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {

PluginEditorHost::PluginEditorHost(QQuickItem* parent) : QQuickItem(parent) {}

PluginEditorHost::~PluginEditorHost()
{
    teardown();
}

void PluginEditorHost::setService(EditorService* to)
{
    if (m_service == to) return;
    if (m_service) disconnect(m_service, nullptr, this, nullptr);
    m_service = to;
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
    // The room changed (maximize, restore, the mixer divider, or the layout
    // giving it its first size after the plugin opened): fitted again.
    if (m_editor && newGeometry.size() != oldGeometry.size()) m_editor->updateGeometry();
    if (m_window && !m_window->isVisible()) updateVisibility(); // shown once it has a size
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
    // As VstView::init: the screen's scaling, then the frame, then attach.
    editor->setContentScale(host->devicePixelRatio());
    editor->setFitter([this](QSize wanted) { return fit(wanted); });
    m_window = pluginWindow;
    if (auto attached = editor->attach(static_cast<quintptr>(pluginWindow->winId())); !attached) {
        m_window = nullptr;
        pluginWindow->deleteLater();
        m_service->reportFailure(attached.error().message); // already logged by the editor; now shown too
        emit editorChanged();
        return;
    }
    m_editor = std::move(editor);
    m_eraseFilter = platform::makeNoFlickerFilter(pluginWindow->winId());
    if (m_eraseFilter) QCoreApplication::instance()->installNativeEventFilter(m_eraseFilter.get());
    m_editor->updateGeometry(); // VstView::updateViewGeometry
    updateVisibility();
    qCInfo(lcUi).noquote() << "Plugin window" << m_editor->title() << ": closing the previous" << closing
                           << "ms, opening" << timer.elapsed() - closing << "ms";
    m_frameConnection = connect(host, &QQuickWindow::afterAnimating, this, &PluginEditorHost::place);
    // Another screen, other scaling (VstView: screenChanged).
    m_screenConnection = connect(host, &QWindow::screenChanged, this, [this](QScreen* screen) {
        if (!m_editor || screen == nullptr) return;
        m_editor->setContentScale(screen->devicePixelRatio());
        m_editor->updateGeometry();
    });
    emit editorChanged();
}

void PluginEditorHost::teardown()
{
    GC_ONLY_MAIN_THREAD();
    disconnect(m_frameConnection);
    disconnect(m_screenConnection);
    if (m_eraseFilter) {
        QCoreApplication::instance()->removeNativeEventFilter(m_eraseFilter.get());
        m_eraseFilter.reset();
    }
    if (m_editor) {
        m_editor->detach(); // must happen before its window is destroyed
        m_editor.reset();
    }
    if (m_window) {
        m_window->hide();
        m_window->deleteLater();
        m_window = nullptr;
    }
    m_windowSize = {};
}

QSize PluginEditorHost::fit(QSize wanted)
{
    if (!m_window || window() == nullptr || wanted.isEmpty()) return {};
    const double dpr = window()->devicePixelRatio();
    if (width() < 1 || height() < 1) {
        m_windowSize = {}; // no room yet: fitted when the layout gives it one
        updateVisibility();
        return {};
    }
    // The whole plugin, as big as the room allows, same shape, never bigger
    // than its own size: nothing cut off, nothing around it covered. Sized in
    // the screen's units, so the window and the plugin are the same pixels.
    const double ownWidth = wanted.width() / dpr;
    const double ownHeight = wanted.height() / dpr;
    const double scale = std::min({1.0, width() / ownWidth, height() / ownHeight});
    m_windowSize = QSize(std::max(1, static_cast<int>(std::floor(ownWidth * scale))),
                         std::max(1, static_cast<int>(std::floor(ownHeight * scale))));
    place();
    return {qRound(m_windowSize.width() * dpr), qRound(m_windowSize.height() * dpr)};
}

void PluginEditorHost::place()
{
    if (!m_window || window() == nullptr || m_windowSize.isEmpty()) return;
    // At the top of the area, centred across it.
    const QPointF topLeft = mapToScene(QPointF(0, 0));
    const int side = std::max(0, (static_cast<int>(width()) - m_windowSize.width()) / 2);
    const QRect wanted(qRound(topLeft.x()) + side, qRound(topLeft.y()), m_windowSize.width(), m_windowSize.height());
    if (m_window->geometry() != wanted) m_window->setGeometry(wanted);
}

void PluginEditorHost::updateVisibility()
{
    if (!m_window) return;
    if (isVisible() && !m_suspended && !m_windowSize.isEmpty() && width() > 0 && height() > 0) {
        place();
        m_window->show();
    } else {
        m_window->hide();
    }
}

} // namespace gigchain::ui
