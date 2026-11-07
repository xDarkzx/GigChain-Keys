#include "gigchain/core/Editing.h"

#include "gigchain/core/Chart.h"
#include "gigchain/core/ChartEdit.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/PluginSharing.h"
#include "gigchain/core/SongMap.h"

#include <cmath>
#include "gigchain/core/Validation.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

constexpr auto kCopySuffix = " (copy)"_L1;

bool inRange(int index, std::size_t size)
{
    return index >= 0 && std::cmp_less(index, size);
}

std::size_t toIndex(int index)
{
    return static_cast<std::size_t>(index);
}

tl::unexpected<Error> missing(const QString& what)
{
    return fail(ErrorCode::OutOfRange, u"%1 does not exist"_s.arg(what));
}

// No song at `songIndex`: as the player reads it ("Song 0 does not exist"
// was what no song at all, -1, read as).
tl::unexpected<Error> missingSong(int songIndex)
{
    if (songIndex < 0) return fail(ErrorCode::OutOfRange, u"There is no song yet: add one first"_s);
    return missing(u"Song %1"_s.arg(songIndex + 1));
}

Result<QString> cleanName(const QString& name, const QString& what)
{
    QString trimmed = name.trimmed();
    if (auto valid = validateName(trimmed, what); !valid) return tl::unexpected(valid.error());
    return trimmed;
}

QString copyName(const QString& name)
{
    return name.left(limits::kMaxNameLength - kCopySuffix.size()) + kCopySuffix;
}

template <typename T>
void moveElement(std::vector<T>& items, int from, int to)
{
    const auto begin = items.begin();
    if (from < to) {
        std::rotate(begin + from, begin + from + 1, begin + to + 1);
    } else if (from > to) {
        std::rotate(begin + to, begin + from, begin + from + 1);
    }
}

} // namespace

Result<int> addSong(Setlist& setlist, const QString& name)
{
    auto clean = cleanName(name, u"Song name"_s);
    if (!clean) return tl::unexpected(clean.error());
    if (setlist.songs.size() >= toIndex(limits::kMaxSongs)) {
        return fail(ErrorCode::LimitExceeded, u"A setlist can hold at most %1 songs"_s.arg(limits::kMaxSongs));
    }
    setlist.songs.push_back(makeSong(*clean));
    return static_cast<int>(setlist.songs.size()) - 1;
}

Result<int> addPatch(Setlist& setlist, int songIndex, const QString& name)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    auto clean = cleanName(name, u"Patch name"_s);
    if (!clean) return tl::unexpected(clean.error());
    Song& song = setlist.songs.at(toIndex(songIndex));
    if (song.patches.size() >= toIndex(limits::kMaxPatchesPerSong)) {
        return fail(ErrorCode::LimitExceeded, u"A song can hold at most %1 patches"_s.arg(limits::kMaxPatchesPerSong));
    }
    song.patches.push_back(makePatch(*clean));
    return static_cast<int>(song.patches.size()) - 1;
}

Result<void> renameSong(Setlist& setlist, int songIndex, const QString& name)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    auto clean = cleanName(name, u"Song name"_s);
    if (!clean) return tl::unexpected(clean.error());
    setlist.songs.at(toIndex(songIndex)).name = *clean;
    return {};
}

Result<void> setSongChart(Setlist& setlist, int songIndex, const QString& chart)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (chart.size() > limits::kMaxChartLength) {
        return fail(ErrorCode::LimitExceeded,
                    u"The chart is longer than %1 characters"_s.arg(limits::kMaxChartLength));
    }
    setlist.songs.at(toIndex(songIndex)).chart = chart;
    return {};
}

Result<void> setSongKeyAndTempo(Setlist& setlist, int songIndex, const QString& key, double tempo)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (key.size() > limits::kMaxKeyLength) {
        return fail(ErrorCode::LimitExceeded, u"The key is longer than %1 characters"_s.arg(limits::kMaxKeyLength));
    }
    if (!std::isfinite(tempo) || tempo < 0.0 || tempo > limits::kMaxTempo) {
        return fail(ErrorCode::OutOfRange, u"The tempo must be between 0 and %1"_s.arg(limits::kMaxTempo));
    }
    Song& song = setlist.songs.at(toIndex(songIndex));
    song.key = key.trimmed();
    song.tempo = tempo;
    return {};
}

