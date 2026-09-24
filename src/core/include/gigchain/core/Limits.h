#pragma once

#include <QtGlobal>

// Hard limits for setlists. Files are untrusted input: anything outside these
// is rejected with an Error instead of being processed.
namespace gigchain::core::limits {

inline constexpr qint64 kMaxFileBytes = 8LL * 1024 * 1024;
inline constexpr int kMaxSongs = 500;
inline constexpr int kMaxPatchesPerSong = 64;
inline constexpr int kMaxChannelsPerPatch = 32;
inline constexpr int kMaxEffectsPerChannel = 16;
inline constexpr int kMaxNameLength = 200;
inline constexpr int kMaxIdLength = 64;
inline constexpr int kMaxPluginIdLength = 512;

inline constexpr int kMinMidiNote = 0;
inline constexpr int kMaxMidiNote = 127;
inline constexpr int kMinTranspose = -48;
inline constexpr int kMaxTranspose = 48;
inline constexpr int kMinMidiChannel = 0; // omni
inline constexpr int kMaxMidiChannel = 16;
inline constexpr double kMinVolumeDb = -96.0;
inline constexpr double kMaxVolumeDb = 12.0;
inline constexpr double kMinPan = -1.0;
inline constexpr double kMaxPan = 1.0;

} // namespace gigchain::core::limits
