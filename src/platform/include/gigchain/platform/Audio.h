#pragma once

#include <QString>

#include <optional>

namespace gigchain::platform {

// What the player calls this system's own audio (the default driver) in
// Settings: "Windows Audio (WASAPI)", "PulseAudio", "Core Audio".
[[nodiscard]] QString systemAudioName();

// The highest realtime priority this session lets the app's threads have:
// Linux's RLIMIT_RTPRIO (0: none; no limit reads as the largest int), or
// nullopt where there is no such limit (Windows, the Mac: any app may ask
// for a realtime audio thread). A limit that cannot be read is logged, and
// reads as nullopt. The plugin hosts the app starts (yabridge's, for
// plugins running through Wine) inherit it.
[[nodiscard]] std::optional<int> realtimePriorityLimit();

// What to tell the player about that limit: when it is 0, why the audio
// drops out more often and how to fix it (the system grants the priority,
// not the app); otherwise nothing (an empty string).
[[nodiscard]] QString realtimePriorityAdvice(std::optional<int> limit);

} // namespace gigchain::platform
