#pragma once

#include "public.sdk/source/vst/hosting/module.h"

#include <QString>

#include <string>

namespace gigchain::engine {

// A plugin bundle's library, loaded once and shared by every instance of
// it (and the scan), as Audacity 4's VstModulesRepository does. Loading it
// runs the plugin's own start-up (InitDll) and releasing the last user runs
// its shutdown (ExitDll). With a library per instance, one instance going
// away would shut the plugin down under the others still playing.
// Thread-safe; loading happens on the main thread.
class PluginModules
{
public:
    // The shared library, loaded if no one holds it. nullptr: it could not
    // load, and `error` says why.
    static VST3::Hosting::Module::Ptr get(const QString& bundlePath, std::string& error);
    // Libraries currently loaded (for tests and diagnostics).
    [[nodiscard]] static std::size_t loadedCount();
};

} // namespace gigchain::engine
