#include "InstrumentWindows.h"

#include "DocumentController.h"
#include "EditorService.h"

#include "gigchain/core/Checks.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QSettings>

#include <algorithm>
#include <iterator>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

const QString kOpenKey = u"instrumentWindow/open"_s;
const QString kGeometryKey = u"instrumentWindow/geometry"_s;
const QString kMaximizedKey = u"instrumentWindow/maximized"_s;
const QString kOnTopKey = u"instrumentWindow/onTop"_s;

// Enough of a window on a screen to grab its title bar and move it.
constexpr int kGrabWidth = 120;
constexpr int kGrabHeight = 40;

} // namespace

InstrumentWindows::InstrumentWindows(engine::IEngine& engine, DocumentController& document, EditorService& docked,
                                     QSettings& settings, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document), m_docked(docked), m_settings(settings),
      m_onTop(settings.value(kOnTopKey, true).toBool())
{
    m_docked.setElsewhereCheck([this] { return elsewhere(m_docked); });
    // Connected before any view is: the views asked again before they rebuild.
    connect(&m_docked, &EditorService::targetChanged, this, &InstrumentWindows::recheck);
    connect(&m_document, &DocumentController::selectedChannelChanged, this, &InstrumentWindows::recheck);
    connect(&m_document, &DocumentController::channelsChanged, this, &InstrumentWindows::recheck);
    connect(&m_document, &DocumentController::currentChanged, this, &InstrumentWindows::recheck);
    // As it was left: the linked window open on its screen.
    if (m_settings.value(kOpenKey, false).toBool()) create(std::nullopt);
}

InstrumentWindows::~InstrumentWindows()
{
    m_docked.setElsewhereCheck({}); // the tab's service outlives this
}

QList<QObject*> InstrumentWindows::windows() const
{
    QList<QObject*> open;
    for (const Window& w : m_windows) {
        if (!w.closing) open.append(w.service);
    }
    return open;
}

void InstrumentWindows::setOnTop(bool on)
{
    if (m_onTop == on) return;
    m_onTop = on;
    m_settings.setValue(kOnTopKey, on);
    emit onTopChanged();
}

EditorService* InstrumentWindows::create(const std::optional<core::ChannelId>& keep)
{
    GC_ONLY_MAIN_THREAD();
    auto* service = new EditorService(m_engine, m_document, this);
    service->pin(keep);
    m_windows.push_back({.service = service});
    service->setElsewhereCheck([this, service] { return elsewhere(*service); });
    connect(service, &EditorService::targetChanged, this, &InstrumentWindows::recheck);
    connect(service, &EditorService::linkedChanged, this, &InstrumentWindows::recheck);
    // The views losing their instrument to this one let it go before it opens.
    recheck();
    emit windowsChanged();
    return service;
}

EditorService* InstrumentWindows::linkedWindow() const
{
    const auto it = std::ranges::find_if(m_windows, [](const Window& w) { return !w.closing && w.service->linked(); });
    return it != m_windows.end() ? it->service : nullptr;
}

InstrumentWindows::Window* InstrumentWindows::find(QObject* window)
{
    const auto it = std::ranges::find_if(m_windows, [window](const Window& w) { return w.service == window; });
    return it != m_windows.end() ? &*it : nullptr;
}

EditorService* InstrumentWindows::showing(const EditorService& view) const
{
    const std::optional<core::ChannelId> target = view.targetId();
    if (!target) return nullptr;
    // Who comes first: windows kept on a channel (oldest first), then the
    // linked window, then the Instrument tab.
    std::vector<EditorService*> order;
    for (const Window& w : m_windows) {
        if (!w.closing && !w.service->linked()) order.push_back(w.service);
    }
    if (EditorService* linked = linkedWindow()) order.push_back(linked);
    for (EditorService* first : order) {
        if (first == &view) return nullptr; // nothing before it shows its instrument
        if (first->targetId() == target) return first;
    }
    return nullptr; // the tab, with no window showing its instrument
}

bool InstrumentWindows::elsewhere(const EditorService& view) const
{
    const auto it = std::ranges::find_if(m_windows, [&view](const Window& w) { return w.service == &view; });
    if (it != m_windows.end() && it->closing) return true;
    return showing(view) != nullptr;
}

void InstrumentWindows::recheck()
{
    std::vector<EditorService*> views{&m_docked};
    std::ranges::transform(m_windows, std::back_inserter(views), &Window::service);
    for (EditorService* view : views) {
        if (view->shownElsewhere()) view->recheckElsewhere();
    }
    for (EditorService* view : views) view->recheckElsewhere();
}

void InstrumentWindows::rememberOpen()
{
    m_settings.setValue(kOpenKey, linkedOpen());
}

void InstrumentWindows::popOut()
{
    if (EditorService* linked = linkedWindow()) {
        emit raiseRequested(linked);
        return;
    }
    create(std::nullopt);
    rememberOpen();
    qCInfo(lcUi) << "Instrument window opened, following the selected channel";
}

