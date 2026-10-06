#pragma once

#include "Notifications.h"

#include "gigchain/core/Error.h"
#include "gigchain/core/Model.h"
#include "gigchain/core/Navigation.h"
#include "gigchain/core/SongMap.h"
#include "gigchain/engine/EngineTypes.h"

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

class QSettings;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

// Owns the open setlist, the current position in it and the file state. Every
// edit from the UI goes through here, and the engine is kept in sync with the
// current patch. Messages for the user are posted to notifications, each
// with its level; errors also set lastError. All are logged.
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
    // The current sound's play mode: 0 every instrument together (layers),
    // 1 only the selected channel (one at a time). Sections set up override it.
    Q_PROPERTY(int playMode READ playMode NOTIFY playModeChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY filePathChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(gigchain::ui::Notifications* notifications READ notifications CONSTANT)
    // False until a setlist is created or opened: nothing exists by default.
    Q_PROPERTY(bool hasSetlist READ hasSetlist NOTIFY hasSetlistChanged)
    // The last setlists opened or saved, newest first (at most 5).
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    // The same, for the start screen: [{path, name, songs, opened}]; songs is
    // -1 and opened invalid for a file not opened or saved since they were kept.
    Q_PROPERTY(QVariantList recentSetlists READ recentSetlists NOTIFY recentFilesChanged)
    // The current song's chart (ChordPro).
    Q_PROPERTY(QString currentChart READ currentChart NOTIFY chartChanged)
    // The last paste can be undone: exactly what was pasted, and the old name.
    // Undo and redo of every edit to the setlist (Ctrl+Z, Ctrl+Shift+Z).
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    // The current song's tempo (0 = not set) and backing track file name ("" = none).
    Q_PROPERTY(double songTempo READ songTempo NOTIFY songChanged)
    Q_PROPERTY(QString songBackingTrack READ songBackingTrack NOTIFY songChanged)
    // The current song's time signature and whether its sections switch a beat early.
    Q_PROPERTY(int songTimeNumerator READ songTimeNumerator NOTIFY songChanged)
    Q_PROPERTY(int songTimeDenominator READ songTimeDenominator NOTIFY songChanged)
    Q_PROPERTY(bool songSwitchEarly READ songSwitchEarly NOTIFY songChanged)
    Q_PROPERTY(bool songFollowChords READ songFollowChords NOTIFY songChanged)
    Q_PROPERTY(bool following READ following NOTIFY sectionsChanged)
    // Play has something to count: sections, or a song on its timeline without
    // section titles (one part, a bar per chord).
    Q_PROPERTY(bool canPlaySong READ hasSections NOTIFY sectionsChanged)
    Q_PROPERTY(QString followFirstChord READ followFirstChord NOTIFY sectionsChanged)
    // The current song's loops start and stop on the bars (or press to press).
    Q_PROPERTY(bool songLoopSync READ songLoopSync NOTIFY songChanged)
    // Its synced loops' length in bars (0 = open: closed where stopped).
    Q_PROPERTY(int songLoopBars READ songLoopBars NOTIFY songChanged)
    // The current song's sections (from its chart) with what each plays in
    // the current patch: [{index, name, label, bars, guessed, assigned,
    // channels: [{channel, name}], choices: [{channel, name}] (the patch's
    // other instruments)}]. Empty: the song has none (all plays).
    Q_PROPERTY(QVariantList currentSections READ currentSections NOTIFY sectionsChanged)

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
    [[nodiscard]] int playMode() const;
    // An undo step; the engine plays it at once.
    Q_INVOKABLE bool setPlayMode(int mode);
    // Why channel `index` of the current sound would not sound if played now
    // ("" = it plays): muted, another soloed, not in the section in force,
    // another selected (one at a time), no instrument loaded.
    Q_INVOKABLE QString silentReason(int index) const;
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
    // A keyboard's patch button (Program Change, 0-based): that patch of the
    // current song, as MainStage does; one the song does not have is said.
    bool selectProgram(int program);

    // Structure
    Q_INVOKABLE bool addSong();
    Q_INVOKABLE bool addPatch(int song);
    Q_INVOKABLE bool renameSong(int song, const QString& name);
    Q_INVOKABLE bool renamePatch(int song, int patch, const QString& name);

    // Song charts. setSongChart takes ChordPro as typed in the editor;
    // pasteChart takes anything with chords and lyrics (a chord site, a text
    // file's contents) and cleans it (core::tidyChordSheet) first.
    [[nodiscard]] QString currentChart() const;
    [[nodiscard]] bool hasSetlist() const { return m_hasSetlist; }
    [[nodiscard]] QStringList recentFiles() const;
    [[nodiscard]] QVariantList recentSetlists() const;
    Q_INVOKABLE bool setSongChart(int song, const QString& chordPro);
    // Pasting a chord-site page: the site's clutter is removed, a song still
    // named "Song N" takes the sheet's title, and an unset key/tempo is filled.
    // All of it is one undo step.
    Q_INVOKABLE bool pasteChart(int song, const QString& pasted);
    // pasteChart with whatever text is on the clipboard.
    Q_INVOKABLE bool pasteChartFromClipboard(int song);
    // A downloaded text chart: .txt, .cho, .chopro, .chordpro, .crd, .pro, .onsong.
    Q_INVOKABLE bool importChartFile(int song, const QUrl& file);
    // For the chart view: [{kind: "lyrics"|"section"|"comment"|"blank",
    // label, segments: [{chord, text}]}]; title/key lines are left out.
    Q_INVOKABLE QVariantList chartLines(const QString& chordPro) const;
    // The current song's chart edited in place (the Chart tab): `line` is a
    // chartLines() line's "line", `at` a character of its words, `chord` the
    // line's chord number. Each is an undo step (typing a line is one); a
    // change that cannot be made is said and logged, the chart untouched.
    Q_INVOKABLE bool placeChordAt(int line, int at, const QString& chord);
    Q_INVOKABLE bool renameChordAt(int line, int chord, const QString& name); // empty: removed
    Q_INVOKABLE bool moveChordTo(int line, int chord, int toLine, int toAt);
    Q_INVOKABLE bool setLineLyrics(int line, const QString& lyrics);
    Q_INVOKABLE bool splitChartLine(int line, int at);
    Q_PROPERTY(QVariantMap currentChordInversions READ currentChordInversions NOTIFY chordInversionsChanged)
    // How to play a chord (its diagram): {name, understood, inversion (the one
    // shown: `inversion`, else the song's choice, else 0), chosen (-1: none),
    // inversions: [names], left/right: [MIDI notes], leftNames/rightNames}.
    Q_INVOKABLE QVariantMap chordDiagram(const QString& name, int inversion) const;
    // The current song's inversion for a chord (-1: none); an undo step.
    Q_INVOKABLE bool setChordInversion(const QString& name, int inversion);
    // The current song's chosen inversions: {chord name: inversion}.
    [[nodiscard]] QVariantMap currentChordInversions() const;

    // The song's flow (the order it is played in, which chord follow keeps
    // to): [{name, occurrence, label}], the chart's own order when none is set.
    Q_PROPERTY(QVariantList songFlow READ songFlow NOTIFY sectionsChanged)
    // Whether the flow is the song's own (false: the chart's order).
    Q_PROPERTY(bool songFlowSet READ songFlowSet NOTIFY sectionsChanged)
    [[nodiscard]] QVariantList songFlow() const;
    // The chart's sections, in its order, as parts a flow can have: [{name, occurrence, label}].
    Q_PROPERTY(QVariantList chartParts READ chartParts NOTIFY sectionsChanged)
    [[nodiscard]] QVariantList chartParts() const;
    [[nodiscard]] bool songFlowSet() const;
    [[nodiscard]] std::vector<core::SectionRef> currentSongFlow() const;
    // A new flow ([{name, occurrence}]; empty: back to the chart's order): an
    // undo step. A part naming no section of the chart is refused, said.
    Q_INVOKABLE bool setSongFlow(const QVariantList& flow);
    // Enter while typing a line: its words become `lyrics` and it splits at
    // `at` (of the new words), as one undo step.
    Q_INVOKABLE bool editLineAndSplit(int line, const QString& lyrics, int at);
    Q_INVOKABLE bool joinChartLine(int line);
    Q_INVOKABLE bool renameChartSection(int line, const QString& label);
    Q_INVOKABLE bool addChartSection(const QString& label);
    // The chords the current song's chart uses, each once, in order (to drag
    // onto the words again).
    Q_PROPERTY(QStringList currentChartChords READ currentChartChords NOTIFY chartChanged)
    [[nodiscard]] QStringList currentChartChords() const;
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
    // The note-on velocities (1-127) the channel plays: a velocity layer.
    Q_INVOKABLE bool setChannelVelocityRange(int channel, int low, int high);
    // A channel playing an audio input (1-based; right 0 = mono) through its
    // effects; addInputChannel makes a new one.
    Q_INVOKABLE bool addInputChannel(int inputLeft, int inputRight);
    Q_INVOKABLE bool setChannelInput(int channel, int inputLeft, int inputRight);
    // Keyboard knobs mapped to plugin parameters (target -1 = the
    // instrument, else the effect's position). The range is the parameter's
    // value (0-1) at the knob's lowest and highest position.
    Q_INVOKABLE bool addMapping(int channel, int midiChannel, int controller, int target, quint32 parameter,
                                const QString& parameterName);
    Q_INVOKABLE bool removeMapping(int channel, int mapping);
    Q_INVOKABLE bool setMappingRange(int channel, int mapping, double minimum, double maximum);
    // For the knob editor: [{midiChannel, controller, target, targetName, parameter, parameterName, minimum, maximum}].
    Q_INVOKABLE QVariantList mappings(int channel) const;
    // Opens a channel's editor ("zone": keys, velocity, transpose, MIDI
    // channel; "knobs": knob mappings) wherever the window shows it.
    Q_INVOKABLE void editChannel(int channel, const QString& page);

    // Song tempo (BPM, 0 = not set): it plays whenever the song is chosen.
    [[nodiscard]] double songTempo() const;
    Q_INVOKABLE bool setSongTempo(int song, double bpm);
    // The song's backing track: `file` is copied into the setlist's folder
    // (the setlist must be saved first); an empty url removes it.
    [[nodiscard]] QString songBackingTrack() const;
    Q_INVOKABLE bool setSongBackingTrack(int song, const QUrl& file);

    [[nodiscard]] int songTimeNumerator() const;
    [[nodiscard]] int songTimeDenominator() const;
    [[nodiscard]] bool songSwitchEarly() const;
    Q_INVOKABLE bool setSongTimeSignature(int song, int numerator, int denominator);
    Q_INVOKABLE bool setSongSwitchEarly(int song, bool early);
    [[nodiscard]] bool songFollowChords() const;
    Q_INVOKABLE bool setSongFollowChords(int song, bool on);
    // Chord follow: whether this song follows its chords now, its first chord,
    // "Chorus · chord 2 of 8" for a step, and the chartLines() index of its line.
    [[nodiscard]] bool following() const;
    // ---- The song's timeline (it runs at its tempo along its flow)
    // Whether the current song moves by its timeline (not by the chords played).
    [[nodiscard]] bool onTimeline() const;
    // Timeline part `part`'s place in songFlow() (-1: none).
    Q_INVOKABLE int timelinePlace(int part) const;
    // The chord lit `quarter` quarter notes into timeline part `part` (a
    // step of the chart's chords, as followLine() takes); -1: none yet.
    Q_INVOKABLE int timelineStep(int part, double quarter) const;
    // The live controls: on the next bar line, or at the part's end.
    Q_INVOKABLE void nextPart();
    Q_INVOKABLE void repeatPart();
    Q_INVOKABLE void holdPart();
    Q_INVOKABLE void stopAtEndOfPart();
    // Play from the song's first part.
    Q_INVOKABLE void playSongFromTop();
    [[nodiscard]] QString followFirstChord() const;
    Q_INVOKABLE QString followLabel(int step) const;
    Q_INVOKABLE int followLine(int step) const;
    // The part of the song's flow (songFlow) a followed chord is in; -1: none.
    Q_INVOKABLE int followPart(int step) const;
    [[nodiscard]] bool songLoopSync() const;
    Q_INVOKABLE bool setSongLoopSync(int song, bool sync);
    [[nodiscard]] int songLoopBars() const;
    Q_INVOKABLE bool setSongLoopBars(int song, int bars);
    // The looper's keyboard controls, kept with the setlist (an undoable edit).
    [[nodiscard]] const core::LoopControls& loopControls() const { return m_setlist.loopControls; }
    bool setLoopControls(const core::LoopControls& controls);

    // Song sections: what each section of the current song's chart plays in
    // the current patch, and how long it is.
    [[nodiscard]] QVariantList currentSections() const;
    // The patch's instruments a section does not play yet: [{channel, name}].
    Q_INVOKABLE QVariantList sectionChoices(int section) const;
    Q_INVOKABLE bool addSectionChannel(int section, int channel);
    Q_INVOKABLE bool removeSectionChannel(int section, int channel);
    Q_INVOKABLE bool setSectionBars(int section, int bars);
    // The song's count: Play from the section in force (a bar of click
    // first when the click is on), Stop, and moving between sections
    // (stopped: where Play starts; playing: at once).
    Q_INVOKABLE void playSong();
    Q_INVOKABLE void stopSong();
    Q_INVOKABLE void selectSection(int section);
    // Part `place` of songFlow() (a tile in Perform): its section, and chord
    // follow from that very time the section comes round.
    Q_INVOKABLE void selectFlowPart(int place);
    Q_INVOKABLE void nextSection();
    // (A song on its timeline without section titles plays as one section.)
    [[nodiscard]] bool hasSections() const { return m_sectionCount > 0; }

    [[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.empty(); }
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

    // Files
    Q_INVOKABLE void newSetlist();
    Q_INVOKABLE bool open(const QString& path);
    Q_INVOKABLE bool openUrl(const QUrl& url);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString& path);
    Q_INVOKABLE bool saveAsUrl(const QUrl& url);
    // On start: reopens the last opened file, only when the user chose that
    // in Settings (off by default: the start screen shows). A failure is
    // reported, and no setlist is made up in its place.
    Q_INVOKABLE void restoreLastSession();
    // The Settings choice restoreLastSession() follows (bool, default false).
    [[nodiscard]] static QString reopenLastSetlistKey();

    // A plugin's own settings changed (in its window): unsaved changes.
    void markPluginSettingsChanged();

    Q_INVOKABLE void clearError();
    // Shows a message from elsewhere (the engine, a window) at its level; an
    // error also becomes lastError. The caller has logged it.
    Q_INVOKABLE void reportMessage(const QString& message,
                                   gigchain::ui::Notifications::Level level = Notifications::Error);
    [[nodiscard]] Notifications* notifications() { return &m_notifications; }

signals:
    void currentChanged();
    void structureChanged();          // songs/patches added, removed, renamed or moved
    void channelsChanged();           // the current patch's channel list was replaced
    void channelUpdated(int channel); // one channel's fields changed
    void channelAdded(int channel);   // an instrument was added and loaded (after channelsChanged)
    void selectedChannelChanged();
    void playModeChanged();
    void dirtyChanged();
    void filePathChanged();
    void lastErrorChanged();
    void chartChanged(); // the current song's chart, or which song is current
    void hasSetlistChanged();
    void recentFilesChanged();
    void undoChanged();
    void songChanged(); // the current song, or its tempo, time, backing track
    void sectionsChanged(); // the current song's sections or what they play
    void chordInversionsChanged(); // the current song's chosen inversions (or which song is current)
    void loopControlsChanged();
    void channelEditRequested(int channel, const QString& page);

private:
    bool report(const core::Error& error);
    // A chart edit's result into the current song (or its reason reported).
    bool applyChartEdit(const core::Result<QString>& edited, const QString& coalesceKey);
    void setCursor(core::Cursor to, bool force = false);
    void commitStructure(core::Cursor target, const std::optional<core::PatchId>& previous);
    void commitRename();
    void commitChannels(int select);
    void commitChannelField(int channel, bool reapply);
    [[nodiscard]] std::optional<core::PatchId> currentPatchId() const;
    [[nodiscard]] core::Cursor follow(const std::optional<core::PatchId>& id, core::Cursor fallback) const;
    void resetSelectedChannel();
    void applyCurrentPatchToEngine();
    void setHasSetlist(bool has);
    void rememberRecent(const QString& path);
    void forgetRecent(const QString& path);
    void setDirty(bool dirty);
    [[nodiscard]] bool effectExists(int channel, int effect) const;
    void setFilePath(const QString& path);
    // The current song's tempo, time signature and backing track, to the engine.
    void applyCurrentSongToEngine();
    // The current song's sections for the current patch, to the engine
    // (before the patch itself, so the new patch plays them from its first
    // note). A different song than last time stops the count and starts
    // again at its first section.
    void applySectionsToEngine();
    // What each section (and a song without sections) plays, for the engine.
    [[nodiscard]] engine::SongSections songSections() const;
    [[nodiscard]] std::vector<int> timelinePlaces() const;
    // A song on its timeline with chords but no section titles: its bars as
    // one part (a bar per chord); 0 otherwise.
    [[nodiscard]] int unsectionedBars() const;
    // The selected channel's id (one-at-a-time sounds play it).
    [[nodiscard]] std::optional<core::ChannelId> selectedChannelId() const;
    // The channels a section of the current song plays in the current
    // patch; nothing when there is no such section.
    [[nodiscard]] std::optional<std::vector<core::ChannelId>> sectionLive(int section) const;
    // The setlist's looper controls, to the engine.
    void applyLoopControlsToEngine();
    // Stores one section's setup (an undoable edit) and plays it.
    bool storeSection(int section, const std::function<void(core::SectionSetup&)>& edit);
    core::SongId m_sectionsSong; // the song whose sections the engine has
    int m_sectionCount = 0;
    core::SongMap m_songMap; // the current song's chords, when it follows them
    bool m_followTooLong = false; // its chart has too many chords (or sections) to follow (said once)
    std::vector<int> m_followLines;            // per step: its chartLines() index
    std::vector<int> m_timelinePlaces;         // per timeline part: its place in songFlow()
    // Per timeline part: its chords, (quarter notes into the part, step).
    std::vector<std::vector<std::pair<double, int>>> m_timelineSteps;
    std::vector<QString> m_followSectionNames; // per chart section: its name
    [[nodiscard]] const core::Song* currentSong() const;
    // Undo: the setlist as it was before each edit. Called whenever an edit
    // is committed; edits in a row with the same `m_coalesceKey` within a
    // moment (a fader being dragged) are one step.
    struct UndoStep
    {
        core::Setlist setlist;
        core::Cursor cursor;
    };
    void recordEdit();
    void resetUndo();
    // Goes back to the last step of `from`, keeping where it was in `to`.
    bool restore(std::vector<UndoStep>& from, std::vector<UndoStep>& to);

    engine::IEngine& m_engine;
    QSettings& m_settings;
    core::Setlist m_setlist;
    bool m_hasSetlist = false;
    // Above 0: edits are not recorded one by one; the whole (a paste: the
    // chart, the song's name, key, tempo, time) becomes one undo step when
    // it ends (OneUndoStep).
    int m_holdUndo = 0;
    class OneUndoStep;
    // A flow as QML reads it: [{name, occurrence, section, label}].
    [[nodiscard]] static QVariantList partsList(const std::vector<core::SectionRef>& flow,
                                                const std::vector<core::ChartSection>& sections);
    core::Cursor m_cursor;
    int m_selectedChannel = -1;
    bool m_dirty = false;
    // Undo history: each step is the setlist and position before an edit.
    std::vector<UndoStep> m_undo;
    std::vector<UndoStep> m_redo;
    core::Setlist m_committed; // the setlist after the last recorded edit
    core::Cursor m_committedCursor;
    QString m_coalesceKey;     // set by an edit that merges with the same edit just before
    QString m_lastCoalesceKey;
    qint64 m_lastEditMs = 0;
    bool m_restoring = false;
    QString m_filePath;
    QString m_lastError;
    Notifications m_notifications;
};

} // namespace gigchain::ui