Result<void> renamePatch(Setlist& setlist, Cursor cursor, const QString& name)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    auto clean = cleanName(name, u"Patch name"_s);
    if (!clean) return tl::unexpected(clean.error());
    patch->name = *clean;
    return {};
}

Result<void> setExternalPrograms(Setlist& setlist, Cursor cursor, const std::vector<ExternalProgram>& programs)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Song %1, sound %2"_s.arg(cursor.song + 1).arg(cursor.patch + 1));
    if (programs.size() > static_cast<std::size_t>(limits::kMaxExternalPrograms)) {
        return fail(ErrorCode::LimitExceeded, u"A sound can call up at most %1 hardware sounds"_s.arg(limits::kMaxExternalPrograms));
    }
    for (const ExternalProgram& p : programs) {
        if (p.port.trimmed().isEmpty()) return fail(ErrorCode::InvalidData, u"Choose the MIDI output the synth is on"_s);
        if (p.midiChannel < 1 || p.midiChannel > 16) return fail(ErrorCode::OutOfRange, u"MIDI channels are 1 to 16"_s);
        if (p.program < 0 || p.program > 127) return fail(ErrorCode::OutOfRange, u"Programs are 1 to 128"_s);
        if (p.bank < -1 || p.bank > limits::kMaxBank) {
            return fail(ErrorCode::OutOfRange, u"Banks are 0 to %1 (or none)"_s.arg(limits::kMaxBank));
        }
    }
    patch->externalPrograms = programs;
    return {};
}

Result<void> setPatchPlayMode(Setlist& setlist, Cursor cursor, PlayMode mode)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Song %1, sound %2"_s.arg(cursor.song + 1).arg(cursor.patch + 1));
    if (mode != PlayMode::All && mode != PlayMode::Selected) {
        return fail(ErrorCode::OutOfRange, u"A sound plays all its channels (0) or the selected one (1), not %1"_s.arg(static_cast<int>(mode)));
    }
    patch->playMode = mode;
    return {};
}

Result<void> setSongTimeSignature(Setlist& setlist, int songIndex, int numerator, int denominator)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (!isTimeSignature(numerator, denominator)) {
        return fail(ErrorCode::OutOfRange,
                    u"%1/%2 is not a time signature (1-32 beats of a 1, 2, 4, 8, 16 or 32 note)"_s.arg(numerator).arg(denominator));
    }
    Song& song = setlist.songs.at(toIndex(songIndex));
    song.timeNumerator = numerator;
    song.timeDenominator = denominator;
    return {};
}

Result<void> setSongSwitchEarly(Setlist& setlist, int songIndex, bool early)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    setlist.songs.at(toIndex(songIndex)).switchEarly = early;
    return {};
}

Result<void> renameSongSection(Setlist& setlist, int songIndex, const QString& chart)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    Song& song = setlist.songs.at(toIndex(songIndex));
    const std::vector<ChartSection> before = chartSections(parseChordPro(song.chart));
    if (auto r = setSongChart(setlist, songIndex, chart); !r) return r;
    const std::vector<ChartSection> after = chartSections(parseChordPro(song.chart));
    // A note made a section (one more): the sections after it are one further on.
    int added = -1;
    if (after.size() == before.size() + 1) {
        for (std::size_t s = 0; s < after.size(); ++s) {
            if (s >= before.size() || after.at(s).line != before.at(s).line) {
                added = static_cast<int>(s);
                break;
            }
        }
    } else if (after.size() != before.size()) {
        return {}; // not a rename: nothing to carry over
    }
    // What named the section at a place names it there still.
    const auto follow = [&](QString& name, int& occurrence) {
        int at = sectionIndexOf(before, SectionRef{.name = name, .occurrence = occurrence});
        if (at < 0) return;
        if (added >= 0 && at >= added) ++at;
        name = after.at(toIndex(at)).name;
        occurrence = after.at(toIndex(at)).occurrence;
    };
    for (SectionRef& part : song.flow) follow(part.name, part.occurrence);
    for (SectionSetup& section : song.sections) follow(section.name, section.occurrence);
    return {};
}

