#include "GraphExchange.h"

#include <algorithm>

namespace openstage::engine {

void GraphExchange::publish(std::shared_ptr<RenderGraph> graph)
{
    RenderGraph* raw = graph.get();
    if (m_current) m_retired.push_back(std::move(m_current));
    m_current = std::move(graph);
    m_published.store(raw, std::memory_order_seq_cst);
    collectGarbage();
}

void GraphExchange::collectGarbage()
{
    const RenderGraph* busy = m_hazard.load(std::memory_order_seq_cst);
    std::erase_if(m_retired, [busy](const std::shared_ptr<RenderGraph>& graph) { return graph.get() != busy; });
}

RenderGraph* GraphExchange::acquire() noexcept
{
    // Hazard-pointer protocol: publish which graph we are about to use, then
    // confirm it is still the published one. If the main thread swapped in
    // between, retry; otherwise the main thread is guaranteed to see our mark
    // before it frees that graph.
    RenderGraph* graph = m_published.load(std::memory_order_seq_cst);
    while (true) {
        m_hazard.store(graph, std::memory_order_seq_cst);
        RenderGraph* confirmed = m_published.load(std::memory_order_seq_cst);
        if (confirmed == graph) return graph;
        graph = confirmed;
    }
}

void GraphExchange::release() noexcept
{
    m_hazard.store(nullptr, std::memory_order_seq_cst);
}

} // namespace openstage::engine
