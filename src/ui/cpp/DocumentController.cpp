#include "DocumentController.h"
#include "FreezeWatchdog.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Chart.h"
#include "gigchain/core/ChartEdit.h"
#include "gigchain/core/Checks.h"
#include "gigchain/core/Chords.h"
#include "gigchain/core/Practice.h"
#include "gigchain/core/SongMap.h"

#include <QStringDecoder>

#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>

#include "gigchain/core/Editing.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/Sections.h"
#include "gigchain/core/SetlistFile.h"
#include "gigchain/engine/IEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QSettings>

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <utility>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcUi, "gigchain.ui")

namespace gigchain::ui {
namespace {

constexpr auto kLastFileKey = "session/lastFile"_L1;
const QString kRecentKey = u"session/recentFiles"_s;
const QString kRecentDetailsKey = u"session/recentDetails"_s; // path -> {songs, opened}
constexpr int kMaxRecent = 5; // setlists in File > Recent

} // namespace

DocumentController::DocumentController(engine::IEngine& engine, QSettings& settings, QObject* parent)
    : QObject(parent), m_engine(engine), m_settings(settings), m_cursor(core::firstPatch(m_setlist))
{
    // A different current song (or setlist) means a different chart.
    connect(this, &DocumentController::currentChanged, this, &DocumentController::chartChanged);
    connect(this, &DocumentController::currentChanged, this, &DocumentController::chordInversionsChanged);
    resetSelectedChannel();
    applyCurrentPatchToEngine();
}

const core::Patch* DocumentController::currentPatch() const
{
    return core::patchAt(m_setlist, m_cursor);
}

QString DocumentController::currentSongName() const
{
    return hasPatch() ? m_setlist.songs.at(static_cast<std::size_t>(m_cursor.song)).name : QString();
}

QString DocumentController::currentPatchName() const
{
    const core::Patch* patch = currentPatch();
    return patch != nullptr ? patch->name : QString();
}

QString DocumentController::nextPatchLabel() const
{
    const core::Cursor next = core::nextPatch(m_setlist, m_cursor);
    const core::Patch* patch = core::patchAt(m_setlist, next);
    if (patch == nullptr || next == m_cursor) return {};
    if (next.song == m_cursor.song) return patch->name;
    return u"%1 — %2"_s.arg(m_setlist.songs.at(static_cast<std::size_t>(next.song)).name, patch->name);
}

void DocumentController::setSelectedChannel(int index)
{
    const core::Patch* patch = currentPatch();
    const int count = patch != nullptr ? static_cast<int>(patch->channels.size()) : 0;
    const int clamped = (index >= 0 && index < count) ? index : -1;
    if (clamped == m_selectedChannel) return;
    m_selectedChannel = clamped;
    emit selectedChannelChanged();
}

QString DocumentController::displayName() const
{
    return m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
}

// ---------------------------------------------------------------- navigation

void DocumentController::nextPatch()
{
    setCursor(core::nextPatch(m_setlist, m_cursor));
}

void DocumentController::previousPatch()
{
    setCursor(core::previousPatch(m_setlist, m_cursor));
}

void DocumentController::nextSong()
{
    setCursor(core::nextSong(m_setlist, m_cursor));
}

void DocumentController::previousSong()
{
    setCursor(core::previousSong(m_setlist, m_cursor));
}

bool DocumentController::selectPatch(int song, int patch)
{
    const core::Cursor target{song, patch};
    if (core::patchAt(m_setlist, target) == nullptr) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("That patch does not exist")});
    }
    setCursor(target);
    return true;
}

bool DocumentController::selectProgram(int program)
{
    if (!m_hasSetlist) {
        qCInfo(lcUi) << "Program change" << program + 1 << "ignored: no setlist is open";
        return false;
    }
    const core::Cursor target(m_cursor.song, program);
    if (program < 0 || core::patchAt(m_setlist, target) == nullptr) {
        const core::Song* song = m_cursor.song >= 0 && static_cast<std::size_t>(m_cursor.song) < m_setlist.songs.size()
                                     ? &m_setlist.songs.at(static_cast<std::size_t>(m_cursor.song))
                                     : nullptr;
        const QString text = tr("Program %1: %2 has %3 patches")
                                 .arg(program + 1)
                                 .arg(song != nullptr ? song->name : tr("this song"))
                                 .arg(song != nullptr ? song->patches.size() : 0);
        qCInfo(lcUi).noquote() << text;
        reportMessage(text, Notifications::Warning);
        return false;
    }
    setCursor(target);
    return true;
}

// ---------------------------------------------------------------- structure

bool DocumentController::addSong()
{
    if (!m_hasSetlist) newSetlist();
    const auto current = currentPatchId();
    const auto index = core::addSong(m_setlist, tr("Song %1").arg(m_setlist.songs.size() + 1));
    if (!index) return report(index.error());
    commitStructure(core::Cursor{*index, 0}, current);
    return true;
}

bool DocumentController::addPatch(int song)
{
    const bool validSong = song >= 0 && static_cast<std::size_t>(song) < m_setlist.songs.size();
    const auto count = validSong ? m_setlist.songs.at(static_cast<std::size_t>(song)).patches.size() : 0;
    const auto current = currentPatchId();
    const auto index = core::addPatch(m_setlist, song, tr("Patch %1").arg(count + 1));
    if (!index) return report(index.error());
    commitStructure(core::Cursor{song, *index}, current);
    return true;
}

bool DocumentController::renameSong(int song, const QString& name)
{
    if (auto r = core::renameSong(m_setlist, song, name); !r) return report(r.error());
    commitRename();
    return true;
}

QString DocumentController::currentChart() const
{
    const int song = songIndex();
    return song >= 0 && static_cast<std::size_t>(song) < m_setlist.songs.size()
               ? m_setlist.songs.at(static_cast<std::size_t>(song)).chart
               : QString();
}

bool DocumentController::setSongChart(int song, const QString& chordPro)
{
    if (auto r = core::setSongChart(m_setlist, song, chordPro); !r) return report(r.error());
    m_coalesceKey = u"chart:%1"_s.arg(song);
    setDirty(true);
    if (song == m_cursor.song) applySectionsToEngine(); // its sections come from the chart
    emit chartChanged();
    return true;
}

bool DocumentController::applyChartEdit(const core::Result<QString>& edited, const QString& coalesceKey)
{
    if (!edited) return report(edited.error());
    const int song = m_cursor.song;
    if (auto r = core::setSongChart(m_setlist, song, *edited); !r) return report(r.error());
    m_coalesceKey = coalesceKey; // empty: a step of its own
    setDirty(true);
    applySectionsToEngine(); // its sections come from the chart
    emit chartChanged();
    return true;
}

bool DocumentController::placeChordAt(int line, int at, const QString& chord)
{
    return applyChartEdit(core::placeChord(currentChart(), line, at, chord), {});
}

bool DocumentController::renameChordAt(int line, int chord, const QString& name)
{
    return applyChartEdit(core::changeChord(currentChart(), line, chord, name), {});
}

bool DocumentController::moveChordTo(int line, int chord, int toLine, int toAt)
{
    return applyChartEdit(core::moveChord(currentChart(), line, chord, toLine, toAt), {});
}

bool DocumentController::setLineLyrics(int line, const QString& lyrics)
{
    // Typing a line: one undo step, not one per letter.
    return applyChartEdit(core::editLyrics(currentChart(), line, lyrics), u"lyrics:%1:%2"_s.arg(m_cursor.song).arg(line));
}

bool DocumentController::splitChartLine(int line, int at)
{
    return applyChartEdit(core::splitLyricLine(currentChart(), line, at), {});
}

QVariantMap DocumentController::currentChordInversions() const
{
    QVariantMap map;
    const int song = songIndex();
    if (song < 0 || static_cast<std::size_t>(song) >= m_setlist.songs.size()) return map;
    for (const auto& [chord, inversion] : m_setlist.songs.at(static_cast<std::size_t>(song)).chordInversions) map.insert(chord, inversion);
    return map;
}

std::vector<core::SectionRef> DocumentController::currentSongFlow() const
{
    const int song = songIndex();
    if (song < 0 || static_cast<std::size_t>(song) >= m_setlist.songs.size()) return {};
    return m_setlist.songs.at(static_cast<std::size_t>(song)).flow;
}

bool DocumentController::songFlowSet() const
{
    return !currentSongFlow().empty();
}

QVariantList DocumentController::chartParts() const
{
    const std::vector<core::ChartSection> sections = core::chartSections(core::parseChordPro(currentChart()));
    std::vector<core::SectionRef> parts;
    parts.reserve(sections.size());
    std::ranges::transform(sections, std::back_inserter(parts),
                           [](const core::ChartSection& s) { return core::SectionRef{.name = s.name, .occurrence = s.occurrence}; });
    return partsList(parts, sections);
}

QVariantList DocumentController::songFlow() const
{
    const std::vector<core::ChartSection> sections = core::chartSections(core::parseChordPro(currentChart()));
    const std::vector<core::SectionRef> flow = currentSongFlow();
    return flow.empty() ? chartParts() : partsList(flow, sections);
}

