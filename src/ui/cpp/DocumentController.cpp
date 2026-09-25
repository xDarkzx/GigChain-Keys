#include "DocumentController.h"
#include "FreezeWatchdog.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Chart.h"
#include "gigchain/core/Checks.h"

#include <QStringDecoder>

#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>

#include "gigchain/core/Editing.h"
#include "gigchain/core/SetlistFile.h"
#include "gigchain/engine/IEngine.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>

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
    connect(this, &DocumentController::currentChanged, this, &DocumentController::clearPasteUndo);
    resetSelectedChannel();
    applyCurrentPatchToEngine();
}

const core::Patch* DocumentController::currentPatch() const
{
    return core::patchAt(m_setlist, m_cursor);
}

QString DocumentController::currentSongName() const
{
    return hasPatch() ? m_setlist.songs[static_cast<std::size_t>(m_cursor.song)].name : QString();
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
    return u"%1 — %2"_s.arg(m_setlist.songs[static_cast<std::size_t>(next.song)].name, patch->name);
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
    const auto count = validSong ? m_setlist.songs[static_cast<std::size_t>(song)].patches.size() : 0;
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
               ? m_setlist.songs[static_cast<std::size_t>(song)].chart
               : QString();
}

bool DocumentController::setSongChart(int song, const QString& chordPro)
{
    if (auto r = core::setSongChart(m_setlist, song, chordPro); !r) return report(r.error());
    setDirty(true);
    emit chartChanged();
    return true;
}

bool DocumentController::pasteChart(int song, const QString& pasted)
{
    if (pasted.trimmed().isEmpty()) {
        return report(core::Error{core::ErrorCode::InvalidData, tr("There is no text to paste")});
    }
    const core::ImportedSheet sheet = core::importChordSheet(pasted);
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
    const core::Song& before = m_setlist.songs[static_cast<std::size_t>(song)];
    PasteUndo undo{before.id, before.name, before.key, before.tempo, pasted};

    if (!setSongChart(song, sheet.chart)) return false; // reported
    // A placeholder name ("Song 3") takes the sheet's title; a name the user
    // chose stays.
    static const QRegularExpression kPlaceholder(uR"(^Song \d+$)"_s);
    if (!sheet.title.isEmpty() && kPlaceholder.match(before.name).hasMatch()) {
        if (auto r = core::renameSong(m_setlist, song, sheet.title); !r) return report(r.error());
        commitRename();
    }
    const core::Song& now = m_setlist.songs[static_cast<std::size_t>(song)];
    const QString key = now.key.isEmpty() ? sheet.key : now.key;
    const double tempo = now.tempo > 0.0 ? now.tempo : sheet.tempo;
    if (key != now.key || tempo != now.tempo) {
        if (auto r = core::setSongKeyAndTempo(m_setlist, song, key, tempo); !r) return report(r.error());
    }
    m_pasteUndo = undo;
    emit pasteUndoChanged();
    return true;
}

bool DocumentController::undoPaste()
{
    if (!m_pasteUndo) return false;
    const PasteUndo undo = *m_pasteUndo;
    clearPasteUndo();
    const auto it = std::find_if(m_setlist.songs.begin(), m_setlist.songs.end(),
                                 [&](const core::Song& s) { return s.id == undo.song; });
    if (it == m_setlist.songs.end()) {
        return report(core::Error{core::ErrorCode::OutOfRange, tr("The pasted song no longer exists")});
    }
    const int song = static_cast<int>(it - m_setlist.songs.begin());
    if (auto r = core::renameSong(m_setlist, song, undo.name); !r) return report(r.error());
    if (auto r = core::setSongKeyAndTempo(m_setlist, song, undo.key, undo.tempo); !r) return report(r.error());
    commitRename();
    return setSongChart(song, undo.pasted);
}

