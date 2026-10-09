#pragma once

#include "gigchain/engine/EngineTypes.h"

#include <QString>

#include <functional>

#include <vector>

namespace gigchain::engine {

class PluginLoadGuard;

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
    // Called before each plugin: its file name (no extension), how many are
    // done, how many in all. For a splash screen.
    using Progress = std::function<void(const QString& plugin, int done, int total)>;

    // No cacheFile: every plugin is opened. `guard`: plugins it blocked are
    // not opened, and plugins read in this process are read under it.
    // `scanner`: the plugin scanner program (src/scanner). New and changed
    // plugins are then read each in a process of its own, several at once,
    // as Audacity 4 does: one that crashes while being read ends only that
    // process (remembered, not opened again until its file changes); one that
    // hangs is stopped after kScanTimeoutMs (tried again next scan). Empty:
    // read in this process; set but missing: the same, and logged.
    // `format`: the plugins looked for (VST2: platform::isVst2PluginFile).
    static std::vector<PluginInfo> scan(const QString& folder, const QString& cacheFile = {},
                                        ScanStats* stats = nullptr, const Progress& progress = {},
                                        const PluginLoadGuard* guard = nullptr, const QString& scanner = {},
                                        PluginFormat format = PluginFormat::Vst3);

    // The scanner program's work: reads one plugin and writes what it found
    // (or why it could not) to `resultFile`. False, logged, when the file
    // cannot be written.
    static bool readToFile(const QString& bundle, const QString& resultFile);

    static constexpr int kScanTimeoutMs = 15000; // Audacity 4's AUDIO_PLUGIN_REGISTRATION_TIMEOUT_MS
    // A cache larger than this is not read (a real one is ~1 KB a plugin).
    static constexpr qint64 kMaxCacheBytes = 16LL * 1024 * 1024;
    // A scanner's result larger than this is not read (a real one is ~1 KB).
    static constexpr qint64 kMaxResultBytes = 1024LL * 1024;
};

} // namespace gigchain::engine