QVariantList DocumentController::partsList(const std::vector<core::SectionRef>& flow, const std::vector<core::ChartSection>& sections)
{
    QVariantList list;
    for (const core::SectionRef& part : flow) {
        const int s = core::sectionIndexOf(sections, part);
        // How the chart names it ("Chorus"; the second of two alike: "Chorus (2)").
        const bool alike = std::ranges::count_if(sections, [&part](const core::ChartSection& c) {
                               return c.name.compare(part.name, Qt::CaseInsensitive) == 0;
                           }) > 1;
        const QString label = s >= 0 ? sections.at(static_cast<std::size_t>(s)).name : part.name;
        list << QVariantMap{{u"name"_s, part.name},
                            {u"occurrence"_s, part.occurrence},
                            {u"section"_s, s},
                            {u"label"_s, alike ? u"%1 (%2)"_s.arg(label).arg(part.occurrence) : label}};
    }
    return list;
}

bool DocumentController::setSongFlow(const QVariantList& flow)
{
    const int song = songIndex();
    const std::vector<core::ChartSection> sections = core::chartSections(core::parseChordPro(currentChart()));
    std::vector<core::SectionRef> parts;
    for (const QVariant& item : flow) {
        const QVariantMap map = item.toMap();
        const core::SectionRef part{.name = map.value(u"name"_s).toString().trimmed(),
                                    .occurrence = std::max(1, map.value(u"occurrence"_s, 1).toInt())};
        if (core::sectionIndexOf(sections, part) < 0) {
            return report(core::Error{core::ErrorCode::InvalidData,
                                      tr("The chart has no section \"%1\" to put in the song's flow").arg(part.name)});
        }
        parts.push_back(part);
    }
    if (auto r = core::setSongFlow(m_setlist, song, parts); !r) return report(r.error());
    setDirty(true);
    applySectionsToEngine(); // chord follow keeps to the new flow
    emit sectionsChanged();
    emit chartChanged(); // (the chords light along it)
    return true;
}

QVariantMap DocumentController::chordDiagram(const QString& name, int inversion) const
{
    const auto shape = core::parseChordName(name);
    QVariantMap diagram{{u"name"_s, name}, {u"understood"_s, shape.has_value()}};
    if (!shape) return diagram;
    const QVariantMap chosenAll = currentChordInversions();
    const int chosen = chosenAll.contains(name) ? chosenAll.value(name).toInt() : -1;
    const int count = core::inversionCount(*shape);
    int shown = inversion >= 0 ? inversion : std::max(chosen, 0);
    if (shown >= count) shown = 0;
    // Note names in the chord's own spelling: flats for a flat chord.
    static const QStringList kSharps{u"C"_s, u"C#"_s, u"D"_s, u"D#"_s, u"E"_s, u"F"_s, u"F#"_s, u"G"_s, u"G#"_s, u"A"_s, u"A#"_s, u"B"_s};
    static const QStringList kFlats{u"C"_s, u"Db"_s, u"D"_s, u"Eb"_s, u"E"_s, u"F"_s, u"Gb"_s, u"G"_s, u"Ab"_s, u"A"_s, u"Bb"_s, u"B"_s};
    const bool flat = name.size() > 1 && name.at(1) == u'b';
    const auto names = [&](const std::vector<int>& notes) {
        QStringList list;
        for (const int n : notes) list << (flat ? kFlats : kSharps).at(n % 12);
        return list.join(u' ');
    };
    const auto asList = [](const std::vector<int>& notes) {
        QVariantList list;
        for (const int n : notes) list << n;
        return list;
    };
    QStringList inversions;
    for (int i = 0; i < count; ++i) inversions << core::inversionName(i);
    const std::vector<int> left = core::leftHandNotes(*shape, core::LeftHand::Bass);
    const std::vector<int> right = core::chordInversion(*shape, shown);
    diagram.insert(u"inversion"_s, shown);
    diagram.insert(u"chosen"_s, chosen);
    diagram.insert(u"inversions"_s, inversions);
    diagram.insert(u"left"_s, asList(left));
    diagram.insert(u"right"_s, asList(right));
    diagram.insert(u"leftNames"_s, names(left));
    diagram.insert(u"rightNames"_s, names(right));
    return diagram;
}

bool DocumentController::setChordInversion(const QString& name, int inversion)
{
    const auto shape = core::parseChordName(name);
    if (!shape) return report(core::Error{core::ErrorCode::InvalidData, tr("\"%1\" is not a chord this app knows").arg(name)});
    if (inversion >= core::inversionCount(*shape)) {
        return report(core::Error{core::ErrorCode::OutOfRange,
                                  tr("%1 has %2 inversions: there is no inversion %3").arg(name).arg(core::inversionCount(*shape)).arg(inversion)});
    }
    if (auto r = core::setChordInversion(m_setlist, songIndex(), name, inversion); !r) return report(r.error());
    setDirty(true);
    emit chordInversionsChanged();
    return true;
}

bool DocumentController::editLineAndSplit(int line, const QString& lyrics, int at)
{
    const auto edited = core::editLyrics(currentChart(), line, lyrics);
    if (!edited) return report(edited.error());
    return applyChartEdit(core::splitLyricLine(*edited, line, at), {});
}

bool DocumentController::joinChartLine(int line)
{
    return applyChartEdit(core::joinWithPrevious(currentChart(), line), {});
}

bool DocumentController::renameChartSection(int line, const QString& label)
{
    return applyChartEdit(core::renameSection(currentChart(), line, label), {});
}

bool DocumentController::addChartSection(const QString& label)
{
    return applyChartEdit(core::appendSection(currentChart(), label), {});
}

QStringList DocumentController::currentChartChords() const
{
    QStringList chords;
    const core::Chart chart = core::parseChordPro(currentChart());
    for (const core::ChartLine& line : chart.lines) {
        for (const QString& chord : line.chords()) {
            if (!chords.contains(chord)) chords << chord;
        }
    }
    return chords;
}

namespace {

// A line of words as the chart editor's cells: [{at, text, space, chords:
// [{name, index, steps, understood}]}]. A cell is a word (`space`: a space
// after it), split where a chord sits inside it; a chord after the last
// word is a cell with no words. `segments` are chartLines()'s for the line.
QVariantList wordCells(const QString& lyrics, const QVariantList& segments)
{
    const int length = static_cast<int>(lyrics.size());
    std::map<int, QVariantList> chordsAt;
    for (const QVariant& s : segments) {
        const QVariantMap segment = s.toMap();
        if (segment.value(u"chord"_s).toString().isEmpty()) continue;
        chordsAt[segment.value(u"at"_s).toInt()] << QVariantMap{{u"name"_s, segment.value(u"chord"_s)},
                                                              {u"index"_s, segment.value(u"chordIndex"_s)},
                                                              {u"steps"_s, segment.value(u"steps"_s)},
                                                              {u"understood"_s, segment.value(u"understood"_s)}};
    }
    // Where cells start: each word, and each chord.
    std::set<int> starts;
    for (int c = 0; c < length; ++c) {
        if (!lyrics.at(c).isSpace() && (c == 0 || lyrics.at(c - 1).isSpace())) starts.insert(c);
    }
    for (const auto& [place, chords] : chordsAt) starts.insert(std::clamp(place, 0, length));
    if (!starts.empty() && *starts.begin() > 0) starts.insert(0); // (spaces before the first word)
    QVariantList cells;
    for (auto it = starts.begin(); it != starts.end(); ++it) {
        const int from = *it;
        const int to = std::next(it) == starts.end() ? length : *std::next(it);
        const QString piece = lyrics.mid(from, std::max(to - from, 0));
        const auto found = chordsAt.find(from);
        cells << QVariantMap{{u"at"_s, from},
                             {u"text"_s, piece.trimmed()},
                             {u"space"_s, !piece.isEmpty() && piece.back().isSpace()},
                             {u"chords"_s, found != chordsAt.end() ? found->second : QVariantList{}}};
    }
    return cells;
}

} // namespace

// Edits made while one lives are one undo step, recorded when it ends.
class DocumentController::OneUndoStep
{
public:
    explicit OneUndoStep(DocumentController& doc) : m_doc(doc) { ++m_doc.m_holdUndo; }
    OneUndoStep(const OneUndoStep&) = delete;
    OneUndoStep& operator=(const OneUndoStep&) = delete;
    OneUndoStep(OneUndoStep&&) = delete;
    OneUndoStep& operator=(OneUndoStep&&) = delete;
    ~OneUndoStep()
    {
        if (--m_doc.m_holdUndo != 0 || !m_doc.m_dirty) return;
        try {
            m_doc.recordEdit();
        } catch (const std::exception& e) { // (out of memory for the undo copy: the edit stands, said)
            qCCritical(lcUi) << "The edit could not be made an undo step:" << e.what();
        }
    }

private:
    DocumentController& m_doc;
};

