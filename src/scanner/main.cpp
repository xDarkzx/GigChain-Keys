// The plugin scanner: reads one VST3 plugin and writes what it found, for
// the app's plugin scan (PluginCatalog::scan). It runs as a process of its
// own, as Audacity 4 reads plugins, so a plugin that crashes while being read
// ends this process and never the app.
//
//   <scanner> <plugin.vst3> <result.json>
//
// Exit codes: 0 read (the result says whether the plugin loaded), 2 wrong
// arguments, 3 the result could not be written. Anything else: the plugin
// crashed it.
#include "PluginCatalog.h"

#include "gigchain/platform/Process.h"

#include <QCoreApplication>

#include <cstdio>

int main(int argc, char** argv)
{
    // It loads plugins: no library from the folder it was started in (said on
    // stderr, which the app logs, if it cannot), and a plugin crashing it ends
    // it quietly with a code the app reads.
    (void)gigchain::platform::hardenLibrarySearch();
    gigchain::platform::endQuietlyOnCrash();

    const QCoreApplication app(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() != 3) {
        std::fputs("usage: <scanner> <plugin.vst3> <result.json>\n", stderr);
        return 2;
    }
    return gigchain::engine::PluginCatalog::readToFile(arguments.at(1), arguments.at(2)) ? 0 : 3;
}
