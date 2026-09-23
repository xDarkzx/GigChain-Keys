#pragma once

#include "openstage/engine/EngineTypes.h"

#include <QString>

#include <vector>

namespace openstage::engine {

// Finds VST3 plugins under a folder (the standard one is
// C:/Program Files/Common Files/VST3) and reads each bundle's class info.
// A plugin's id is its absolute bundle path, which Vst3Node::load accepts.
//
// v1 loads each module in-process to read its factory (as Muse does).
// Bundles that fail to load are logged with the exact reason and left out.
class PluginCatalog
{
public:
    static QString standardFolder();
    static std::vector<PluginInfo> scan(const QString& folder);
};

} // namespace openstage::engine