bool DocumentController::pasteChart(int song, const QString& pasted)
{
    if (pasted.trimmed().isEmpty()) {
        return report(core::Error{core::ErrorCode::InvalidData, tr("There is no text to paste")});
    }
    // Before reading it: a whole book would freeze the app for seconds.
    if (pasted.size() > core::limits::kMaxChartSourceLength) {
        return report(core::Error{core::ErrorCode::LimitExceeded,
                                  tr("That text is too long for a chart (%1 characters; a song is at most %2)")
                                      .arg(pasted.size())
                                      .arg(core::limits::kMaxChartSourceLength)});
    }
    const core::ImportedSheet sheet = core::importChordSheet(pasted);
    const OneUndoStep oneStep(*this); // the chart, name, key, tempo and time: one Ctrl+Z
    if (song < 0 || static_cast<std::size_t>(song) >= m_setlist.songs.size()) {
        // No song to paste into (an empty setlist): the paste makes one.
        if (!m_hasSetlist) newSetlist();
        const QString name = sheet.title.isEmpty() ? tr("Song %1").arg(m_setlist.songs.size() + 1) : sheet.title;
        const auto current = currentPatchId();
        const auto index = core::addSong(m_setlist, name);
        if (!index) return report(index.error());
        commitStructure(core::Cursor{*index, 0}, current);
        song = *index;
    }
    const core::Song& before = m_setlist.songs.at(static_cast<std::size_t>(song));
    if (!setSongChart(song, sheet.chart)) return false; // reported
    // A placeholder name ("Song 3") takes the sheet's title; a name the user
    // chose stays.
    static const QRegularExpression kPlaceholder(uR"(^Song \d+$)"_s);
    if (!sheet.title.isEmpty() && kPlaceholder.match(before.name).hasMatch()) {
        if (auto r = core::renameSong(m_setlist, song, sheet.title); !r) return report(r.error());
        commitRename();
    }
    const core::Song& now = m_setlist.songs.at(static_cast<std::size_t>(song));
    const QString key = now.key.isEmpty() ? sheet.key : now.key;
    const double tempo = now.tempo > 0.0 ? now.tempo : sheet.tempo;
    const bool tempoTaken = tempo != now.tempo;
    if (key != now.key || tempoTaken) {
        if (auto r = core::setSongKeyAndTempo(m_setlist, song, key, tempo); !r) return report(r.error());
    }
    // A time signature from the chart, when the song has the default (4/4).
    const core::Song& updated = m_setlist.songs.at(static_cast<std::size_t>(song));
    const bool timeTaken = sheet.timeNumerator > 0 && updated.timeNumerator == 4 && updated.timeDenominator == 4 &&
                           (sheet.timeNumerator != 4 || sheet.timeDenominator != 4);
    if (timeTaken) {
        if (auto r = core::setSongTimeSignature(m_setlist, song, sheet.timeNumerator, sheet.timeDenominator); !r) {
            return report(r.error());
        }
    }
    const auto tell = [this](const QString& message) {
        qCInfo(lcUi).noquote() << message;
        reportMessage(message, Notifications::Info);
    };
    if (tempoTaken) tell(tr("Tempo %1 BPM taken from the chart").arg(tempo));
    if (timeTaken) tell(tr("Time signature %1/%2 taken from the chart").arg(sheet.timeNumerator).arg(sheet.timeDenominator));
    if (tempoTaken || timeTaken) {
        setDirty(true);
        if (song == m_cursor.song) {
            applyCurrentSongToEngine();
            applySectionsToEngine();
        }
        emit songChanged();
    }
    return true;
}

QStringList DocumentController::recentFiles() const
{
    return m_settings.value(kRecentKey).toStringList();
}

QVariantList DocumentController::recentSetlists() const
{
    const QVariantMap details = m_settings.value(kRecentDetailsKey).toMap();
    QVariantList list;
    for (const QString& path : recentFiles()) {
        QString name = QFileInfo(path).fileName();
        if (name.endsWith(branding::setlistSuffix(), Qt::CaseInsensitive)) {
            name.chop(branding::setlistSuffix().size());
        } else if (name.endsWith(branding::jsonSetlistSuffix(), Qt::CaseInsensitive)) {
            name.chop(branding::jsonSetlistSuffix().size());
        } else if (name.endsWith(u".json"_s, Qt::CaseInsensitive)) {
            name.chop(5);
        }
        const QVariantMap known = details.value(path).toMap();
        list.append(QVariantMap{
            {u"path"_s, path},
            {u"name"_s, name},
            {u"songs"_s, known.value(u"songs"_s, -1).toInt()},
            {u"opened"_s, known.value(u"opened"_s).toDateTime()},
        });
    }
    return list;
}

void DocumentController::rememberRecent(const QString& path)
{
    QStringList recent = recentFiles();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > kMaxRecent) recent.removeLast();
    m_settings.setValue(kRecentKey, recent);

    // Details for the start screen; only for files still in the list.
    const QVariantMap stored = m_settings.value(kRecentDetailsKey).toMap();
    QVariantMap details;
    for (const QString& kept : recent) {
        if (stored.contains(kept)) details.insert(kept, stored.value(kept));
    }
    details.insert(path, QVariantMap{{u"songs"_s, static_cast<int>(m_setlist.songs.size())},
                                     {u"opened"_s, QDateTime::currentDateTime()}});
    m_settings.setValue(kRecentDetailsKey, details);
    emit recentFilesChanged();
}

void DocumentController::forgetRecent(const QString& path)
{
    QStringList recent = recentFiles();
    if (recent.removeAll(path) == 0) return;
    m_settings.setValue(kRecentKey, recent);
    QVariantMap details = m_settings.value(kRecentDetailsKey).toMap();
    details.remove(path);
    m_settings.setValue(kRecentDetailsKey, details);
    emit recentFilesChanged();
}

void DocumentController::setHasSetlist(bool has)
{
    if (m_hasSetlist == has) return;
    m_hasSetlist = has;
    emit hasSetlistChanged();
}

bool DocumentController::pasteChartFromClipboard(int song)
{
    const QClipboard* clipboard = QGuiApplication::clipboard();
    return pasteChart(song, clipboard != nullptr ? clipboard->text() : QString());
}

bool DocumentController::importChartFile(int song, const QUrl& file)
{
    const QString path = file.toLocalFile();
    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QStringList kText{u"txt"_s, u"cho"_s, u"chopro"_s, u"chordpro"_s, u"crd"_s, u"pro"_s, u"onsong"_s};
    if (suffix == u"pdf"_s) {
        return report(core::Error{core::ErrorCode::InvalidData,
                                  tr("%1 is a PDF, which cannot be edited. Download the song as text or "
                                     "ChordPro instead, or copy its text and paste it.")
                                      .arg(QFileInfo(path).fileName())});
    }
    if (!kText.contains(suffix)) {
        return report(core::Error{core::ErrorCode::InvalidData,
                                  tr("%1 is not a chart file this version can read (text or ChordPro)")
                                      .arg(QFileInfo(path).fileName())});
    }
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        return report(core::Error{core::ErrorCode::FileReadFailed, tr("Could not open %1: %2").arg(path, in.errorString())});
    }
    constexpr qint64 kMaxChartFile = qint64{1024} * 1024; // a chart is a few KB
    if (in.size() > kMaxChartFile) {
        return report(core::Error{core::ErrorCode::FileTooLarge, tr("%1 is too large for a chart").arg(path)});
    }
    const QByteArray bytes = in.readAll();
    // UTF-8, else the Windows code page older chord files use.
    auto utf8 = QStringDecoder(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    if (utf8.hasError()) text = QString::fromLatin1(bytes);
    return pasteChart(song, text);
}