Result<void> removeSongSection(Setlist& setlist, int songIndex, int line)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    Song& song = setlist.songs.at(toIndex(songIndex));
    auto edited = removeSectionTitle(song.chart, line);
    if (!edited) return tl::unexpected(edited.error());
    const std::vector<ChartSection> before = chartSections(parseChordPro(song.chart));
    const auto gone = std::ranges::find_if(before, [line](const ChartSection& s) { return s.line == line; });
    if (auto r = setSongChart(setlist, songIndex, *edited); !r) return r;
    if (gone == before.end()) return {}; // a note, not a section: nothing names it
    const auto removed = static_cast<int>(gone - before.begin());
    const std::vector<ChartSection> after = chartSections(parseChordPro(song.chart));
    if (after.size() + 1 != before.size()) return {}; // (read differently: nothing to carry over)
    // Where what named a section at a place names it now; -1: the one taken out.
    const auto now = [&](const QString& name, int occurrence) {
        const int at = sectionIndexOf(before, SectionRef{.name = name, .occurrence = occurrence});
        if (at < 0) return -2; // named nothing before either: left as it is
        if (at == removed) return -1;
        return at > removed ? at - 1 : at;
    };
    std::erase_if(song.flow, [&](const SectionRef& part) { return now(part.name, part.occurrence) == -1; });
    std::erase_if(song.sections, [&](const SectionSetup& s) { return now(s.name, s.occurrence) == -1; });
    // (Each looked up by its old name in `before`: one renamed is never taken for another.)
    const auto follow = [&](QString& name, int& occurrence) {
        const int at = now(name, occurrence);
        if (at < 0) return;
        name = after.at(toIndex(at)).name;
        occurrence = after.at(toIndex(at)).occurrence;
    };
    for (SectionRef& part : song.flow) follow(part.name, part.occurrence);
    for (SectionSetup& section : song.sections) follow(section.name, section.occurrence);
    return {};
}

Result<void> setSongFlow(Setlist& setlist, int songIndex, const std::vector<SectionRef>& flow)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (flow.size() > static_cast<std::size_t>(limits::kMaxFlowParts)) {
        return fail(ErrorCode::LimitExceeded, u"A song's flow has at most %1 parts"_s.arg(limits::kMaxFlowParts));
    }
    const bool unnamed = std::ranges::any_of(flow, [](const SectionRef& part) {
        return part.name.trimmed().isEmpty() || part.occurrence < 1 || part.occurrence > limits::kMaxSectionOccurrence;
    });
    if (unnamed) return fail(ErrorCode::InvalidData, u"Each part of a song's flow names a section of its chart"_s);
    setlist.songs.at(toIndex(songIndex)).flow = flow;
    return {};
}

Result<void> arrangeSongChart(Setlist& setlist, int songIndex)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    Song& song = setlist.songs.at(toIndex(songIndex));
    if (song.flow.empty()) return {};
    const Chart chart = parseChordPro(song.chart);
    const std::vector<ChartSection> sections = chartSections(chart);
    // The order: each section where the flow first plays it, then the rest as written.
    std::vector<std::size_t> order;
    for (const SectionRef& part : song.flow) {
        const int at = sectionIndexOf(sections, part);
        if (at >= 0 && std::ranges::find(order, toIndex(at)) == order.end()) order.push_back(toIndex(at));
    }
    for (std::size_t s = 0; s < sections.size(); ++s) {
        if (std::ranges::find(order, s) == order.end()) order.push_back(s);
    }
    if (std::ranges::is_sorted(order)) return {};
    // A section's lines: from its title to the next one's; the blank lines
    // closing it are the gap after it, which stays in its place.
    const auto begin = [&](std::size_t s) { return toIndex(sections.at(s).line); };
    const auto end = [&](std::size_t s) { return s + 1 < sections.size() ? begin(s + 1) : chart.lines.size(); };
    const auto bodyEnd = [&](std::size_t s) {
        std::size_t e = end(s);
        while (e > begin(s) + 1 && chart.lines.at(e - 1).kind == ChartLine::Kind::Blank) --e;
        return e;
    };
    const auto lineAt = [&chart](std::size_t l) { return chart.lines.begin() + static_cast<std::ptrdiff_t>(l); };
    Chart arranged = chart;
    arranged.lines.assign(chart.lines.begin(), lineAt(begin(0))); // (what comes before the first section)
    for (std::size_t place = 0; place < order.size(); ++place) {
        const std::size_t s = order.at(place);
        arranged.lines.insert(arranged.lines.end(), lineAt(begin(s)), lineAt(bodyEnd(s)));
        arranged.lines.insert(arranged.lines.end(), lineAt(bodyEnd(place)), lineAt(end(place)));
    }
    const QString text = toChordPro(arranged);
    const std::vector<ChartSection> after = chartSections(parseChordPro(text));
    if (after.size() != sections.size()) {
        return fail(ErrorCode::InvalidData, u"The chart's sections could not be put in the flow's order (they read differently moved)"_s);
    }
    if (auto r = setSongChart(setlist, songIndex, text); !r) return r;
    // What named a section names it in its new place.
    const auto follow = [&](QString& name, int& occurrence) {
        const int at = sectionIndexOf(sections, SectionRef{.name = name, .occurrence = occurrence});
        if (at < 0) return;
        const auto moved = std::ranges::find(order, toIndex(at));
        const ChartSection& now = after.at(static_cast<std::size_t>(moved - order.begin()));
        name = now.name;
        occurrence = now.occurrence;
    };
    for (SectionRef& part : song.flow) follow(part.name, part.occurrence);
    for (SectionSetup& section : song.sections) follow(section.name, section.occurrence);
    return {};
}

