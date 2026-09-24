#pragma once

#include "openstage/engine/EngineTypes.h"

#include <QString>

#include <vector>

namespace openstage::engine {

// Finds VST3 plugins under a folder (the standard one is
// C:/Program Files/Common Files/VST3) and reads each bundle's class info.
// A plugin's id is its absolute bundle path, which Vst3Node::load accepts.
//
// Opening a plugin runs its own start-up code (and Windows Defender scans
// it), so, like every host, a plugin is opened once and what it reported is
// kept in `cacheFile` (JSON) with its file's size and date. Later scans open
// only plugins that are new or changed. Bundles that fail to load are logged
// with the exact reason and left out; a cached failure is logged again at
// every scan and retried when the file changes.
struct ScanStats
{
    int opened = 0;    // plugin files opened this scan
    int fromCache = 0; // taken from the cache without opening
    int failed = 0;    // left out (each logged)
};

class PluginCatalog
{
public:
    static QString standardFolder();
    // No cacheFile: every plugin is opened.
    static std::vector<PluginInfo> scan(const QString& folder, const QString& cacheFile = {},
                                        ScanStats* stats = nullptr);
};

} // namespace openstage::engine
