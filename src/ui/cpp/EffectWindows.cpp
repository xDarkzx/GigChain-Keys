#include "EffectWindows.h"

#include "DocumentController.h"
#include "FreezeWatchdog.h"

#include "gigchain/engine/IEngine.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <functional>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

// A top-level window that says when the user closes it.
class FloatingWindow final : public QWindow
{
public:
    std::function<void()> onClose;

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::Close && onClose) onClose();
        return QWindow::event(event);
    }
};

QSize toLogical(QSize physical, double ratio)
{
    return QSize(static_cast<int>(std::ceil(physical.width() / ratio)),
                 static_cast<int>(std::ceil(physical.height() / ratio)));
}

} // namespace

struct EffectWindows::Entry
{
    core::ChannelId channel;
    int effect = -1;
    QString pluginId;
    std::unique_ptr<engine::IPluginEditor> editor;
    QPointer<QWindow> window;
    double ratio = 1.0;
    bool resizing = false; // we are resizing the window ourselves
    bool master = false;   // an effect of the master bus (channel unused)
};

EffectWindows::EffectWindows(engine::IEngine& engine, DocumentController& document, QObject* parent)
    : QObject(parent), m_engine(engine), m_document(document)
{
    connect(&m_document, &DocumentController::currentChanged, this, &EffectWindows::sweep);
    connect(&m_document, &DocumentController::channelsChanged, this, &EffectWindows::sweep);
    connect(&m_document, &DocumentController::channelUpdated, this, &EffectWindows::sweep);
}

EffectWindows::~EffectWindows()
{
    for (auto& entry : m_open) {
        entry->editor->detach(); // before its window goes
        delete entry->window.data();
    }
}

bool EffectWindows::open(int channel, int effect, QWindow* owner)
{
    const core::Patch* patch = m_document.currentPatch();
    if (patch == nullptr || channel < 0 || static_cast<std::size_t>(channel) >= patch->channels.size()) return false;
    const core::Channel& strip = patch->channels[static_cast<std::size_t>(channel)];
    if (effect < 0 || static_cast<std::size_t>(effect) >= strip.effects.size()) return false;
    const core::PluginSlot& slot = strip.effects[static_cast<std::size_t>(effect)];

    for (auto& entry : m_open) {
        if (entry->channel == strip.id && entry->effect == effect && entry->pluginId == slot.pluginId) {
            entry->window->raise();
            entry->window->requestActivate();
            return true;
        }
    }

    QElapsedTimer timer;
    timer.start();
    FreezeWatchdog::mark(u"opening the window of %1"_s.arg(slot.displayName));
    auto created = m_engine.createEffectEditor(strip.id, effect);
    if (!created) {
        m_document.reportMessage(created.error().message); // logged where it failed, or a user matter
        qCInfo(lcUi).noquote() << "No window for" << slot.displayName << ":" << created.error().message;
        return false;
    }
    if (!*created) {
        m_document.reportMessage(tr("%1 has no window of its own").arg(slot.displayName));
        return false;
    }

    auto entry = std::make_unique<Entry>();
    entry->channel = strip.id;
    entry->effect = effect;
    entry->pluginId = slot.pluginId;
    entry->editor = std::move(*created);
    if (!show(std::move(entry), u"%1 — %2"_s.arg(slot.displayName, strip.name), owner)) return false;
    qCInfo(lcUi).noquote() << "Effect window" << slot.displayName << "on" << strip.name << "opened in" << timer.elapsed()
                           << "ms";
    return true;
}

bool EffectWindows::openMaster(int effect, const std::vector<core::PluginSlot>& masterSlots, QWindow* owner)
{
    if (effect < 0 || static_cast<std::size_t>(effect) >= masterSlots.size()) return false;
    const core::PluginSlot& slot = masterSlots[static_cast<std::size_t>(effect)];
    for (auto& entry : m_open) {
        if (entry->master && entry->effect == effect && entry->pluginId == slot.pluginId) {
            entry->window->raise();
            entry->window->requestActivate();
            return true;
        }
    }
    FreezeWatchdog::mark(u"opening the window of %1 (master)"_s.arg(slot.displayName));
    auto created = m_engine.createMasterEffectEditor(effect);
    if (!created) {
        m_document.reportMessage(created.error().message);
        return false;
    }
    if (!*created) {
        m_document.reportMessage(tr("%1 has no window of its own").arg(slot.displayName));
        return false;
    }
    auto entry = std::make_unique<Entry>();
    entry->master = true;
    entry->effect = effect;
    entry->pluginId = slot.pluginId;
    entry->editor = std::move(*created);
    return show(std::move(entry), tr("%1 — Master").arg(slot.displayName), owner);
}

