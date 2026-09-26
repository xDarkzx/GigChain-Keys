#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/engine/IEngine.h"
#include "gigchain/engine/MidiSetup.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace gigchain::engine {

struct RealEngineOptions
{
    // Default: the Windows default output at its own rate. If the saved
    // device cannot open, system audio is used and the user is told why.
    AudioSetup audio;
    MidiSetup midi;            // the inputs chosen in Settings
    QString pluginFolder;      // empty: the standard VST3 folder
    QString pluginCacheFile;   // what the plugin scan learned; empty: open every plugin
    QString pluginGuardFolder; // remembers plugins that crashed the app while loading; empty: off
    // The plugin scanner program: new plugins are read in a process of their
    // own, so one that crashes while being read cannot take the app down.
    // Empty or missing: read in the app (a missing one is logged).
    QString pluginScanner;
    // Start-up progress (plugin scan) for a splash screen; also becomes the
    // engine's progress handler (see IEngine::setProgressHandler).
    LoadProgress progress;
};

// Opens the audio output and every MIDI input and scans plugins. Fails with
// ErrorCode::DeviceUnavailable when no audio output can be opened.
core::Result<std::unique_ptr<IEngine>> createRealEngine(const RealEngineOptions& options = {});

} // namespace gigchain::engine
