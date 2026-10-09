#include "PluginNode.h"

#include "Vst2Node.h"
#include "Vst3Node.h"

using namespace Qt::StringLiterals;

namespace gigchain::engine {

bool PluginNode::isVst2(const QString& pluginId)
{
    return !pluginId.endsWith(u".vst3"_s, Qt::CaseInsensitive);
}

core::Result<std::shared_ptr<PluginNode>> PluginNode::load(const QString& pluginId, double sampleRate, int maxBlock)
{
    if (isVst2(pluginId)) {
        auto node = Vst2Node::open(pluginId, sampleRate, maxBlock);
        if (!node) return tl::unexpected(node.error());
        return std::shared_ptr<PluginNode>(std::move(*node));
    }
    auto node = Vst3Node::load(pluginId, sampleRate, maxBlock);
    if (!node) return tl::unexpected(node.error());
    return std::shared_ptr<PluginNode>(std::move(*node));
}

core::Result<std::unique_ptr<IPluginEditor>> PluginNode::createEditor(const std::shared_ptr<PluginNode>& node)
{
    if (!node) return core::fail(core::ErrorCode::InvalidData, u"No plugin to open an editor for"_s);
    return node->makeEditor(node);
}

} // namespace gigchain::engine
