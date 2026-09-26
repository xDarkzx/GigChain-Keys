#pragma once

#include "RenderGraph.h"

#include <atomic>
#include <memory>
#include <vector>

namespace gigchain::engine {

// Hands objects (render graphs, backing tracks) from the main thread to the
// audio thread without locks.
//
// The main thread owns every object (shared_ptr) and is the only thread that
// frees one. The audio thread marks the object it is using with a hazard
// pointer; a retired object is freed only once the audio thread is no longer
// marking it. Audio must be stopped before the exchange is destroyed.
template <typename T>
class HazardExchange
{
public:
    HazardExchange() = default;
    HazardExchange(const HazardExchange&) = delete;
    HazardExchange& operator=(const HazardExchange&) = delete;
    HazardExchange(HazardExchange&&) = delete;
    HazardExchange& operator=(HazardExchange&&) = delete;
    ~HazardExchange() = default;

    // Main thread. `object` may be null (nothing).
    void publish(std::shared_ptr<T> object)
    {
        T* raw = object.get();
        if (m_current) m_retired.push_back(std::move(m_current));
        m_current = std::move(object);
        m_published.store(raw, std::memory_order_seq_cst);
        collectGarbage();
    }
    // Main thread. Frees retired objects the audio thread can no longer be using.
    void collectGarbage()
    {
        const T* busy = m_hazard.load(std::memory_order_seq_cst);
        std::erase_if(m_retired, [busy](const std::shared_ptr<T>& object) { return object.get() != busy; });
    }
    // Main thread. The latest published object. Non-owning.
    [[nodiscard]] T* current() const { return m_current.get(); }
    // Main thread. The latest published object, shared.
    [[nodiscard]] const std::shared_ptr<T>& currentShared() const { return m_current; }
    [[nodiscard]] std::size_t retiredCount() const { return m_retired.size(); }

    // Audio thread: acquire() at the start of a block, release() at the end.
    T* acquire() noexcept
    {
        // Hazard-pointer protocol: publish which object we are about to use,
        // then confirm it is still the published one. If the main thread
        // swapped in between, retry; otherwise the main thread is guaranteed
        // to see our mark before it frees that object.
        T* object = m_published.load(std::memory_order_seq_cst);
        while (true) {
            m_hazard.store(object, std::memory_order_seq_cst);
            T* confirmed = m_published.load(std::memory_order_seq_cst);
            if (confirmed == object) return object;
            object = confirmed;
        }
    }
    void release() noexcept { m_hazard.store(nullptr, std::memory_order_seq_cst); }

private:
    std::shared_ptr<T> m_current;
    std::vector<std::shared_ptr<T>> m_retired;
    std::atomic<T*> m_published{nullptr};
    std::atomic<T*> m_hazard{nullptr};
};

using GraphExchange = HazardExchange<RenderGraph>;

} // namespace gigchain::engine