QVariantList DocumentController::chartLines(const QString& chordPro) const
{
    using Kind = core::ChartLine::Kind;
    QVariantList lines;
    const auto chart = core::parseChordPro(chordPro);
    // Which lines are section titles (they carry the section's instruments).
    std::map<int, int> sectionAt;
    const auto sections = core::chartSections(chart);
    for (std::size_t s = 0; s < sections.size(); ++s) sectionAt[sections.at(s).line] = static_cast<int>(s);
    // Chord follow: which steps each chord is (lit when played), along the song's flow.
    const core::SongMap map = core::buildSongMap(chart, currentSongFlow());
    std::map<std::pair<int, int>, QVariantList> stepsAt;
    for (std::size_t s = 0; s < map.steps.size(); ++s) {
        for (const auto& place : map.steps.at(s).places) stepsAt[place] << static_cast<int>(s);
    }
    for (std::size_t i = 0; i < chart.lines.size(); ++i) {
        const core::ChartLine& line = chart.lines.at(i);
        QString kind;
        switch (line.kind) {
        case Kind::Lyrics: kind = u"lyrics"_s; break;
        case Kind::Section: kind = u"section"_s; break;
        case Kind::Comment: kind = u"comment"_s; break;
        case Kind::Blank: kind = u"blank"_s; break;
        case Kind::SectionEnd:
        case Kind::Meta: continue; // not shown
        }
        QVariantList segments;
        int chordIndex = 0;
        int at = 0; // where the segment's words start in the line's words (for editing in place)
        for (const core::ChartSegment& segment : line.segments) {
            QVariantList steps;
            bool understood = true;
            int thisChord = -1;
            if (!segment.chord.isEmpty()) {
                thisChord = chordIndex;
                const auto found = stepsAt.find({static_cast<int>(i), chordIndex++});
                if (found != stepsAt.end()) steps = found->second;
                understood = core::parseChordName(segment.chord).has_value();
            }
            segments << QVariantMap{{u"chord"_s, segment.chord}, {u"text"_s, segment.text},
                                    {u"steps"_s, steps}, {u"understood"_s, understood},
                                    {u"at"_s, at}, {u"chordIndex"_s, thisChord}};
            at += static_cast<int>(segment.text.size());
        }
        const auto section = sectionAt.find(static_cast<int>(i));
        lines << QVariantMap{{u"kind"_s, kind},
                             {u"line"_s, static_cast<int>(i)},
                             {u"label"_s, line.label},
                             {u"lyrics"_s, line.lyrics()},
                             {u"segments"_s, segments},
                             {u"cells"_s, line.kind == Kind::Lyrics ? wordCells(line.lyrics(), segments) : QVariantList{}},
                             {u"sectionIndex"_s, section != sectionAt.end() ? section->second : -1}};
    }
    // No empty space after the last line (a chart usually ends with a newline).
    while (!lines.isEmpty() && lines.last().toMap().value(u"kind"_s).toString() == u"blank"_s) lines.removeLast();
    return lines;
}

bool DocumentController::renamePatch(int song, int patch, const QString& name)
{
    if (auto r = core::renamePatch(m_setlist, core::Cursor{song, patch}, name); !r) return report(r.error());
    commitRename();
    return true;
}

bool DocumentController::duplicateSong(int song)
{
    const auto current = currentPatchId();
    // The copy starts with the plugins' settings as they are now, not as
    // they were last saved.
    if (song >= 0 && static_cast<std::size_t>(song) < m_setlist.songs.size()) {
        core::Setlist one;
        one.songs = {m_setlist.songs.at(static_cast<std::size_t>(song))};
        const std::vector<QString> problems = m_engine.storePluginStates(one); // each logged
        if (!problems.empty()) reportMessage(problems.back(), Notifications::Warning);
        m_setlist.songs.at(static_cast<std::size_t>(song)) = std::move(one.songs.front());
    }
    const auto index = core::duplicateSong(m_setlist, song);
    if (!index) return report(index.error());
    commitStructure(core::Cursor{*index, 0}, current);
    return true;
}

bool DocumentController::duplicatePatch(int song, int patch)
{
    const auto current = currentPatchId();
    const auto index = core::duplicatePatch(m_setlist, core::Cursor{song, patch});
    if (!index) return report(index.error());
    commitStructure(core::Cursor{song, *index}, current);
    return true;
}

bool DocumentController::removeSong(int song)
{
    const auto current = currentPatchId();
    if (auto r = core::removeSong(m_setlist, song); !r) return report(r.error());
    // If the current song went away, land on the song that took its place.
    commitStructure(follow(current, core::Cursor{song, 0}), current);
    return true;
}

bool DocumentController::removePatch(int song, int patch)
{
    const auto current = currentPatchId();
    if (auto r = core::removePatch(m_setlist, core::Cursor{song, patch}); !r) return report(r.error());
    commitStructure(follow(current, core::Cursor{song, patch}), current);
    return true;
}

bool DocumentController::moveSong(int from, int to)
{
    const auto current = currentPatchId();
    if (auto r = core::moveSong(m_setlist, from, to); !r) return report(r.error());
    commitStructure(follow(current, m_cursor), current);
    return true;
}

bool DocumentController::movePatch(int song, int from, int to)
{
    const auto current = currentPatchId();
    if (auto r = core::movePatch(m_setlist, song, from, to); !r) return report(r.error());
    commitStructure(follow(current, m_cursor), current);
    return true;
}

// ---------------------------------------------------------------- channels

bool DocumentController::addChannel(const QString& pluginId, const QString& name)
{
    // The first instrument, with nothing open yet, starts a setlist; the
    // first of an empty setlist starts its first song.
    if (!m_hasSetlist) newSetlist();
    if (m_setlist.songs.empty() && !addSong()) return false;
    const auto index = core::addChannel(m_setlist, m_cursor, core::PluginSlot{.pluginId = pluginId, .displayName = name, .bypass = false, .state = {}});
    if (!index) return report(index.error());
    commitChannels(*index);
    emit channelAdded(*index);
    return true;
}

bool DocumentController::removeChannel(int channel)
{
    if (auto r = core::removeChannel(m_setlist, m_cursor, channel); !r) return report(r.error());
    const core::Patch* patch = currentPatch();
    const int remaining = patch != nullptr ? static_cast<int>(patch->channels.size()) : 0;
    commitChannels(remaining == 0 ? -1 : std::min(channel, remaining - 1));
    return true;
}

bool DocumentController::addEffect(int channel, const QString& pluginId, const QString& name)
{
    if (auto r = core::addEffect(m_setlist, m_cursor, channel, core::PluginSlot{.pluginId = pluginId, .displayName = name, .bypass = false, .state = {}}); !r) {
        return report(r.error());
    }
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::removeEffect(int channel, int effect)
{
    if (auto r = core::removeEffect(m_setlist, m_cursor, channel, effect); !r) return report(r.error());
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::setEffectBypass(int channel, int effect, bool bypass)
{
    if (!effectExists(channel, effect)) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("That effect does not exist")});
    }
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [effect, bypass](core::Channel& c) {
        c.effects.at(static_cast<std::size_t>(effect)).bypass = bypass;
    });
    if (!r) return report(r.error());
    commitChannelField(channel, true); // the engine rebuilds the chain without (or with) it
    return true;
}

bool DocumentController::replaceEffect(int channel, int effect, const QString& pluginId, const QString& name)
{
    if (!effectExists(channel, effect)) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("That effect does not exist")});
    }
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [&](core::Channel& c) {
        c.effects.at(static_cast<std::size_t>(effect)) =
            core::PluginSlot{.pluginId = pluginId, .displayName = name, .bypass = false, .state = {}};
    });
    if (!r) return report(r.error());
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::setChannelInstrument(int channel, const QString& pluginId, const QString& name)
{
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [&](core::Channel& c) {
        // A channel still named after its old instrument takes the new name.
        if (!c.instrument || c.name == c.instrument->displayName) c.name = name;
        c.instrument = core::PluginSlot{.pluginId = pluginId, .displayName = name, .bypass = false, .state = {}};
    });
    if (!r) return report(r.error());
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::setChannelName(int channel, const QString& name)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [&name](core::Channel& c) { c.name = name; }); !r) {
        return report(r.error());
    }
    commitChannelField(channel, false);
    emit sectionsChanged(); // they show the channels' names
    return true;
}

bool DocumentController::setChannelKeyRange(int channel, int low, int high)
{
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [low, high](core::Channel& c) {
        c.keyLow = low;
        c.keyHigh = high;
    });
    if (!r) return report(r.error());
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::setChannelTranspose(int channel, int semitones)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [semitones](core::Channel& c) { c.transpose = semitones; });
        !r) {
        return report(r.error());
    }
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::setChannelMidiChannel(int channel, int midiChannel)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel,
                                     [midiChannel](core::Channel& c) { c.midiChannel = midiChannel; });
        !r) {
        return report(r.error());
    }
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::setChannelVolume(int channel, double volumeDb)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [volumeDb](core::Channel& c) { c.volumeDb = volumeDb; });
        !r) {
        return report(r.error());
    }
    m_engine.setChannelVolume(currentPatch()->channels.at(static_cast<std::size_t>(channel)).id, volumeDb);
    m_coalesceKey = u"volume:%1"_s.arg(channel);
    commitChannelField(channel, false);
    return true;
}

bool DocumentController::setChannelPan(int channel, double pan)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [pan](core::Channel& c) { c.pan = pan; }); !r) {
        return report(r.error());
    }
    m_engine.setChannelPan(currentPatch()->channels.at(static_cast<std::size_t>(channel)).id, pan);
    m_coalesceKey = u"pan:%1"_s.arg(channel);
    commitChannelField(channel, false);
    return true;
}

bool DocumentController::setChannelMute(int channel, bool mute)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [mute](core::Channel& c) { c.mute = mute; }); !r) {
        return report(r.error());
    }
    m_engine.setChannelMute(currentPatch()->channels.at(static_cast<std::size_t>(channel)).id, mute);
    commitChannelField(channel, false);
    return true;
}

bool DocumentController::setChannelSolo(int channel, bool solo)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [solo](core::Channel& c) { c.solo = solo; }); !r) {
        return report(r.error());
    }
    m_engine.setChannelSolo(currentPatch()->channels.at(static_cast<std::size_t>(channel)).id, solo);
    commitChannelField(channel, false);
    return true;
}

