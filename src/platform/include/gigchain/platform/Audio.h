#pragma once

#include <QString>

namespace gigchain::platform {

// What the player calls this system's own audio (the default driver) in
// Settings: "Windows Audio (WASAPI)", "PulseAudio".
[[nodiscard]] QString systemAudioName();

} // namespace gigchain::platform
