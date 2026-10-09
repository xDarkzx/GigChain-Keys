#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/core/Model.h"
#include "gigchain/core/Navigation.h"

#include <functional>

// Setlist edits. Each function validates its input and leaves the setlist
// unchanged when it returns an error. Names are trimmed.
namespace gigchain::core {

Result<int> addSong(Setlist& setlist, const QString& name);
Result<int> addPatch(Setlist& setlist, int songIndex, const QString& name);
Result<void> renameSong(Setlist& setlist, int songIndex, const QString& name);
Result<void> renamePatch(Setlist& setlist, Cursor cursor, const QString& name);
// Every channel of the sound plays together, or only the selected one.
Result<void> setPatchPlayMode(Setlist& setlist, Cursor cursor, PlayMode mode);
// The hardware sounds (Program Change, bank) the patch calls up when it comes up.
Result<void> setExternalPrograms(Setlist& setlist, Cursor cursor, const std::vector<ExternalProgram>& programs);
// The song's chart (ChordPro). Refused when longer than limits::kMaxChartLength.
Result<void> setSongChart(Setlist& setlist, int songIndex, const QString& chart);
// The song's key ("" = not set) and tempo in BPM (0 = not set).
Result<void> setSongKeyAndTempo(Setlist& setlist, int songIndex, const QString& key, double tempo);

// The song's time signature (numerator beats of a 1/denominator note).
Result<void> setSongTimeSignature(Setlist& setlist, int songIndex, int numerator, int denominator);
// Sections switch a beat early (true) or just before their first beat.
Result<void> setSongSwitchEarly(Setlist& setlist, int songIndex, bool early);
// The song's chart with a section renamed (renameSection()'s result): its
// flow and its sections' instruments keep to each section, by its place.
Result<void> renameSongSection(Setlist& setlist, int songIndex, const QString& chart);
// The order a song is played in (its sections, by name); empty: the chart's order.
Result<void> setSongFlow(Setlist& setlist, int songIndex, const std::vector<SectionRef>& flow);
// The inversion chosen for chord `chord` of a song (0 to 3); -1 forgets it.
Result<void> setChordInversion(Setlist& setlist, int songIndex, const QString& chord, int inversion);
// Loops start and stop on the bars (true) or press to press.
Result<void> setSongLoopSync(Setlist& setlist, int songIndex, bool sync);
// A synced loop's length in bars (0 = open).
Result<void> setSongLoopBars(Setlist& setlist, int songIndex, int bars);
// The looper's keyboard controls (validated).
Result<void> setLoopControls(Setlist& setlist, const LoopControls& controls);
// The mixer's keyboard knobs (validated).
Result<void> setMixerControls(Setlist& setlist, const MixerControls& controls);
// Stores what one section plays and how long it is, replacing the setup of
// the same section (same name ignoring case, same occurrence).
Result<void> setSectionSetup(Setlist& setlist, int songIndex, const SectionSetup& setup);

// Inserts the copy directly after the original and returns its index.
Result<int> duplicateSong(Setlist& setlist, int songIndex);
Result<int> duplicatePatch(Setlist& setlist, Cursor cursor);

Result<void> removeSong(Setlist& setlist, int songIndex);
// A song keeps at least one patch; removing the last one is refused.
Result<void> removePatch(Setlist& setlist, Cursor cursor);

// `to` is the final index of the moved item.
Result<void> moveSong(Setlist& setlist, int from, int to);
Result<void> movePatch(Setlist& setlist, int songIndex, int from, int to);

// The song's backing track: a plain file name in the setlist's folder, or
// "" for none.
Result<void> setSongBackingTrack(Setlist& setlist, int songIndex, const QString& fileName);

// New channel named after the instrument. Returns its index.
Result<int> addChannel(Setlist& setlist, Cursor cursor, const PluginSlot& instrument);
// New channel playing an audio input (1-based; `inputRight` 0 = mono)
// through its effects, instead of an instrument. Returns its index.
Result<int> addInputChannel(Setlist& setlist, Cursor cursor, const QString& name, int inputLeft, int inputRight);
Result<void> removeChannel(Setlist& setlist, Cursor cursor, int channelIndex);
Result<void> addEffect(Setlist& setlist, Cursor cursor, int channelIndex, const PluginSlot& effect);
Result<void> removeEffect(Setlist& setlist, Cursor cursor, int channelIndex, int effectIndex);

// Applies `edit` to a copy of the channel and commits it only if the result
// validates, so a bad value from the UI can never enter the model.
Result<void> updateChannel(Setlist& setlist, Cursor cursor, int channelIndex,
                           const std::function<void(Channel&)>& edit);

} // namespace gigchain::core