bool DocumentController::setChannelVelocityRange(int channel, int low, int high)
{
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [low, high](core::Channel& c) {
        c.velocityLow = low;
        c.velocityHigh = high;
    });
    if (!r) return report(r.error());
    m_coalesceKey = u"velocity:%1"_s.arg(channel);
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::addInputChannel(int inputLeft, int inputRight)
{
    // As for an instrument: the first one starts a setlist and its first song.
    if (!m_hasSetlist) newSetlist();
    if (m_setlist.songs.empty() && !addSong()) return false;
    const QString name = inputRight > 0 ? tr("Input %1+%2").arg(inputLeft).arg(inputRight) : tr("Input %1").arg(inputLeft);
    const auto index = core::addInputChannel(m_setlist, m_cursor, name, inputLeft, inputRight);
    if (!index) return report(index.error());
    commitChannels(*index);
    if (m_engine.audioInputChannels() < std::max(inputLeft, inputRight)) {
        reportMessage(tr("%1 is not open: choose an input device in Settings > Audio").arg(name), Notifications::Warning);
    }
    return true;
}

bool DocumentController::setChannelInput(int channel, int inputLeft, int inputRight)
{
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [inputLeft, inputRight](core::Channel& c) {
        c.inputLeft = inputLeft;
        c.inputRight = inputRight;
    });
    if (!r) return report(r.error());
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::addMapping(int channel, int midiChannel, int controller, int target, quint32 parameter,
                                    const QString& parameterName)
{
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [&](core::Channel& c) {
        // One knob, one parameter per plugin: a knob learned again replaces its old job there.
        std::erase_if(c.mappings, [&](const core::ControlMapping& m) {
            return m.target == target && m.midiChannel == midiChannel && m.controller == controller;
        });
        c.mappings.push_back(core::ControlMapping{.midiChannel = midiChannel,
                                                  .controller = controller,
                                                  .target = target,
                                                  .parameter = parameter,
                                                  .parameterName = parameterName,
                                                  .minimum = 0.0,
                                                  .maximum = 1.0});
    });
    if (!r) return report(r.error());
    commitChannelField(channel, true);
    qCInfo(lcUi).noquote() << "Knob CC" << controller << "(channel" << (midiChannel == 0 ? u"any"_s : QString::number(midiChannel))
                           << ") now moves" << parameterName;
    return true;
}

bool DocumentController::removeMapping(int channel, int mapping)
{
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [mapping](core::Channel& c) {
        if (mapping >= 0 && std::cmp_less(mapping, c.mappings.size())) {
            c.mappings.erase(c.mappings.begin() + mapping);
        }
    });
    if (!r) return report(r.error());
    commitChannelField(channel, true);
    return true;
}

bool DocumentController::setMappingRange(int channel, int mapping, double minimum, double maximum)
{
    const core::Patch* patch = currentPatch();
    if (patch == nullptr || channel < 0 || std::cmp_greater_equal(channel, patch->channels.size()) || mapping < 0 ||
        std::cmp_greater_equal(mapping, patch->channels.at(static_cast<std::size_t>(channel)).mappings.size())) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("That knob mapping does not exist")});
    }
    auto r = core::updateChannel(m_setlist, m_cursor, channel, [mapping, minimum, maximum](core::Channel& c) {
        core::ControlMapping& m = c.mappings.at(static_cast<std::size_t>(mapping));
        m.minimum = minimum;
        m.maximum = maximum;
    });
    if (!r) return report(r.error());
    m_coalesceKey = u"mapping:%1:%2"_s.arg(channel).arg(mapping);
    commitChannelField(channel, true);
    return true;
}

QVariantList DocumentController::mappings(int channel) const
{
    QVariantList list;
    const core::Patch* patch = currentPatch();
    if (patch == nullptr || channel < 0 || std::cmp_greater_equal(channel, patch->channels.size())) return list;
    const core::Channel& c = patch->channels.at(static_cast<std::size_t>(channel));
    for (const core::ControlMapping& m : c.mappings) {
        QString targetName;
        if (m.target < 0) targetName = c.instrument ? c.instrument->displayName : tr("Instrument");
        else if (std::cmp_less(m.target, c.effects.size())) targetName = c.effects.at(static_cast<std::size_t>(m.target)).displayName;
        list << QVariantMap{{u"midiChannel"_s, m.midiChannel}, {u"controller"_s, m.controller},
                            {u"target"_s, m.target},           {u"targetName"_s, targetName},
                            {u"parameter"_s, m.parameter},     {u"parameterName"_s, m.parameterName},
                            {u"minimum"_s, m.minimum},         {u"maximum"_s, m.maximum}};
    }
    return list;
}

void DocumentController::editChannel(int channel, const QString& page)
{
    const core::Patch* patch = currentPatch();
    if (patch == nullptr || channel < 0 || std::cmp_greater_equal(channel, patch->channels.size())) return;
    setSelectedChannel(channel);
    emit channelEditRequested(channel, page);
}

// ---------------------------------------------------------------- song tempo and backing track

const core::Song* DocumentController::currentSong() const
{
    const int song = m_cursor.song;
    return song >= 0 && std::cmp_less(song, m_setlist.songs.size()) ? &m_setlist.songs.at(static_cast<std::size_t>(song))
                                                                   : nullptr;
}

int DocumentController::songTimeNumerator() const
{
    const core::Song* song = currentSong();
    return song != nullptr ? song->timeNumerator : 4;
}

int DocumentController::songTimeDenominator() const
{
    const core::Song* song = currentSong();
    return song != nullptr ? song->timeDenominator : 4;
}

bool DocumentController::songSwitchEarly() const
{
    const core::Song* song = currentSong();
    return song != nullptr && song->switchEarly;
}

bool DocumentController::setSongTimeSignature(int song, int numerator, int denominator)
{
    if (auto r = core::setSongTimeSignature(m_setlist, song, numerator, denominator); !r) return report(r.error());
    setDirty(true);
    if (song == m_cursor.song) {
        applyCurrentSongToEngine();
        applySectionsToEngine();
    }
    emit songChanged();
    return true;
}

bool DocumentController::setSongSwitchEarly(int song, bool early)
{
    if (auto r = core::setSongSwitchEarly(m_setlist, song, early); !r) return report(r.error());
    setDirty(true);
    if (song == m_cursor.song) applySectionsToEngine();
    emit songChanged();
    return true;
}

bool DocumentController::songFollowChords() const
{
    const core::Song* song = currentSong();
    return song == nullptr || song->followChords;
}

bool DocumentController::setSongFollowChords(int song, bool on)
{
    if (auto r = core::setSongFollowChords(m_setlist, song, on); !r) return report(r.error());
    setDirty(true);
    if (song == m_cursor.song) applySectionsToEngine();
    emit songChanged();
    return true;
}

QString DocumentController::followFirstChord() const
{
    return m_songMap.steps.empty() ? QString() : m_songMap.steps.front().name;
}

QString DocumentController::followLabel(int step) const
{
    if (step < 0 || std::cmp_greater_equal(step, m_songMap.steps.size())) return {};
    const auto at = [this](int i) { return m_songMap.steps.at(static_cast<std::size_t>(i)).section; };
    const int section = at(step);
    int first = step;
    while (first > 0 && at(first - 1) == section) --first;
    int last = step;
    while (std::cmp_less(last + 1, m_songMap.steps.size()) && at(last + 1) == section) ++last;
    QString where = tr("chord %1 of %2").arg(step - first + 1).arg(last - first + 1);
    if (section < 0 || std::cmp_greater_equal(section, m_followSectionNames.size())) return where;
    return tr("%1 · %2").arg(m_followSectionNames.at(static_cast<std::size_t>(section)), where);
}

int DocumentController::followLine(int step) const
{
    if (step < 0 || std::cmp_greater_equal(step, m_followLines.size())) return -1;
    return m_followLines.at(static_cast<std::size_t>(step));
}

int DocumentController::followPart(int step) const
{
    if (step < 0 || std::cmp_greater_equal(step, m_songMap.steps.size())) return -1;
    // (Chords before the first section are a part of their own in the map,
    // not one of the flow's sections: the flow's parts count from after them.)
    const bool prelude = !m_songMap.steps.empty() && m_songMap.steps.front().section < 0;
    const int part = m_songMap.steps.at(static_cast<std::size_t>(step)).part - (prelude ? 1 : 0);
    return part >= 0 ? part : -1;
}

bool DocumentController::songLoopSync() const
{
    const core::Song* song = currentSong();
    return song == nullptr || song->loopSync;
}

bool DocumentController::setSongLoopSync(int song, bool sync)
{
    if (auto r = core::setSongLoopSync(m_setlist, song, sync); !r) return report(r.error());
    setDirty(true);
    if (song == m_cursor.song) m_engine.setLoopSync(sync);
    emit songChanged();
    return true;
}

int DocumentController::songLoopBars() const
{
    const core::Song* song = currentSong();
    return song != nullptr ? song->loopBars : 4;
}

