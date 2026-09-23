#pragma once

#include "openstage/core/Error.h"
#include "openstage/engine/IEngine.h"

#include <QString>

#include <memory>

namespace openstage::engine {

struct RealEngineOptions
{
    unsigned int bufferFrames = 256;
    // Empty: the default system output (WASAPI). Otherwise the name of an
    // ASIO driver to use instead.
    QString asioDevice;
    QString pluginFolder; // empty: the standard VST3 folder
};

// Opens the audio output and every MIDI input and scans plugins. Fails with
// ErrorCode::DeviceUnavailable when no audio output can be opened.
core::Result<std::unique_ptr<IEngine>> createRealEngine(const RealEngineOptions& options = {});

} // namespace openstage::engine
