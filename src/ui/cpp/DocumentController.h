#pragma once

#include "openstage/core/Error.h"
#include "openstage/core/Model.h"
#include "openstage/core/Navigation.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <optional>

class QSettings;

namespace openstage::engine {
class IEngine;
}

namespace openstage::ui {

// Owns the open setlist, the current position in it and the file state. Every
// edit from the UI goes through here, and the engine is kept in sync with the
// current patch. Failures set lastError (shown in the UI) and are logged.
//
// The engine and settings are owned by the application and must outlive this.
class DocumentController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(int songIndex READ songIndex NOTIFY currentChanged)
    Q_PROPERTY(int patchIndex READ patchIndex NOTIFY currentChanged)
    Q_PROPERTY(bool hasPatch READ hasPatch NOTIFY currentChanged)
    Q_PROPERTY(QString currentSongName READ currentSongName NOTIFY currentChanged)
    Q_PROPERTY(QString currentPatchName READ currentPatchName NOTIFY currentChanged)
    Q_PROPERTY(int currentPatchNumber READ currentPatchNumber NOTIFY currentChanged)
    Q_PROPERTY(QString nextPatchLabel READ nextPatchLabel NOTIFY currentChanged)
    Q_PROPERTY(int selectedChannel READ selectedChannel WRITE setSelectedChannel NOTIFY selectedChannelChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY filePathChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    DocumentController(engine::IEngine& engine, QSettings& settings, QObject* parent = nullptr);

    // C++ access for the models. Non-owning; valid until the next edit.
    [[nodiscard]] const core::Setlist& setlist() const { return m_setlist; }
    [[nodiscard]] core::Cursor cursor() const { return m_cursor; }
    [[nodiscard]] const core::Patch* currentPatch() const;

    [[nodiscard]] int songIndex() const { return m_cursor.song; }
    [[nodiscard]] int patchIndex() const { return m_cursor.patch; }
    [[nodiscard]] bool hasPatch() const { return currentPatch() != nullptr; }
    [[nodiscard]] QString currentSongName() const;
    [[nodiscard]] QString currentPatchName() const;
    [[nodiscard]] int currentPatchNumber() const { return hasPatch() ? m_cursor.patch + 1 : 0; }
    [[nodiscard]] QString nextPatchLabel() const;
    [[nodiscard]] int selectedChannel() const { return m_selectedChannel; }
    void setSelectedChannel(int index);
    [[nodiscard]] bool isDirty() const { return m_dirty; }
    [[nodiscard]] QString filePath() const { return m_filePath; }
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }

    // Navigation
    Q_INVOKABLE void nextPatch();
    Q_INVOKABLE void previousPatch();
    Q_INVOKABLE void nextSong();
    Q_INVOKABLE void previousSong();
    Q_INVOKABLE bool selectPatch(int song, int patch);

    // Structure
    Q_INVOKABLE bool addSong();
    Q_INVOKABLE bool addPatch(int song);
    Q_INVOKABLE bool renameSong(int song, const QString& name);
    Q_INVOKABLE bool renamePatch(int song, int patch, const QString& name);
    Q_INVOKABLE bool duplicateSong(int song);
    Q_INVOKABLE bool duplicatePatch(int song, int patch);
    Q_INVOKABLE bool removeSong(int song);
    Q_INVOKABLE bool removePatch(int song, int patch);
    Q_INVOKABLE bool moveSong(int from, int to);
    Q_INVOKABLE bool movePatch(int song, int from, int to);

    // Channels of the current patch
    Q_INVOKABLE bool addChannel(const QString& pluginId, const QString& name);
    Q_INVOKABLE bool removeChannel(int channel);
    Q_INVOKABLE bool addEffect(int channel, const QString& pluginId, const QString& name);
    Q_INVOKABLE bool removeEffect(int channel, int effect);
    // A bypassed effect stays in the chain but is not played.
    Q_INVOKABLE bool setEffectBypass(int channel, int effect, bool bypass);
    Q_INVOKABLE bool replaceEffect(int channel, int effect, const QString& pluginId, const QString& name);
    Q_INVOKABLE bool setChannelInstrument(int channel, const QString& pluginId, const QString& name);
    Q_INVOKABLE bool setChannelName(int channel, const QString& name);
    Q_INVOKABLE bool setChannelKeyRange(int channel, int low, int high);
    Q_INVOKABLE bool setChannelTranspose(int channel, int semitones);
    Q_INVOKABLE bool setChannelMidiChannel(int channel, int midiChannel);
    Q_INVOKABLE bool setChannelVolume(int channel, double volumeDb);
    Q_INVOKABLE bool setChannelPan(int channel, double pan);
    Q_INVOKABLE bool setChannelMute(int channel, bool mute);
    Q_INVOKABLE bool setChannelSolo(int channel, bool solo);

    // Files
    Q_INVOKABLE void newSetlist();
    Q_INVOKABLE bool open(const QString& path);
    Q_INVOKABLE bool openUrl(const QUrl& url);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString& path);
    Q_INVOKABLE bool saveAsUrl(const QUrl& url);
    // Reopens the last opened file, if any. A failure is reported, and the
    // empty setlist stays.
    Q_INVOKABLE void restoreLastSession();

    Q_INVOKABLE void clearError();
    // Shows a message from elsewhere (e.g. the engine) the same way as errors.
    Q_INVOKABLE void reportMessage(const QString& message);

signals:
    void currentChanged();
    void structureChanged();          // songs/patches added, removed, renamed or moved
    void channelsChanged();           // the current patch's channel list was replaced
    void channelUpdated(int channel); // one channel's fields changed
    void selectedChannelChanged();
    void dirtyChanged();
    void filePathChanged();
    void lastErrorChanged();

private:
    bool report(const core::Error& error);
    void setCursor(core::Cursor cursor, bool force = false);
    void commitStructure(core::Cursor target, const std::optional<core::PatchId>& previous);
    void commitRename();
    void commitChannels(int select);
    void commitChannelField(int channel, bool reapply);
    [[nodiscard]] std::optional<core::PatchId> currentPatchId() const;
    [[nodiscard]] core::Cursor follow(const std::optional<core::PatchId>& id, core::Cursor fallback) const;
    void resetSelectedChannel();
    void applyCurrentPatchToEngine();
    void setDirty(bool dirty);
    [[nodiscard]] bool effectExists(int channel, int effect) const;
    void setFilePath(const QString& path);

    engine::IEngine& m_engine;
    QSettings& m_settings;
    core::Setlist m_setlist;
    core::Cursor m_cursor;
    int m_selectedChannel = -1;
    bool m_dirty = false;
    QString m_filePath;
    QString m_lastError;
};

} // namespace openstage::ui