bool DocumentController::setSongLoopBars(int song, int bars)
{
    if (auto r = core::setSongLoopBars(m_setlist, song, bars); !r) return report(r.error());
    setDirty(true);
    if (song == m_cursor.song) m_engine.setLoopBars(bars);
    emit songChanged();
    return true;
}

bool DocumentController::setLoopControls(const core::LoopControls& controls)
{
    if (!m_hasSetlist) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("Start or open a setlist first")});
    }
    if (auto r = core::setLoopControls(m_setlist, controls); !r) return report(r.error());
    setDirty(true);
    applyLoopControlsToEngine();
    emit loopControlsChanged();
    return true;
}

void DocumentController::applyLoopControlsToEngine()
{
    const auto trigger = [](const core::LearnedControl& c) {
        if (!c.isSet()) return engine::MidiTrigger{};
        return engine::MidiTrigger{.kind = static_cast<engine::MidiTrigger::Kind>(c.kind),
                                   .channel = static_cast<uint8_t>(c.channel - 1),
                                   .number = static_cast<uint8_t>(c.number)};
    };
    const core::LoopControls& controls = m_setlist.loopControls;
    engine::LoopTriggers buttons{};
    std::ranges::transform(controls.buttons, buttons.begin(), trigger);
    m_engine.setLoopControls(buttons, engine::SelectorKnob{.knob = trigger(controls.selector),
                                                           .mode = static_cast<engine::SelectorKnob::Mode>(controls.selectorMode)});
}

QVariantList DocumentController::currentSections() const
{
    const core::Song* song = currentSong();
    const core::Patch* patch = currentPatch();
    if (song == nullptr || patch == nullptr) return {};
    QVariantList list;
    int index = 0;
    for (const core::ResolvedSection& section : core::resolveSections(*song, *patch)) {
        QVariantList channels;
        for (const core::ChannelId& id : section.live) {
            const auto it = std::ranges::find_if(patch->channels, [&id](const core::Channel& c) { return c.id == id; });
            if (it == patch->channels.end()) continue;
            channels << QVariantMap{{u"channel"_s, static_cast<int>(it - patch->channels.begin())}, {u"name"_s, it->name}};
        }
        // The patch's other instruments, for [+].
        QVariantList choices;
        for (std::size_t c = 0; c < patch->channels.size(); ++c) {
            const core::Channel& channel = patch->channels.at(c);
            if (!channel.instrument || std::ranges::find(section.live, channel.id) != section.live.end()) continue;
            choices << QVariantMap{{u"channel"_s, static_cast<int>(c)}, {u"name"_s, channel.name}};
        }
        list << QVariantMap{{u"index"_s, index++},
                            {u"choices"_s, choices},
                            {u"name"_s, section.chart.name},
                            {u"label"_s, section.chart.label},
                            {u"bars"_s, section.bars},
                            {u"guessed"_s, section.guessed},
                            {u"assigned"_s, section.assigned},
                            {u"channels"_s, channels}};
    }
    return list;
}

QVariantList DocumentController::sectionChoices(int section) const
{
    const QVariantList sections = currentSections();
    return section >= 0 && section < sections.size() ? sections.at(section).toMap().value(u"choices"_s).toList()
                                                     : QVariantList{};
}

std::optional<std::vector<core::ChannelId>> DocumentController::sectionLive(int section) const
{
    const core::Song* song = currentSong();
    const core::Patch* patch = currentPatch();
    if (song == nullptr || patch == nullptr) return std::nullopt;
    const auto sections = core::resolveSections(*song, *patch);
    if (section < 0 || std::cmp_greater_equal(section, sections.size())) return std::nullopt;
    return sections.at(static_cast<std::size_t>(section)).live;
}

bool DocumentController::storeSection(int section, const std::function<void(core::SectionSetup&)>& edit)
{
    const core::Song* song = currentSong();
    const core::Patch* patch = currentPatch();
    const auto sections = song != nullptr && patch != nullptr ? core::resolveSections(*song, *patch)
                                                              : std::vector<core::ResolvedSection>{};
    if (section < 0 || std::cmp_greater_equal(section, sections.size())) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("Section %1 does not exist in this song's chart").arg(section + 1)});
    }
    const core::ChartSection& chart = sections.at(static_cast<std::size_t>(section)).chart;
    core::SectionSetup setup{.name = chart.name, .occurrence = chart.occurrence, .bars = 0, .assigned = false, .channels = {}};
    if (const core::SectionSetup* stored = core::findSectionSetup(*song, chart)) setup = *stored;
    edit(setup);
    if (auto r = core::setSectionSetup(m_setlist, m_cursor.song, setup); !r) return report(r.error());
    setDirty(true);
    applySectionsToEngine();
    return true;
}

bool DocumentController::addSectionChannel(int section, int channel)
{
    const core::Patch* patch = currentPatch();
    if (patch == nullptr || channel < 0 || std::cmp_greater_equal(channel, patch->channels.size())) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("Channel %1 does not exist in this patch").arg(channel + 1)});
    }
    const core::ChannelId id = patch->channels.at(static_cast<std::size_t>(channel)).id;
    std::optional<std::vector<core::ChannelId>> live = sectionLive(section);
    if (!live) return storeSection(section, {}); // reports it
    if (std::ranges::find(*live, id) != live->end()) return true; // already plays there
    live->push_back(id);
    return storeSection(section, [&live](core::SectionSetup& setup) {
        setup.assigned = true;
        setup.channels = *live;
    });
}

bool DocumentController::removeSectionChannel(int section, int channel)
{
    const core::Patch* patch = currentPatch();
    if (patch == nullptr || channel < 0 || std::cmp_greater_equal(channel, patch->channels.size())) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("Channel %1 does not exist in this patch").arg(channel + 1)});
    }
    std::optional<std::vector<core::ChannelId>> live = sectionLive(section);
    if (!live) return storeSection(section, {}); // reports it
    std::erase(*live, patch->channels.at(static_cast<std::size_t>(channel)).id);
    return storeSection(section, [&live](core::SectionSetup& setup) {
        setup.assigned = true; // none left: a silent section
        setup.channels = *live;
    });
}

bool DocumentController::setSectionBars(int section, int bars)
{
    if (bars < 1 || bars > core::limits::kMaxSectionBars) {
        return report(core::Error{core::ErrorCode::OutOfRange,
                                  tr("A section is 1 to %1 bars long").arg(core::limits::kMaxSectionBars)});
    }
    m_coalesceKey = u"bars:%1"_s.arg(section);
    return storeSection(section, [bars](core::SectionSetup& setup) { setup.bars = bars; });
}

void DocumentController::playSong()
{
    if (m_sectionCount == 0) {
        const QString message = tr("This song has no sections to play: give its chart section titles like [Verse] or [Chorus]");
        qCInfo(lcUi).noquote() << message;
        reportMessage(message, Notifications::Info);
        return;
    }
    const int from = std::clamp(m_engine.songPosition().section, 0, m_sectionCount - 1);
    m_engine.playSong(from, m_engine.clickOn()); // the click counts in when it is on
}

void DocumentController::stopSong()
{
    m_engine.stopSong();
    m_engine.stopAllLoops(); // they are kept: Play on a loop starts it again
}

void DocumentController::selectSection(int section)
{
    if (section < 0 || section >= m_sectionCount) {
        report(core::Error{core::ErrorCode::OutOfRange, tr("Section %1 does not exist in this song's chart").arg(section + 1)});
        return;
    }
    m_engine.jumpToSection(section);
}

void DocumentController::nextSection()
{
    if (m_sectionCount == 0) return; // a pedal pressed in a song without sections: nothing to move to
    const int next = m_engine.songPosition().section + 1;
    if (next >= m_sectionCount) {
        qCInfo(lcUi) << "Next section: already at the last one";
        return;
    }
    m_engine.jumpToSection(next);
}