Result<void> setChordInversion(Setlist& setlist, int songIndex, const QString& chord, int inversion)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (chord.trimmed().isEmpty()) return fail(ErrorCode::InvalidData, u"Which chord? (no name given)"_s);
    if (inversion < -1 || inversion > limits::kMaxChordInversion) {
        return fail(ErrorCode::OutOfRange,
                    u"A chord's inversion is 0 (root position) to %1 (got %2)"_s.arg(limits::kMaxChordInversion).arg(inversion));
    }
    auto& chosen = setlist.songs.at(toIndex(songIndex)).chordInversions;
    if (inversion < 0) chosen.erase(chord.trimmed());
    else chosen[chord.trimmed()] = inversion;
    return {};
}

Result<void> setSongLoopSync(Setlist& setlist, int songIndex, bool sync)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    setlist.songs.at(toIndex(songIndex)).loopSync = sync;
    return {};
}

Result<void> setSongLoopBars(Setlist& setlist, int songIndex, int bars)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (bars < 0 || bars > limits::kMaxLoopBars) {
        return fail(ErrorCode::OutOfRange, u"A loop is 1 to %1 bars long (or open)"_s.arg(limits::kMaxLoopBars));
    }
    setlist.songs.at(toIndex(songIndex)).loopBars = bars;
    return {};
}

Result<void> setLoopControls(Setlist& setlist, const LoopControls& controls)
{
    if (auto r = validateLoopControls(controls); !r) return r;
    setlist.loopControls = controls;
    return {};
}

Result<void> setMixerControls(Setlist& setlist, const MixerControls& controls)
{
    if (auto r = validateMixerControls(controls); !r) return r;
    setlist.mixerControls = controls;
    return {};
}

Result<void> setPanelControls(Setlist& setlist, const PanelControls& controls)
{
    if (auto r = validatePanelControls(controls); !r) return r;
    setlist.panelControls = controls;
    return {};
}

Result<void> setSectionSetup(Setlist& setlist, int songIndex, const SectionSetup& setup)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    Song edited = setlist.songs.at(toIndex(songIndex));
    SectionSetup clean = setup;
    clean.name = clean.name.simplified();
    const auto same = std::ranges::find_if(edited.sections, [&clean](const SectionSetup& s) {
        return s.occurrence == clean.occurrence && s.name.compare(clean.name, Qt::CaseInsensitive) == 0;
    });
    if (same != edited.sections.end()) *same = clean;
    else edited.sections.push_back(clean);
    if (auto r = validateSections(edited, u"Song %1"_s.arg(songIndex + 1)); !r) return r;
    setlist.songs.at(toIndex(songIndex)) = std::move(edited);
    return {};
}

Result<int> duplicateSong(Setlist& setlist, int songIndex)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (setlist.songs.size() >= toIndex(limits::kMaxSongs)) {
        return fail(ErrorCode::LimitExceeded, u"A setlist can hold at most %1 songs"_s.arg(limits::kMaxSongs));
    }
    // The copy plays the same instruments (loaded once, changed together),
    // until it takes its own copy of one.
    linkForSharing(setlist.songs.at(toIndex(songIndex)));
    Song copy = withFreshIds(setlist.songs.at(toIndex(songIndex)));
    copy.name = copyName(copy.name);
    setlist.songs.insert(setlist.songs.begin() + songIndex + 1, std::move(copy));
    return songIndex + 1;
}

