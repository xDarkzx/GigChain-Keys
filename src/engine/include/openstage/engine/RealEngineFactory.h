#pragma once

#include "openstage/core/Error.h"
#include "openstage/engine/IEngine.h"
#include "openstage/engine/MidiSetup.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace openstage::engine {

struct RealEngineOptions
{
    // Default: the Windows default output at its own rate. If the saved
    // device cannot open, system audio is used and the user is told why.
    AudioSetup audio;
    MidiSetup midi;            // the inputs chosen in Settings
    QString pluginFolder;      // empty: the standard VST3 folder
    QString pluginCacheFile;   // what the plugin scan learned; empty: open every plugin
};

// Opens the audio output and every MIDI input and scans plugins. Fails with
// ErrorCode::DeviceUnavailable when no audio output can be opened.
core::Result<std::unique_ptr<IEngine>> createRealEngine(const RealEngineOptions& options = {});

} // namespace openstage::engine