void DocumentController::clearPasteUndo()
{
    if (!m_pasteUndo) return;
    m_pasteUndo.reset();
    emit pasteUndoChanged();
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
    constexpr qint64 kMaxChartFile = 1024 * 1024; // a chart is a few KB
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
    for (const core::ChartLine& line : core::parseChordPro(chordPro).lines) {
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
        for (const core::ChartSegment& segment : line.segments) {
            segments << QVariantMap{{u"chord"_s, segment.chord}, {u"text"_s, segment.text}};
        }
        lines << QVariantMap{{u"kind"_s, kind}, {u"label"_s, line.label}, {u"segments"_s, segments}};
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
        one.songs = {m_setlist.songs[static_cast<std::size_t>(song)]};
        const std::vector<QString> problems = m_engine.storePluginStates(one); // each logged
        if (!problems.empty()) reportMessage(problems.back());
        m_setlist.songs[static_cast<std::size_t>(song)] = std::move(one.songs.front());
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
    const auto index = core::addChannel(m_setlist, m_cursor, core::PluginSlot{pluginId, name, false});
    if (!index) return report(index.error());
    commitChannels(*index);
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
    if (auto r = core::addEffect(m_setlist, m_cursor, channel, core::PluginSlot{pluginId, name, false}); !r) {
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
        c.effects[static_cast<std::size_t>(effect)].bypass = bypass;
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
        c.effects[static_cast<std::size_t>(effect)] = core::PluginSlot{pluginId, name, false};
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
        c.instrument = core::PluginSlot{pluginId, name, false};
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
    m_engine.setChannelVolume(currentPatch()->channels[static_cast<std::size_t>(channel)].id, volumeDb);
    commitChannelField(channel, false);
    return true;
}

bool DocumentController::setChannelPan(int channel, double pan)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [pan](core::Channel& c) { c.pan = pan; }); !r) {
        return report(r.error());
    }
    m_engine.setChannelPan(currentPatch()->channels[static_cast<std::size_t>(channel)].id, pan);
    commitChannelField(channel, false);
    return true;
}

bool DocumentController::setChannelMute(int channel, bool mute)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [mute](core::Channel& c) { c.mute = mute; }); !r) {
        return report(r.error());
    }
    m_engine.setChannelMute(currentPatch()->channels[static_cast<std::size_t>(channel)].id, mute);
    commitChannelField(channel, false);
    return true;
}

bool DocumentController::setChannelSolo(int channel, bool solo)
{
    if (auto r = core::updateChannel(m_setlist, m_cursor, channel, [solo](core::Channel& c) { c.solo = solo; }); !r) {
        return report(r.error());
    }
    m_engine.setChannelSolo(currentPatch()->channels[static_cast<std::size_t>(channel)].id, solo);
    commitChannelField(channel, false);
    return true;
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
    return saveAs(m_filePath);
}

bool DocumentController::saveAs(const QString& path)
{
    GC_ONLY_MAIN_THREAD();
    QString target = path;
    if (!target.endsWith(u".json"_s, Qt::CaseInsensitive)) target += branding::setlistSuffix();
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
                                                                  : tr("%n plugins' settings could not be saved (see the log)", nullptr, static_cast<int>(problems.size()))));
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

void DocumentController::reportMessage(const QString& message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

// ---------------------------------------------------------------- internals

bool DocumentController::report(const core::Error& error)
{
    qCWarning(lcUi).noquote() << error.message;
    m_lastError = error.message;
    emit lastErrorChanged();
    return false;
}

void DocumentController::setCursor(core::Cursor cursor, bool force)
{
    GC_ONLY_MAIN_THREAD();
    if (cursor == m_cursor && !force) return;
    m_cursor = cursor;
    const core::Patch* patch = currentPatch();
    FreezeWatchdog::mark(u"switch to %1 / %2"_s.arg(currentSongName(), patch != nullptr ? patch->name : QString()));
    QElapsedTimer timer;
    timer.start();
    resetSelectedChannel();
    // The engine first: views react to these signals by asking the engine
    // about the new channels (e.g. for plugin editors).
    applyCurrentPatchToEngine();
    const qint64 sound = timer.elapsed();
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
    const core::Patch* patch = currentPatch();
    const int song = m_cursor.song;
    const core::SongId songId =
        song >= 0 && static_cast<std::size_t>(song) < m_setlist.songs.size() ? m_setlist.songs[static_cast<std::size_t>(song)].id
                                                                            : core::SongId{};
    m_engine.applyPatch(songId, patch != nullptr ? *patch : core::Patch{});
}

bool DocumentController::effectExists(int channel, int effect) const
{
    const core::Patch* patch = currentPatch();
    return patch != nullptr && channel >= 0 && static_cast<std::size_t>(channel) < patch->channels.size() && effect >= 0 &&
           static_cast<std::size_t>(effect) < patch->channels[static_cast<std::size_t>(channel)].effects.size();
}

void DocumentController::markPluginSettingsChanged()
{
    if (m_hasSetlist) setDirty(true);
}

void DocumentController::setDirty(bool dirty)
{
    if (m_dirty == dirty) return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void DocumentController::setFilePath(const QString& path)
{
    if (m_filePath == path) return;
    m_filePath = path;
    emit filePathChanged();
}

} // namespace gigchain::ui
