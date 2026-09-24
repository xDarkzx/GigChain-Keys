#include "ArtworkBuilder.h"

#include "WindowCapture.h"

#include "openstage/engine/IEngine.h"

#include <QGuiApplication>
#include <QScreen>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace openstage::ui {

ArtworkBuilder::ArtworkBuilder(engine::IEngine& engine, ArtworkCache& cache, QObject* parent)
    : QObject(parent), m_engine(engine), m_cache(cache)
{
}

ArtworkBuilder::~ArtworkBuilder()
{
    closeCurrent();
}

void ArtworkBuilder::start()
{
    if (m_running) return;
    auto plugins = m_engine.availablePlugins();
    std::stable_sort(plugins.begin(), plugins.end(), [](const auto& a, const auto& b) {
        return a.kind == engine::PluginKind::Instrument && b.kind != engine::PluginKind::Instrument;
    });
    m_queue.clear();
    m_names.clear();
    for (const auto& plugin : plugins) {
        if (m_cache.has(plugin.id)) continue;
        m_queue << plugin.id;
        m_names << plugin.name;
    }
    m_total = static_cast<int>(m_queue.size());
    m_done = 0;
    m_running = true;
    qCInfo(lcUi) << "Building artwork for" << m_total << "plugins";
    emit progressChanged();
    QTimer::singleShot(0, this, &ArtworkBuilder::next);
}

void ArtworkBuilder::cancel()
{
    if (!m_running) return;
    m_queue.clear();
    m_names.clear();
    closeCurrent();
    m_running = false;
    qCInfo(lcUi) << "Artwork building cancelled after" << m_done << "of" << m_total;
    emit progressChanged();
    emit finished();
}

void ArtworkBuilder::next()
{
    closeCurrent();
    if (!m_running) return;
    if (m_queue.isEmpty()) {
        m_running = false;
        m_current.clear();
        qCInfo(lcUi) << "Artwork building finished:" << m_done << "plugins processed";
        emit progressChanged();
        emit finished();
        return;
    }
    m_currentId = m_queue.takeFirst();
    m_current = m_names.takeFirst();
    emit progressChanged();

    auto editor = m_engine.createEditorForPlugin(m_currentId);
    if (!editor || !*editor) {
        // Failures are logged by the engine; plugins without editors have no picture.
        ++m_done;
        emit progressChanged();
        QTimer::singleShot(0, this, &ArtworkBuilder::next);
        return;
    }

    auto window = new QWindow(); // top-level, deleted via deleteLater in closeCurrent()
    // Plugins only draw into windows that are on a screen, so the window sits
    // on screen but practically invisible (1 % opacity), behind other windows
    // and ignoring the mouse.
    window->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus |
                     Qt::WindowTransparentForInput | Qt::WindowStaysOnBottomHint);
    window->setOpacity(0.01);
    window->create();
    const double dpr = window->devicePixelRatio();
    const QSize size = (*editor)->preferredSize();
    const QRect screen = window->screen() != nullptr ? window->screen()->availableGeometry() : QRect(0, 0, 800, 600);
    window->setGeometry(screen.x(), screen.y(), std::max(1, static_cast<int>(size.width() / dpr)),
                        std::max(1, static_cast<int>(size.height() / dpr)));
    if (auto attached = (*editor)->attach(static_cast<quintptr>(window->winId())); !attached) {
        window->deleteLater(); // the editor logged why
        ++m_done;
        emit progressChanged();
        QTimer::singleShot(0, this, &ArtworkBuilder::next);
        return;
    }
    window->show();
    m_editor = std::move(*editor);
    m_window = window;
    QTimer::singleShot(kDrawWaitMs, this, &ArtworkBuilder::capture);
}

void ArtworkBuilder::capture()
{
    if (!m_running || !m_window) return;
    const QImage image = captureWindow(m_window);
    if (image.isNull()) {
        qCWarning(lcUi).noquote() << "No picture could be taken of" << m_current;
    } else {
        (void)m_cache.store(m_currentId, image); // a failure is logged by the cache
    }
    ++m_done;
    emit progressChanged();
    QTimer::singleShot(150, this, &ArtworkBuilder::next);
}

void ArtworkBuilder::closeCurrent()
{
    if (m_editor) {
        m_editor->detach(); // before its window goes
        m_editor.reset();
    }
    if (m_window) {
        m_window->hide();
        m_window->deleteLater();
        m_window = nullptr;
    }
}

} // namespace openstage::ui
