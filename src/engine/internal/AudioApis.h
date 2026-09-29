#pragma once

#include "AudioDevice.h"

#include <RtAudio.h>

namespace gigchain::engine {

// The RtAudio API behind each of this system's drivers (AudioApis_<system>.cpp).
// Only for drivers in systemAudioDrivers().
[[nodiscard]] RtAudio::Api toRtApi(AudioApi api);

} // namespace gigchain::engine
