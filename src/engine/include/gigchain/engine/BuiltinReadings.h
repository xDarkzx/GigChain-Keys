#pragma once

#include <cstdint>

// What the app's own instruments show in their windows besides their
// settings, read as parameters (IEngine::pluginParameterValue) but never
// saved or set. (The sidechain's playhead is sidechain::kPositionParameter.)
namespace gigchain::engine::readings {

// Piano Pro: 1 its type's piano plays as it should (its recordings, or an
// FM type), 0.5 its recordings are still being read, 0 they are not there
// (its FM piano plays instead).
inline constexpr uint32_t kPianoRecordings = 0x5C01;
// Piano Pro: which piano the type plays, as a choice of 4 (0 grand, 1/3
// upright, 2/3 electric, 1 DX).
inline constexpr uint32_t kPianoSource = 0x5C02;
// SoundFont Player: its file: 0 none chosen, 0.25 it could not be read,
// 0.5 being read, 1 playing.
inline constexpr uint32_t kSoundFontFile = 0x5F01;
// SoundFont Player: changes each time another file's presets are in (its
// window then reads the list again).
inline constexpr uint32_t kSoundFontPresets = 0x5F02;

} // namespace gigchain::engine::readings
