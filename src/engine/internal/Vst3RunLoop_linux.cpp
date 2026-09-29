#include "Vst3RunLoop.h"

#include "EngineLog.h"

#include "pluginterfaces/base/smartpointer.h"

#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <vector>

using namespace Steinberg;

namespace gigchain::engine {
namespace {

// Stops a Qt object that calls into a plugin and lets go of the plugin
// (its connection holds the plugin's handler), then deletes it once the event
// loop is back: a plugin often unregisters from inside the very callback,
// while the object is still delivering it.
void retire(QObject* object)
{
    if (object == nullptr) return;
    QObject::disconnect(object, nullptr, nullptr, nullptr);
    object->deleteLater();
}

// Steinberg::Linux::IRunLoop on Qt's event loop (the main thread's).
class QtRunLoop final : public Vst3RunLoop, public Linux::IRunLoop
{
public:
    QtRunLoop() = default;
    ~QtRunLoop() override { clear(); }
    QtRunLoop(const QtRunLoop&) = delete;
    QtRunLoop& operator=(const QtRunLoop&) = delete;
    QtRunLoop(QtRunLoop&&) = delete;
    QtRunLoop& operator=(QtRunLoop&&) = delete;

    Linux::IRunLoop* runLoop() override { return this; }

    bool answers(const TUID requested, void** object) override
    {
        if (!FUnknownPrivate::iidEqual(requested, Linux::IRunLoop::iid)) return false;
        *object = static_cast<Linux::IRunLoop*>(this);
        return true;
    }

    void clear() override
    {
        if (!m_handlers.empty() || !m_timers.empty()) {
            qCInfo(lcEngine) << "Plugin editor closed with" << m_handlers.size() << "file handler(s) and" << m_timers.size()
                             << "timer(s) still registered: stopped";
        }
        for (const Handler& h : m_handlers) retire(h.notifier);
        for (const Timer& t : m_timers) {
            if (t.timer) t.timer->stop();
            retire(t.timer);
        }
        m_handlers.clear();
        m_timers.clear();
    }

    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) override
    {
        if (handler == nullptr || fd < 0) return kInvalidArgument;
        auto* notifier = new QSocketNotifier(static_cast<qintptr>(fd), QSocketNotifier::Read); // retired below
        // The connection holds the plugin's handler (addRef) until retired.
        QObject::connect(notifier, &QSocketNotifier::activated, notifier,
                         [held = IPtr<Linux::IEventHandler>(handler), fd] { held->onFDIsSet(fd); });
        m_handlers.push_back({handler, notifier});
        return kResultOk;
    }

    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override
    {
        const auto removed = std::erase_if(m_handlers, [handler](const Handler& h) {
            if (h.handler != handler) return false;
            retire(h.notifier);
            return true;
        });
        return removed > 0 ? kResultOk : kInvalidArgument;
    }

    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval milliseconds) override
    {
        if (handler == nullptr || milliseconds == 0) return kInvalidArgument;
        auto* timer = new QTimer(); // retired below
        QObject::connect(timer, &QTimer::timeout, timer, [held = IPtr<Linux::ITimerHandler>(handler)] { held->onTimer(); });
        timer->start(static_cast<int>(std::min<Linux::TimerInterval>(milliseconds, 1'000'000)));
        m_timers.push_back({handler, timer});
        return kResultOk;
    }

    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override
    {
        const auto removed = std::erase_if(m_timers, [handler](const Timer& t) {
            if (t.handler != handler) return false;
            if (t.timer) t.timer->stop();
            retire(t.timer);
            return true;
        });
        return removed > 0 ? kResultOk : kInvalidArgument;
    }

    // Owned by the editor: the plugin's references change nothing.
    tresult PLUGIN_API queryInterface(const TUID requested, void** object) override
    {
        QUERY_INTERFACE(requested, object, FUnknown::iid, Linux::IRunLoop)         // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        QUERY_INTERFACE(requested, object, Linux::IRunLoop::iid, Linux::IRunLoop) // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    // Which plugin handler (to find it) and the Qt object calling it; the
    // reference that keeps the handler alive is in the object's connection.
    struct Handler
    {
        Linux::IEventHandler* handler;
        QPointer<QSocketNotifier> notifier;
    };
    struct Timer
    {
        Linux::ITimerHandler* handler;
        QPointer<QTimer> timer;
    };
    std::vector<Handler> m_handlers;
    std::vector<Timer> m_timers;
};

} // namespace

std::unique_ptr<Vst3RunLoop> makeVst3RunLoop()
{
    return std::make_unique<QtRunLoop>();
}

} // namespace gigchain::engine
