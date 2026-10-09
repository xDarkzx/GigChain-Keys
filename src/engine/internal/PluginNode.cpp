#include "PluginNode.h"

#include "EngineLog.h"
#include "Vst2Node.h"
#include "Vst3Node.h"

#include "gigchain/platform/PluginFolders.h"

using namespace Qt::StringLiterals;

namespace gigchain::engine {

bool PluginNode::isVst2(const QString& pluginId)
{
    return !pluginId.endsWith(u".vst3"_s, Qt::CaseInsensitive);
}

core::Result<std::shared_ptr<PluginNode>> PluginNode::load(const QString& pluginId, double sampleRate, int maxBlock)
{
    // Only this system's plugin files: anything else is refused, never loaded.
    if (isVst2(pluginId) && !platform::isVst2PluginFile(pluginId)) {
        const QString why = u"%1 is not a plugin file (a .vst3 bundle, or a VST2 plugin of this system)"_s.arg(pluginId);
        qCWarning(lcEngine).noquote() << why;
        return core::fail(core::ErrorCode::InvalidData, why);
    }
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