void DocumentController::applySectionsToEngine()
{
    const core::Song* song = currentSong();
    const core::Patch* patch = currentPatch();
    engine::SongSections sections;
    if (song != nullptr && patch != nullptr) {
        sections.patch = patch->id;
        sections.switchEarly = song->switchEarly;
        std::ranges::transform(core::resolveSections(*song, *patch), std::back_inserter(sections.sections),
                               [](const core::ResolvedSection& section) {
                                   return engine::SongSections::Section{.bars = section.bars, .live = section.live};
                               });
    }
    const core::SongId songId = song != nullptr ? song->id : core::SongId{};
    const bool newSong = songId != m_sectionsSong;
    if (newSong) {
        m_engine.stopSong();
        m_engine.clearAllLoops(); // loops belong to the song they were played in
    }
    m_engine.setSongSections(sections);
    m_sectionCount = static_cast<int>(sections.sections.size());
    // A new song starts at its beginning. (Before its chords: a section asked
    // before the chords arrive does not start following them.)
    if (newSong && m_sectionCount > 0) m_engine.jumpToSection(0);
    // Chord follow: the song's chords, when its sections follow them. An
    // edit while playing carries on from the chord at the same place.
    std::optional<std::pair<int, int>> place;
    const int playing = newSong ? -1 : m_engine.chordFollow().step;
    if (playing >= 0 && std::cmp_less(playing, m_songMap.steps.size())) {
        place = m_songMap.steps.at(static_cast<std::size_t>(playing)).places.front();
    }
    const core::Chart chart = song != nullptr && song->followChords ? core::parseChordPro(song->chart) : core::Chart{};
    m_songMap = song != nullptr && song->followChords ? core::buildSongMap(chart, song->flow) : core::SongMap{};
    // Too many chords (or sections) to follow: said once (not on every edit),
    // the tempo leads.
    if (m_songMap.tooLong && (newSong || !m_followTooLong)) {
        const QString message = tr("\"%1\" has too many chords to follow (more than %2 with its repeats played out, "
                                   "or more than %3 sections): its sections follow the tempo")
                                    .arg(song->name)
                                    .arg(core::limits::kMaxFollowSteps)
                                    .arg(core::limits::kMaxSectionsPerSong);
        qCWarning(lcUi).noquote() << message;
        reportMessage(message, Notifications::Warning);
    }
    m_followTooLong = m_songMap.tooLong;
    if (!m_songMap.followable()) m_songMap = {};
    engine::ChordFollowMap chords = engine::followMapOf(m_songMap);
    for (std::size_t i = 0; i < m_songMap.steps.size(); ++i) {
        const core::SongStep& step = m_songMap.steps.at(i);
        const bool here = place && std::ranges::find(step.places, *place) != step.places.end();
        // The same step if it is still there (a repeated line has the place several times).
        if (here && (chords.resumeAt < 0 || std::cmp_equal(i, playing))) chords.resumeAt = static_cast<int>(i);
    }
    if (auto set = m_engine.setChordFollow(chords); !set) {
        // (The engine logged it.) Not followed: the tempo leads.
        m_songMap = {};
        reportMessage(set.error().message, Notifications::Warning);
    }
    // Where each chord is shown and its section's name, worked out once here
    // (not on every chord played: a long chart takes a while to read).
    m_followLines.clear();
    m_followSectionNames.clear();
    if (!m_songMap.steps.empty()) {
        // chartLines() leaves out directives and section ends.
        std::vector<int> shownAt(chart.lines.size() + 1, 0);
        for (std::size_t i = 0; i < chart.lines.size(); ++i) {
            const auto kind = chart.lines.at(i).kind;
            const bool shown = kind != core::ChartLine::Kind::Meta && kind != core::ChartLine::Kind::SectionEnd;
            shownAt.at(i + 1) = shownAt.at(i) + (shown ? 1 : 0);
        }
        m_followLines.reserve(m_songMap.steps.size());
        for (const core::SongStep& step : m_songMap.steps) {
            const int line = step.places.front().first;
            m_followLines.push_back(line >= 0 && std::cmp_less(line, shownAt.size()) ? shownAt.at(static_cast<std::size_t>(line)) : -1);
        }
        std::ranges::transform(core::chartSections(chart), std::back_inserter(m_followSectionNames),
                               [](const core::ChartSection& section) { return section.name; });
    }
    m_sectionsSong = songId;
    emit sectionsChanged();
}

double DocumentController::songTempo() const
{
    const core::Song* song = currentSong();
    return song != nullptr ? song->tempo : 0.0;
}

QString DocumentController::songBackingTrack() const
{
    const core::Song* song = currentSong();
    return song != nullptr ? song->backingTrack : QString();
}

bool DocumentController::setSongTempo(int song, double bpm)
{
    if (song < 0 || std::cmp_greater_equal(song, m_setlist.songs.size())) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("That song does not exist")});
    }
    const QString key = m_setlist.songs.at(static_cast<std::size_t>(song)).key;
    if (auto r = core::setSongKeyAndTempo(m_setlist, song, key, bpm); !r) return report(r.error());
    m_coalesceKey = u"tempo:%1"_s.arg(song);
    setDirty(true);
    if (song == m_cursor.song) applyCurrentSongToEngine();
    emit songChanged();
    return true;
}

bool DocumentController::setSongBackingTrack(int song, const QUrl& file)
{
    if (song < 0 || std::cmp_greater_equal(song, m_setlist.songs.size())) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("That song does not exist")});
    }
    QString fileName;
    if (!file.isEmpty()) {
        if (m_filePath.isEmpty()) {
            return report(core::Error{core::ErrorCode::FileWriteFailed,
                                      tr("Save the setlist first: backing tracks are kept in the setlist's folder")});
        }
        if (!file.isLocalFile()) {
            return report(core::Error{core::ErrorCode::FileNotFound, tr("Only local files can be used: %1").arg(file.toString())});
        }
        const QFileInfo source(file.toLocalFile());
        const QDir folder = QFileInfo(m_filePath).absoluteDir();
        fileName = source.fileName();
        const QString target = folder.filePath(fileName);
        if (QFileInfo(target).absoluteFilePath() != source.absoluteFilePath()) {
            if (QFileInfo::exists(target)) {
                if (QFileInfo(target).size() != source.size()) {
                    return report(core::Error{core::ErrorCode::FileWriteFailed,
                                              tr("The setlist's folder already has a different %1").arg(fileName)});
                }
            } else if (!QFile::copy(source.absoluteFilePath(), target)) {
                return report(core::Error{core::ErrorCode::FileWriteFailed,
                                          tr("Could not copy %1 into the setlist's folder").arg(fileName)});
            }
        }
    }
    if (auto r = core::setSongBackingTrack(m_setlist, song, fileName); !r) return report(r.error());
    setDirty(true);
    if (song == m_cursor.song) applyCurrentSongToEngine();
    emit songChanged();
    return true;
}

void DocumentController::applyCurrentSongToEngine()
{
    const core::Song* song = currentSong();
    if (song != nullptr && song->tempo > 0.0) m_engine.setTempo(song->tempo); // a song without one keeps the tempo playing
    if (song != nullptr) m_engine.setTimeSignature(song->timeNumerator, song->timeDenominator);
    m_engine.setLoopSync(song == nullptr || song->loopSync);
    m_engine.setLoopBars(song != nullptr ? song->loopBars : 4);
    applyLoopControlsToEngine(); // the setlist's (a newly opened one too)
    const QString track = song != nullptr && !song->backingTrack.isEmpty() && !m_filePath.isEmpty()
                              ? QFileInfo(m_filePath).absoluteDir().filePath(song->backingTrack)
                              : QString();
    m_engine.setBackingTrack(track);
}

// ---------------------------------------------------------------- files

void DocumentController::newSetlist()
{
    GC_ONLY_MAIN_THREAD();
    m_setlist = {}; // empty: the user adds (or pastes) songs
    setHasSetlist(true);
    m_engine.preload(m_setlist); // unloads the previous setlist's plugins
    setFilePath({});
    setDirty(false);
    emit structureChanged();
    setCursor(core::firstPatch(m_setlist), true);
    resetUndo();
}

bool DocumentController::open(const QString& path)
{
    GC_ONLY_MAIN_THREAD();
    auto loaded = core::loadSetlistFile(path);
    if (!loaded) {
        if (loaded.error().code == core::ErrorCode::FileNotFound) forgetRecent(path); // moved or deleted
        return report(loaded.error());
    }
    m_setlist = std::move(*loaded);
    setHasSetlist(true);
    // Every sound up front (behind the splash or loading overlay), so
    // switching songs never loads anything mid-show.
    m_engine.preload(m_setlist);
    setFilePath(path);
    m_settings.setValue(kLastFileKey, path);
    rememberRecent(path);
    setDirty(false);
    emit structureChanged();
    setCursor(core::firstPatch(m_setlist), true);
    resetUndo();
    qCInfo(lcUi).noquote() << "Opened setlist" << path;
    return true;
}

bool DocumentController::openUrl(const QUrl& url)
{
    if (!url.isLocalFile()) {
        return report(core::Error{core::ErrorCode::FileNotFound, tr("Only local files can be opened: %1").arg(url.toString())});
    }
    return open(url.toLocalFile());
}

bool DocumentController::save()
{
    if (m_filePath.isEmpty()) {
        return report(core::Error{core::ErrorCode::FileWriteFailed, tr("Choose where to save this setlist first")});
    }
    // Named before the setlist file type of its own: saved under the new name
    // (unless another setlist has that name), and the old file goes.
    const QString old = m_filePath;
    if (!old.endsWith(branding::jsonSetlistSuffix(), Qt::CaseInsensitive)) return saveAs(old);
    const QString renamed = old.chopped(branding::jsonSetlistSuffix().size()) + branding::setlistSuffix();
    if (QFileInfo::exists(renamed)) {
        qCInfo(lcUi).noquote() << "Kept the name" << old << "(" << renamed << "is another setlist )";
        return saveAs(old);
    }
    if (!saveAs(renamed)) return false;
    forgetRecent(old);
    if (!QFile::remove(old)) {
        reportMessage(tr("Saved as %1, but the old file %2 could not be removed").arg(QFileInfo(renamed).fileName(), old),
                      Notifications::Warning);
        qCWarning(lcUi).noquote() << "Could not remove" << old << "after saving it as" << renamed;
        return true;
    }
    qCInfo(lcUi).noquote() << "Moved" << old << "to the setlist file type" << renamed;
    return true;
}