void InstrumentWindows::toggle()
{
    if (EditorService* linked = linkedWindow()) dock(linked);
    else popOut();
}

void InstrumentWindows::openFor(int channel)
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || channel < 0 || static_cast<std::size_t>(channel) >= patch->channels.size()) {
        qCWarning(lcUi) << "No instrument window: channel" << channel << "is not in the current patch";
        return;
    }
    const core::ChannelId id = patch->channels.at(static_cast<std::size_t>(channel)).id;
    const auto showingIt = std::ranges::find_if(m_windows, [&id](const Window& w) {
        return !w.closing && w.service->targetId() == id && !w.service->shownElsewhere();
    });
    if (showingIt != m_windows.end()) {
        emit raiseRequested(showingIt->service);
        return;
    }
    create(id);
    qCInfo(lcUi).noquote() << "Instrument window opened, kept on" << patch->channels.at(static_cast<std::size_t>(channel)).name;
}

void InstrumentWindows::close(QObject* window)
{
    GC_ONLY_MAIN_THREAD();
    Window* w = find(window);
    if (w == nullptr || w->closing) return;
    // Its plugin closes first (its view hears it now), then the window goes;
    // only then may another view open that plugin.
    w->closing = true;
    EditorService* service = w->service;
    service->recheckElsewhere();
    std::erase_if(m_windows, [service](const Window& each) { return each.service == service; });
    emit windowsChanged();
    rememberOpen();
    recheck();
    service->deleteLater(); // its QML window may still read it this turn
}

void InstrumentWindows::dock(QObject* window)
{
    const Window* w = find(window);
    if (w == nullptr || w->closing) return;
    const int channel = w->service->channelIndex();
    close(window);
    if (channel >= 0) m_document.setSelectedChannel(channel);
    emit docked();
}

void InstrumentWindows::showWindowOf(QObject* view)
{
    auto* service = qobject_cast<EditorService*>(view);
    if (service == nullptr) return;
    if (EditorService* window = showing(*service)) emit raiseRequested(window);
}

void InstrumentWindows::bringBack(QObject* view)
{
    auto* service = qobject_cast<EditorService*>(view);
    if (service == nullptr) return;
    // (Several may stand in the way: a kept window, then the linked one.)
    while (EditorService* window = showing(*service)) close(window);
}

bool InstrumentWindows::canLink(const QObject* window) const
{
    const EditorService* linked = linkedWindow();
    return linked == nullptr || linked == window;
}

bool InstrumentWindows::setLinked(QObject* window, bool linked)
{
    Window* w = find(window);
    if (w == nullptr || w->closing) return false;
    if (linked) {
        if (!canLink(window)) return false;
        w->service->pin(std::nullopt);
    } else {
        // Kept on what it shows now (nothing shown: on the selected channel).
        std::optional<core::ChannelId> keep = w->service->targetId();
        w->service->pin(keep ? keep : m_docked.targetId());
    }
    rememberOpen();
    recheck();
    emit windowsChanged(); // (linkedOpen)
    return true;
}

QRect InstrumentWindows::onScreens(const QRect& saved, const QList<QRect>& screens)
{
    if (!saved.isValid() || saved.isEmpty()) return {};
    for (const QRect& screen : screens) {
        // Its top (the title bar) on this screen, enough of it to grab.
        const QRect top = saved.intersected(QRect(saved.left(), saved.top(), saved.width(), kGrabHeight));
        const QRect onIt = top.intersected(screen);
        if (onIt.width() < std::min(kGrabWidth, saved.width()) || onIt.height() < std::min(kGrabHeight, saved.height())) continue;
        QRect fitted = saved;
        fitted.setWidth(std::min(saved.width(), screen.width()));
        fitted.setHeight(std::min(saved.height(), screen.height()));
        fitted.moveLeft(std::clamp(fitted.left(), screen.left(), screen.right() + 1 - fitted.width()));
        fitted.moveTop(std::clamp(fitted.top(), screen.top(), screen.bottom() + 1 - fitted.height()));
        return fitted;
    }
    return {};
}

QRect InstrumentWindows::restoredGeometry() const
{
    QList<QRect> screens;
    std::ranges::transform(QGuiApplication::screens(), std::back_inserter(screens),
                           [](const QScreen* screen) { return screen->availableGeometry(); });
    const QRect saved = onScreens(m_settings.value(kGeometryKey).toRect(), screens);
    if (saved.isNull() && m_settings.contains(kGeometryKey)) {
        qCInfo(lcUi) << "The instrument window's screen is gone: it opens on the main window";
    }
    return saved;
}

bool InstrumentWindows::restoredMaximized() const
{
    return !restoredGeometry().isNull() && m_settings.value(kMaximizedKey, false).toBool();
}

void InstrumentWindows::rememberGeometry(const QRect& normal, bool maximized)
{
    if (normal.isValid() && !normal.isEmpty()) m_settings.setValue(kGeometryKey, normal);
    m_settings.setValue(kMaximizedKey, maximized);
}

} // namespace gigchain::ui