Result<int> duplicatePatch(Setlist& setlist, Cursor cursor)
{
    const Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    Song& song = setlist.songs.at(toIndex(cursor.song));
    if (song.patches.size() >= toIndex(limits::kMaxPatchesPerSong)) {
        return fail(ErrorCode::LimitExceeded, u"A song can hold at most %1 patches"_s.arg(limits::kMaxPatchesPerSong));
    }
    Patch copy = withFreshIds(*patch);
    copy.name = copyName(copy.name);
    song.patches.insert(song.patches.begin() + cursor.patch + 1, std::move(copy));
    return cursor.patch + 1;
}

Result<void> removeSong(Setlist& setlist, int songIndex)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    setlist.songs.erase(setlist.songs.begin() + songIndex);
    return {};
}

Result<void> removePatch(Setlist& setlist, Cursor cursor)
{
    if (patchAt(setlist, cursor) == nullptr) return missing(u"Patch"_s);
    Song& song = setlist.songs.at(toIndex(cursor.song));
    if (song.patches.size() == 1) {
        return fail(ErrorCode::InvalidData, u"A song needs at least one patch; delete the song instead"_s);
    }
    song.patches.erase(song.patches.begin() + cursor.patch);
    return {};
}

Result<void> moveSong(Setlist& setlist, int from, int to)
{
    if (!inRange(from, setlist.songs.size()) || !inRange(to, setlist.songs.size())) {
        return missing(u"Song position"_s);
    }
    moveElement(setlist.songs, from, to);
    return {};
}

Result<void> movePatch(Setlist& setlist, int songIndex, int from, int to)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    auto& patches = setlist.songs.at(toIndex(songIndex)).patches;
    if (!inRange(from, patches.size()) || !inRange(to, patches.size())) return missing(u"Patch position"_s);
    moveElement(patches, from, to);
    return {};
}

Result<int> addChannel(Setlist& setlist, Cursor cursor, const PluginSlot& instrument)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    if (patch->channels.size() >= toIndex(limits::kMaxChannelsPerPatch)) {
        return fail(ErrorCode::LimitExceeded,
                    u"A patch can hold at most %1 channels"_s.arg(limits::kMaxChannelsPerPatch));
    }
    // The same instrument again (another preset of it): "Piano V2 2", told apart.
    const QString base = instrument.displayName.trimmed();
    QString name = base;
    const auto taken = [patch](const QString& n) {
        return std::ranges::any_of(patch->channels, [&n](const Channel& c) { return c.name.compare(n, Qt::CaseInsensitive) == 0; });
    };
    for (int number = 2; taken(name) && number <= limits::kMaxChannelsPerPatch + 1; ++number) {
        name = u"%1 %2"_s.arg(base).arg(number);
    }
    Channel channel = makeChannel(name.left(limits::kMaxNameLength));
    channel.instrument = instrument;
    if (auto valid = validateChannel(channel, u"New channel"_s); !valid) return tl::unexpected(valid.error());
    patch->channels.push_back(std::move(channel));
    return static_cast<int>(patch->channels.size()) - 1;
}

Result<int> addInputChannel(Setlist& setlist, Cursor cursor, const QString& name, int inputLeft, int inputRight)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    if (patch->channels.size() >= toIndex(limits::kMaxChannelsPerPatch)) {
        return fail(ErrorCode::LimitExceeded,
                    u"A patch can hold at most %1 channels"_s.arg(limits::kMaxChannelsPerPatch));
    }
    if (inputLeft < 1) return fail(ErrorCode::OutOfRange, u"Choose which input the channel plays"_s);
    Channel channel = makeChannel(name.trimmed());
    channel.inputLeft = inputLeft;
    channel.inputRight = inputRight;
    if (auto valid = validateChannel(channel, u"New channel"_s); !valid) return tl::unexpected(valid.error());
    patch->channels.push_back(std::move(channel));
    return static_cast<int>(patch->channels.size()) - 1;
}

Result<void> setSongBackingTrack(Setlist& setlist, int songIndex, const QString& fileName)
{
    if (songIndex < 0 || static_cast<std::size_t>(songIndex) >= setlist.songs.size()) return missing(u"Song"_s);
    if (!fileName.isEmpty()) {
        if (auto valid = validateFileName(fileName, u"The backing track"_s); !valid) return valid;
    }
    setlist.songs.at(static_cast<std::size_t>(songIndex)).backingTrack = fileName;
    return {};
}