bool DocumentController::saveAs(const QString& path)
{
    GC_ONLY_MAIN_THREAD();
    QString target = path;
    if (!target.endsWith(branding::setlistSuffix(), Qt::CaseInsensitive) && !target.endsWith(u".json"_s, Qt::CaseInsensitive)) {
        target += branding::setlistSuffix();
    }
    // Each plugin's settings (preset, knobs) are saved with it.
    const std::vector<QString> problems = m_engine.storePluginStates(m_setlist); // each logged
    if (auto r = core::saveSetlistFile(m_setlist, target); !r) return report(r.error());
    setFilePath(target);
    rememberRecent(target);
    m_settings.setValue(kLastFileKey, target);
    setDirty(false);
    qCInfo(lcUi).noquote() << "Saved setlist" << target;
    if (!problems.empty()) {
        reportMessage(tr("Saved, but %1").arg(problems.size() == 1 ? problems.front()
                                                                  : tr("%n plugins' settings could not be saved (see the log)", nullptr, static_cast<int>(problems.size()))),
                      Notifications::Warning);
    }
    return true;
}

bool DocumentController::saveAsUrl(const QUrl& url)
{
    if (!url.isLocalFile()) {
        return report(core::Error{core::ErrorCode::FileWriteFailed, tr("Only local files can be saved: %1").arg(url.toString())});
    }
    return saveAs(url.toLocalFile());
}

QString DocumentController::reopenLastSetlistKey()
{
    return u"session/reopenLastSetlist"_s;
}

void DocumentController::restoreLastSession()
{
    if (!m_settings.value(reopenLastSetlistKey(), false).toBool()) return; // the start screen shows
    const QString path = m_settings.value(kLastFileKey).toString();
    if (path.isEmpty()) return;
    (void)open(path); // a failure is reported through lastError and logged
}

void DocumentController::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit lastErrorChanged();
}

void DocumentController::reportMessage(const QString& message, Notifications::Level level)
{
    m_notifications.post(message, level);
    if (level != Notifications::Error) return;
    m_lastError = message;
    emit lastErrorChanged();
}

// ---------------------------------------------------------------- internals

bool DocumentController::report(const core::Error& error)
{
    qCWarning(lcUi).noquote() << error.message;
    reportMessage(error.message, Notifications::Error);
    return false;
}

void DocumentController::setCursor(core::Cursor to, bool force)
{
    GC_ONLY_MAIN_THREAD();
    if (to == m_cursor && !force) return;
    const bool newSong = to.song != m_cursor.song || force;
    m_cursor = to;
    m_committedCursor = to;
    const core::Patch* patch = currentPatch();
    FreezeWatchdog::mark(u"switch to %1 / %2"_s.arg(currentSongName(), patch != nullptr ? patch->name : QString()));
    QElapsedTimer timer;
    timer.start();
    resetSelectedChannel();
    // The engine first: views react to these signals by asking the engine
    // about the new channels (e.g. for plugin editors).
    applyCurrentPatchToEngine();
    if (newSong) applyCurrentSongToEngine();
    const qint64 sound = timer.elapsed();
    if (newSong) emit songChanged();
    emit currentChanged();
    emit channelsChanged();
    emit selectedChannelChanged();
    qCInfo(lcUi).noquote() << "Switched to" << currentSongName() << "/" << (patch != nullptr ? patch->name : QString())
                           << ": sound" << sound << "ms, screen updates" << timer.elapsed() - sound << "ms";
}

void DocumentController::commitStructure(core::Cursor target, const std::optional<core::PatchId>& previous)
{
    setDirty(true);
    emit structureChanged();
    const core::Cursor next = core::clampCursor(m_setlist, target);
    const core::Patch* patch = core::patchAt(m_setlist, next);
    if (patch != nullptr && previous && patch->id == *previous) {
        // Same patch, possibly at a new position: nothing to reload.
        m_cursor = next;
        emit currentChanged();
    } else {
        setCursor(next, true);
    }
}

void DocumentController::commitRename()
{
    setDirty(true);
    emit structureChanged();
    emit currentChanged();
}

void DocumentController::commitChannels(int select)
{
    setDirty(true);
    m_selectedChannel = select;
    applyCurrentPatchToEngine(); // before the signals, as in setCursor
    emit channelsChanged();
    emit selectedChannelChanged();
}

void DocumentController::commitChannelField(int channel, bool reapply)
{
    setDirty(true);
    if (reapply) applyCurrentPatchToEngine(); // before the signal, as in setCursor
    emit channelUpdated(channel);
}

std::optional<core::PatchId> DocumentController::currentPatchId() const
{
    const core::Patch* patch = currentPatch();
    return patch != nullptr ? std::optional<core::PatchId>(patch->id) : std::nullopt;
}

core::Cursor DocumentController::follow(const std::optional<core::PatchId>& id, core::Cursor fallback) const
{
    if (id) {
        if (const auto found = core::findPatch(m_setlist, *id)) return *found;
    }
    return fallback;
}

void DocumentController::resetSelectedChannel()
{
    const core::Patch* patch = currentPatch();
    m_selectedChannel = (patch != nullptr && !patch->channels.empty()) ? 0 : -1;
}

void DocumentController::applyCurrentPatchToEngine()
{
    applySectionsToEngine(); // first: the new patch plays its sections from its first note
    const core::Patch* patch = currentPatch();
    const int song = m_cursor.song;
    const core::SongId songId =
        song >= 0 && static_cast<std::size_t>(song) < m_setlist.songs.size() ? m_setlist.songs.at(static_cast<std::size_t>(song)).id
                                                                            : core::SongId{};
    m_engine.applyPatch(songId, patch != nullptr ? *patch : core::Patch{});
}

bool DocumentController::effectExists(int channel, int effect) const
{
    const core::Patch* patch = currentPatch();
    return patch != nullptr && channel >= 0 && static_cast<std::size_t>(channel) < patch->channels.size() && effect >= 0 &&
           static_cast<std::size_t>(effect) < patch->channels.at(static_cast<std::size_t>(channel)).effects.size();
}

void DocumentController::markPluginSettingsChanged()
{
    if (m_hasSetlist) setDirty(true);
}

void DocumentController::setDirty(bool dirty)
{
    // Every edit ends here: it becomes an undo step. Saving and opening
    // (not dirty) make the current setlist the one later edits start from.
    if (dirty) {
        recordEdit();
    } else {
        m_committed = m_setlist;
        m_committedCursor = m_cursor;
    }
    if (m_dirty == dirty) return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void DocumentController::recordEdit()
{
    const QString key = std::exchange(m_coalesceKey, QString());
    if (m_restoring) return;
    if (m_holdUndo > 0) return; // (one step for the whole, when it ends: OneUndoStep)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 kSameEditMs = 1500; // a fader dragged, a value typed: one step
    const bool sameEdit = !key.isEmpty() && key == m_lastCoalesceKey && now - m_lastEditMs < kSameEditMs && !m_undo.empty();
    m_lastCoalesceKey = key;
    m_lastEditMs = now;
    if (!sameEdit) {
        if (m_setlist == m_committed) return; // nothing in the setlist changed (a plugin's own settings)
        m_undo.push_back(UndoStep{.setlist = m_committed, .cursor = m_committedCursor});
        constexpr std::size_t kMaxUndo = 100;
        if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
        m_redo.clear();
        emit undoChanged();
    }
    m_committed = m_setlist;
    m_committedCursor = m_cursor;
}

void DocumentController::resetUndo()
{
    m_undo.clear();
    m_redo.clear();
    m_committed = m_setlist;
    m_committedCursor = m_cursor;
    m_lastCoalesceKey.clear();
    emit undoChanged();
}

bool DocumentController::undo()
{
    return restore(m_undo, m_redo);
}

bool DocumentController::redo()
{
    return restore(m_redo, m_undo);
}

bool DocumentController::restore(std::vector<UndoStep>& from, std::vector<UndoStep>& to)
{
    GC_ONLY_MAIN_THREAD();
    if (from.empty()) return false;
    to.push_back(UndoStep{.setlist = m_setlist, .cursor = m_cursor});
    UndoStep step = std::move(from.back());
    from.pop_back();
    const QScopedValueRollback restoring(m_restoring, true);
    m_setlist = std::move(step.setlist);
    m_committed = m_setlist;
    emit structureChanged();
    emit chordInversionsChanged();
    setCursor(core::clampCursor(m_setlist, step.cursor), true); // plays it and refreshes every view
    m_committedCursor = m_cursor;
    setDirty(true);
    emit undoChanged();
    return true;
}

void DocumentController::setFilePath(const QString& path)
{
    if (m_filePath == path) return;
    m_filePath = path;
    emit filePathChanged();
}

} // namespace gigchain::ui