bool EffectWindows::show(std::unique_ptr<Entry> entry, const QString& title, QWindow* owner)
{
    auto* window = new FloatingWindow;
    window->setFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    if (owner != nullptr) window->setTransientParent(owner); // stays above the main window
    window->setTitle(title);
    window->create();
    entry->ratio = window->devicePixelRatio();
    // Scale before opening: some plugins size their window from it.
    (void)entry->editor->setContentScale(entry->ratio);
    if (auto attached = entry->editor->attach(static_cast<quintptr>(window->winId())); !attached) {
        delete window;
        m_document.reportMessage(attached.error().message); // logged by the editor
        return false;
    }
    entry->window = window;

    Entry* raw = entry.get();
    const auto fitWindowTo = [raw](QSize physical) {
        const QSize logical = toLogical(physical, raw->ratio);
        raw->resizing = true;
        if (!raw->editor->canResize()) {
            raw->window->setMinimumSize(logical);
            raw->window->setMaximumSize(logical);
        }
        raw->window->resize(logical);
        raw->resizing = false;
    };
    fitWindowTo(entry->editor->preferredSize());
    // The plugin changed its own size (a panel opened, its own zoom menu).
    entry->editor->setResizeHandler(fitWindowTo);
    // The user drags the window edge of a plugin that can be resized.
    const auto userResized = [raw] {
        if (raw->resizing || !raw->editor->canResize()) return;
        const QSize wanted(qRound(raw->window->width() * raw->ratio), qRound(raw->window->height() * raw->ratio));
        const QSize accepted = raw->editor->setSize(wanted);
        if (accepted != wanted && !accepted.isEmpty()) {
            raw->resizing = true;
            raw->window->resize(toLogical(accepted, raw->ratio));
            raw->resizing = false;
        }
    };
    connect(window, &QWindow::widthChanged, this, userResized);
    connect(window, &QWindow::heightChanged, this, userResized);
    window->onClose = [this, raw] {
        const auto it = std::find_if(m_open.begin(), m_open.end(), [raw](const auto& e) { return e.get() == raw; });
        if (it != m_open.end()) close(**it);
    };

    // Cascade from the middle of the main window.
    if (owner != nullptr) {
        const QPoint offset(28 * static_cast<int>(m_open.size() % 8), 28 * static_cast<int>(m_open.size() % 8));
        window->setPosition(owner->geometry().center() - QPoint(window->width() / 2, window->height() / 2) + offset);
    }
    window->show();
    window->requestActivate();
    m_open.push_back(std::move(entry));
    emit openCountChanged();
    return true;
}

void EffectWindows::closeAll()
{
    while (!m_open.empty()) close(*m_open.back());
}

void EffectWindows::close(Entry& entry)
{
    const bool master = entry.master;
    entry.editor->detach(); // before its window goes
    entry.editor.reset();
    if (entry.window) {
        entry.window->hide();
        entry.window->deleteLater();
    }
    std::erase_if(m_open, [&entry](const auto& e) { return e.get() == &entry; });
    emit openCountChanged();
    if (master) emit masterWindowClosed();
}

void EffectWindows::sweepMaster(const std::vector<core::PluginSlot>& masterSlots)
{
    std::vector<Entry*> gone;
    for (auto& entry : m_open) {
        if (!entry->master) continue;
        const bool stillThere = entry->effect < static_cast<int>(masterSlots.size())
                                && masterSlots[static_cast<std::size_t>(entry->effect)].pluginId == entry->pluginId
                                && !masterSlots[static_cast<std::size_t>(entry->effect)].bypass;
        if (!stillThere) gone.push_back(entry.get());
    }
    for (Entry* entry : gone) close(*entry);
}

void EffectWindows::sweep()
{
    const core::Patch* patch = m_document.currentPatch();
    std::vector<Entry*> gone;
    for (auto& entry : m_open) {
        if (entry->master) continue; // the master bus is not in the setlist
        const core::Channel* channel = nullptr;
        if (patch != nullptr) {
            for (const core::Channel& c : patch->channels) {
                if (c.id == entry->channel) channel = &c;
            }
        }
        const bool stillThere = channel != nullptr && entry->effect < static_cast<int>(channel->effects.size())
                                && channel->effects[static_cast<std::size_t>(entry->effect)].pluginId == entry->pluginId
                                && !channel->effects[static_cast<std::size_t>(entry->effect)].bypass;
        if (!stillThere) gone.push_back(entry.get());
    }
    for (Entry* entry : gone) close(*entry);
}

} // namespace gigchain::ui