Result<void> setSongStems(Setlist& setlist, int songIndex, const std::vector<BackingStem>& stems)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (stems.size() > static_cast<std::size_t>(limits::kMaxStems)) {
        return fail(ErrorCode::LimitExceeded, u"A song can have at most %1 stems"_s.arg(limits::kMaxStems));
    }
    for (const BackingStem& stem : stems) {
        if (auto valid = validateFileName(stem.file, u"A stem"_s); !valid) return valid;
        if (!std::isfinite(stem.volumeDb) || stem.volumeDb < limits::kMinVolumeDb || stem.volumeDb > limits::kMaxVolumeDb) {
            return fail(ErrorCode::OutOfRange, u"A stem's level must be between %1 and %2 dB"_s.arg(limits::kMinVolumeDb).arg(limits::kMaxVolumeDb));
        }
        if (stem.outputPair < 0 || stem.outputPair > limits::kMaxOutputPair) {
            return fail(ErrorCode::OutOfRange, u"There are no outputs %1-%2"_s.arg((2 * stem.outputPair) + 1).arg((2 * stem.outputPair) + 2));
        }
    }
    setlist.songs.at(toIndex(songIndex)).stems = stems;
    return {};
}

Result<void> setSongMarkers(Setlist& setlist, int songIndex, std::vector<TrackMarker> markers)
{
    if (!inRange(songIndex, setlist.songs.size())) return missingSong(songIndex);
    if (markers.size() > static_cast<std::size_t>(limits::kMaxTrackMarkers)) {
        return fail(ErrorCode::LimitExceeded, u"A song can have at most %1 markers"_s.arg(limits::kMaxTrackMarkers));
    }
    for (TrackMarker& marker : markers) {
        marker.name = marker.name.trimmed();
        if (marker.name.isEmpty() || marker.name.size() > limits::kMaxNameLength) {
            return fail(ErrorCode::InvalidData, u"A marker needs a name of 1 to %1 characters"_s.arg(limits::kMaxNameLength));
        }
        if (!std::isfinite(marker.seconds) || marker.seconds < 0.0 || marker.seconds > limits::kMaxTrackSeconds) {
            return fail(ErrorCode::OutOfRange, u"A marker must be within the first %1 minutes"_s.arg(limits::kMaxTrackSeconds / 60.0));
        }
    }
    std::ranges::stable_sort(markers, {}, &TrackMarker::seconds); // in the track's order
    setlist.songs.at(toIndex(songIndex)).markers = std::move(markers);
    return {};
}

Result<void> removeChannel(Setlist& setlist, Cursor cursor, int channelIndex)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    if (!inRange(channelIndex, patch->channels.size())) return missing(u"Channel %1"_s.arg(channelIndex + 1));
    patch->channels.erase(patch->channels.begin() + channelIndex);
    return {};
}

Result<void> addEffect(Setlist& setlist, Cursor cursor, int channelIndex, const PluginSlot& effect)
{
    return updateChannel(setlist, cursor, channelIndex, [&effect](Channel& channel) {
        channel.effects.push_back(effect);
    });
}

Result<void> removeEffect(Setlist& setlist, Cursor cursor, int channelIndex, int effectIndex)
{
    const Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr || !inRange(channelIndex, patch->channels.size())) {
        return missing(u"Channel %1"_s.arg(channelIndex + 1));
    }
    if (!inRange(effectIndex, patch->channels.at(toIndex(channelIndex)).effects.size())) {
        return missing(u"Effect %1"_s.arg(effectIndex + 1));
    }
    return updateChannel(setlist, cursor, channelIndex, [effectIndex](Channel& channel) {
        channel.effects.erase(channel.effects.begin() + effectIndex);
    });
}

Result<void> updateChannel(Setlist& setlist, Cursor cursor, int channelIndex,
                           const std::function<void(Channel&)>& edit)
{
    Patch* patch = patchAt(setlist, cursor);
    if (patch == nullptr) return missing(u"Patch"_s);
    if (!inRange(channelIndex, patch->channels.size())) return missing(u"Channel %1"_s.arg(channelIndex + 1));

    Channel updated = patch->channels.at(toIndex(channelIndex));
    edit(updated);
    updated.name = updated.name.trimmed();
    if (auto valid = validateChannel(updated, u"Channel"_s); !valid) return valid;
    patch->channels.at(toIndex(channelIndex)) = std::move(updated);
    return {};
}

} // namespace gigchain::core
