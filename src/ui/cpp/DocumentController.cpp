#include "DocumentController.h"

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
constexpr auto kSuffix = ".openstage.json"_L1;

core::Setlist defaultSetlist()
{
    core::Setlist setlist;
    setlist.songs.push_back(core::makeSong(u"Song 1"_s));
    return setlist;
}

} // namespace

DocumentController::DocumentController(engine::IEngine& engine, QSettings& settings, QObject* parent)
    : QObject(parent), m_engine(engine), m_settings(settings), m_setlist(defaultSetlist()),
      m_cursor(core::firstPatch(m_setlist))
{
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

bool DocumentController::renamePatch(int song, int patch, const QString& name)
{
    if (auto r = core::renamePatch(m_setlist, core::Cursor{song, patch}, name); !r) return report(r.error());
    commitRename();
    return true;
}

bool DocumentController::duplicateSong(int song)
{
    const auto current = currentPatchId();
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
    m_setlist = defaultSetlist();
    setFilePath({});
    setDirty(false);
    emit structureChanged();
    setCursor(core::firstPatch(m_setlist), true);
}

bool DocumentController::open(const QString& path)
{
    auto loaded = core::loadSetlistFile(path);
    if (!loaded) return report(loaded.error());
    m_setlist = std::move(*loaded);
    setFilePath(path);
    m_settings.setValue(kLastFileKey, path);
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
    QString target = path;
    if (!target.endsWith(u".json"_s, Qt::CaseInsensitive)) target += kSuffix;
    if (auto r = core::saveSetlistFile(m_setlist, target); !r) return report(r.error());
    setFilePath(target);
    m_settings.setValue(kLastFileKey, target);
    setDirty(false);
    qCInfo(lcUi).noquote() << "Saved setlist" << target;
    return true;
}

bool DocumentController::saveAsUrl(const QUrl& url)
{
    if (!url.isLocalFile()) {
        return report(core::Error{core::ErrorCode::FileWriteFailed, tr("Only local files can be saved: %1").arg(url.toString())});
    }
    return saveAs(url.toLocalFile());
}

void DocumentController::restoreLastSession()
{
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
    if (cursor == m_cursor && !force) return;
    m_cursor = cursor;
    resetSelectedChannel();
    // The engine first: views react to these signals by asking the engine
    // about the new channels (e.g. for plugin editors).
    applyCurrentPatchToEngine();
    emit currentChanged();
    emit channelsChanged();
    emit selectedChannelChanged();
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
    m_engine.applyPatch(patch != nullptr ? *patch : core::Patch{});
}

bool DocumentController::effectExists(int channel, int effect) const
{
    const core::Patch* patch = currentPatch();
    return patch != nullptr && channel >= 0 && static_cast<std::size_t>(channel) < patch->channels.size() && effect >= 0 &&
           static_cast<std::size_t>(effect) < patch->channels[static_cast<std::size_t>(channel)].effects.size();
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
