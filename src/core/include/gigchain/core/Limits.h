#pragma once

#include <QtGlobal>

// Hard limits for setlists. Files are untrusted input: anything outside these
// is rejected with an Error instead of being processed.
namespace gigchain::core::limits {

// Plugin settings make files bigger: measured, most plugins store under 15 KB
// once compressed (Arturia's Synclavier V: 628 KB raw, 14 KB stored).
inline constexpr qint64 kMaxFileBytes = 64LL * 1024 * 1024;
inline constexpr qsizetype kMaxPluginStateBytes = 16LL * 1024 * 1024; // one plugin's stored settings
inline constexpr int kMaxSongs = 500;
inline constexpr int kMaxPatchesPerSong = 64;
inline constexpr int kMaxChannelsPerPatch = 32;
inline constexpr int kMaxEffectsPerChannel = 16;
inline constexpr int kMaxNameLength = 200;
inline constexpr int kMaxIdLength = 64;
inline constexpr int kMaxPluginIdLength = 512;
inline constexpr int kMaxChartLength = 100'000; // a very long song is ~10k
// What is read to make a chart (pasted, or a chart file) before tidying:
// web pages bring a lot of clutter, but more than this is not a song.
inline constexpr int kMaxChartSourceLength = 1'000'000;
inline constexpr int kMaxNotesLength = 10'000;
inline constexpr int kMaxKeyLength = 16;
inline constexpr int kMaxLinksPerSong = 32;
inline constexpr int kMaxUrlLength = 2048;
inline constexpr int kMaxAttachmentsPerSong = 32;
inline constexpr int kMaxFileNameLength = 255;
inline constexpr double kMaxTempo = 400.0;
inline constexpr int kMaxSectionsPerSong = 64; // one bit each in the engine
inline constexpr int kMaxSectionBars = 999;
inline constexpr int kMaxSectionOccurrence = 64;
inline constexpr int kMaxFollowSteps = 4096; // the chords of a song, repeats played out
inline constexpr int kMaxLoopBars = 64;
inline constexpr int kMaxChordInversion = 3;   // root position, then 1st to 3rd (a four-note chord)
inline constexpr int kMaxChosenInversions = 512; // per song (a chord name each)
inline constexpr int kMaxFlowParts = 256;        // a song's flow: sections as played (a chorus x3 is three)

inline constexpr int kMinMidiNote = 0;
inline constexpr int kMaxMidiNote = 127;
inline constexpr int kMinTranspose = -48;
inline constexpr int kMaxTranspose = 48;
inline constexpr int kMinMidiChannel = 0; // omni
inline constexpr int kMaxMidiChannel = 16;
inline constexpr int kMinVelocity = 1;
inline constexpr int kMaxVelocity = 127;
inline constexpr int kMaxMappingsPerChannel = 32;
inline constexpr int kMaxController = 127;
inline constexpr int kMaxAudioInput = 64; // 1-based input numbers; 0 = none
inline constexpr int kMaxOutputPair = 7;  // a channel's outputs: 0 the mix, 1-7 = outputs 3-4 ... 15-16
inline constexpr int kMaxExternalPrograms = 4; // hardware sounds a patch calls up
inline constexpr int kMaxStems = 8;            // a song's backing tracks besides its main one
inline constexpr int kMaxTrackMarkers = 64;    // places marked in a song's backing track
inline constexpr double kMaxTrackSeconds = 20.0 * 60.0; // the longest backing track read
inline constexpr int kMaxBank = 16383;         // bank select, MSB and LSB
inline constexpr double kMinVolumeDb = -96.0;
inline constexpr double kMaxVolumeDb = 12.0;
inline constexpr double kMinPan = -1.0;
inline constexpr double kMaxPan = 1.0;

} // namespace gigchain::core::limits
