#pragma once

#include "RenderGraph.h"

#include <atomic>
#include <memory>
#include <vector>

namespace gigchain::engine {

// Hands render graphs from the main thread to the audio thread without locks.
//
// The main thread owns every graph (shared_ptr) and is the only thread that
// frees one. The audio thread marks the graph it is rendering with a hazard
// pointer; a retired graph is freed only once the audio thread is no longer
// marking it. Audio must be stopped before the exchange is destroyed.
class GraphExchange
{
public:
    GraphExchange() = default;
    GraphExchange(const GraphExchange&) = delete;
    GraphExchange& operator=(const GraphExchange&) = delete;
    GraphExchange(GraphExchange&&) = delete;
    GraphExchange& operator=(GraphExchange&&) = delete;
    ~GraphExchange() = default;

    // Main thread. `graph` may be null (silence).
    void publish(std::shared_ptr<RenderGraph> graph);
    // Main thread. Frees retired graphs the audio thread can no longer be using.
    void collectGarbage();
    // Main thread. The latest published graph, for live mixer changes. Non-owning.
    [[nodiscard]] RenderGraph* current() const { return m_current.get(); }
    [[nodiscard]] std::size_t retiredCount() const { return m_retired.size(); }

    // Audio thread: acquire() at the start of a block, release() at the end.
    RenderGraph* acquire() noexcept;
    void release() noexcept;

private:
    std::shared_ptr<RenderGraph> m_current;
    std::vector<std::shared_ptr<RenderGraph>> m_retired;
    std::atomic<RenderGraph*> m_published{nullptr};
    std::atomic<RenderGraph*> m_hazard{nullptr};
};

} // namespace gigchain::engine
