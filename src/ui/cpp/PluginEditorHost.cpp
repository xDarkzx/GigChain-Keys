#include "PluginEditorHost.h"

#include "FreezeWatchdog.h"

#include "gigchain/core/Checks.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QScreen>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {

// As VstView::nativeEventFilter: Windows is told the plugin's window (and
// the plugin's own windows inside it) are already erased, so they do not
// flicker while moved or sized.
class PluginEditorHost::EraseFilter final : public QAbstractNativeEventFilter
{
public:
    explicit EraseFilter(HWND window) : m_window(window) {}

    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override
    {
        if (type != "windows_generic_MSG") return false;
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message != WM_ERASEBKGND || msg->hwnd == nullptr) return false;
        if (msg->hwnd != m_window && IsChild(m_window, msg->hwnd) == FALSE) return false;
        *result = 1; // "already erased"
        return true;
    }

private:
    HWND m_window;
};

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
    if (m_window && !m_window->isVisible()) {
        updateVisibility();
    } else if (m_editor && newGeometry.size() != oldGeometry.size()) {
        // The room changed (maximize, restore, the mixer divider): the
        // plugin's own size again, never bigger than the new room.
        m_editor->updateGeometry();
    } else {
        place();
    }
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
    m_eraseFilter = std::make_unique<EraseFilter>(reinterpret_cast<HWND>(pluginWindow->winId()));
    QCoreApplication::instance()->installNativeEventFilter(m_eraseFilter.get());
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
    if (!m_window || window() == nullptr) return wanted;
    const double dpr = window()->devicePixelRatio();
    // VstView::resizeView: the wanted size without the screen's scaling, no
    // bigger than the room: the plugin never covers anything around it.
    const int roomWidth = std::max(1, static_cast<int>(width()));
    const int roomHeight = std::max(1, static_cast<int>(height()));
    m_windowSize = QSize(std::min(qRound(wanted.width() / dpr), roomWidth),
                         std::min(qRound(wanted.height() / dpr), roomHeight));
    place();
    return QSize(qRound(m_windowSize.width() * dpr), qRound(m_windowSize.height() * dpr));
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
    if (isVisible() && !m_suspended && width() > 0 && height() > 0) {
        place();
        m_window->show();
    } else {
        m_window->hide();
    }
}

} // namespace gigchain::ui
