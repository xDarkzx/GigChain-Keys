#pragma once

#include "AudioDevice.h"

#include <RtAudio.h>

namespace gigchain::engine {

// The RtAudio API behind each of this system's drivers (AudioApis_<system>.cpp).
// Only for drivers in systemAudioDrivers().
[[nodiscard]] RtAudio::Api toRtApi(AudioApi api);

// RtAudio's "minimise latency" for this system's drivers: ALSA keeps two
// periods (the buffer asked for stays), WASAPI and ASIO ignore it; Core Audio
// would take the device's smallest buffer instead of the one asked for
// (15 frames: plugins starve), so not there.
[[nodiscard]] RtAudioStreamFlags latencyFlags(AudioApi api);

} // namespace gigchain::engine
