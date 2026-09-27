#include "DocumentController.h"
#include "EffectWindows.h"
#include "EngineStatus.h"
#include "MasterBus.h"
#include "LeakCheck.h"
#include "SpyEngine.h"

#include "gigchain/core/SetlistFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestDocumentController : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<test::SpyEngine> m_engine;
    std::unique_ptr<DocumentController> m_doc;

    QString path(const QString& name) const { return m_dir->filePath(name); }

    // Piano and Strings in the patch; a chart with a Verse (2 chords) and a
    // Chorus (1 chord).
    void addSectionsSong()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Piano"_s));
        QVERIFY(m_doc->addChannel(u"spy/Strings.vst3"_s, u"Strings"_s));
        QVERIFY(m_doc->setSongChart(0, u"{comment: Verse}\n[C]words [G]more\n{comment: Chorus}\n[F]la\n"_s));
    }
    // The names of the channels a section plays.
    [[nodiscard]] QStringList sectionChannels(int section) const
    {
        QStringList names;
        const QVariantMap map = m_doc->currentSections().at(section).toMap();
        for (const QVariant& c : map.value(u"channels"_s).toList()) names << c.toMap().value(u"name"_s).toString();
        return names;
    }
    [[nodiscard]] core::ChannelId channelId(int channel) const
    {
        return m_doc->currentPatch()->channels.at(static_cast<std::size_t>(channel)).id;
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"settings.ini"_s), QSettings::IniFormat);
        m_engine = std::make_unique<test::SpyEngine>();
        m_doc = std::make_unique<DocumentController>(*m_engine, *m_settings);
        // Nothing exists until the user makes it: most tests start from a
        // new setlist with one song.
        m_doc->newSetlist();
        QVERIFY(m_doc->addSong());
    }

    void startsWithNoSetlistAndNothingInIt()
    {
        DocumentController fresh(*m_engine, *m_settings);
        QVERIFY(!fresh.hasSetlist());
        QVERIFY(!fresh.hasPatch());
        QCOMPARE(fresh.setlist().songs.size(), std::size_t{0});

        fresh.newSetlist();
        QVERIFY(fresh.hasSetlist());
        QCOMPARE(fresh.setlist().songs.size(), std::size_t{0}); // empty: no "Song 1"
        QVERIFY(!fresh.isDirty());
    }

    void anInstrumentNeedsASetlistAndStartsTheFirstSong()
    {
        DocumentController fresh(*m_engine, *m_settings);
        QVERIFY(!fresh.addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s)); // start screen: nothing to add to
        QCOMPARE(fresh.lastError(), u"Start or open a setlist first"_s);

        fresh.newSetlist();
        QVERIFY(fresh.addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s)); // not "Patch does not exist"
        QCOMPARE(fresh.setlist().songs.size(), std::size_t{1});
        QCOMPARE(fresh.currentPatch()->channels.size(), std::size_t{1});
    }

    void pastingIntoAnEmptySetlistCreatesTheSong()
    {
        DocumentController fresh(*m_engine, *m_settings);
        fresh.newSetlist();
        QVERIFY(fresh.pasteChart(-1, u"Hallelujah chords by Leonard Cohen\nC        Am\nI heard there was\n"_s));
        QCOMPARE(fresh.setlist().songs.size(), std::size_t{1});
        QCOMPARE(fresh.currentSongName(), u"Hallelujah"_s); // named from the sheet
        QVERIFY(fresh.currentChart().contains(u"[C]I heard"_s));
        QVERIFY(fresh.hasPatch());
    }

    void cleanup()
    {
        m_doc.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    void aNewSongIsCurrentAndReachesTheEngine()
    {
        QVERIFY(m_doc->hasPatch());
        QCOMPARE(m_doc->currentSongName(), u"Song 1"_s);
        QCOMPARE(m_doc->currentPatchName(), u"Patch 1"_s);
        QCOMPARE(m_engine->lastPatch.name, u"Patch 1"_s); // the engine plays it
        QVERIFY(m_doc->isDirty()); // a new song is an unsaved change
        QCOMPARE(m_doc->displayName(), u"Untitled"_s);
    }

    void navigationCrossesSongsAndReachesTheEngine()
    {
        QVERIFY(m_doc->addPatch(0));
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->selectPatch(0, 0));
        QSignalSpy current(m_doc.get(), &DocumentController::currentChanged);
        m_doc->nextPatch();
        QCOMPARE(m_doc->patchIndex(), 1);
        QCOMPARE(m_doc->nextPatchLabel(), u"Song 2 — Patch 1"_s);
        m_doc->nextPatch();
        QCOMPARE(m_doc->songIndex(), 1);
        QCOMPARE(m_doc->nextPatchLabel(), QString());
        QCOMPARE(current.count(), 2);
        QCOMPARE(m_engine->lastPatch.id, m_doc->currentPatch()->id);
        m_doc->previousSong();
        QCOMPARE(m_doc->songIndex(), 0);
        QCOMPARE(m_doc->patchIndex(), 0);
    }

    void editsMarkDirtyAndSaveClearsIt()
    {
        QVERIFY(m_doc->addPatch(0));
        QVERIFY(m_doc->isDirty());
        QVERIFY(m_doc->saveAs(path(u"gig"_s)));
        QVERIFY(!m_doc->isDirty());
        QCOMPARE(m_doc->displayName(), u"gig.gigchain.json"_s);
        QVERIFY(QFile::exists(path(u"gig.gigchain.json"_s)));
    }

    void saveWithoutAFileNameFails()
    {
        QVERIFY(!m_doc->save());
        QVERIFY(!m_doc->lastError().isEmpty());
    }

    void failedSaveKeepsDirty()
    {
        QVERIFY(m_doc->addSong());
        QVERIFY(!m_doc->saveAs(path(u"missing/folder/gig.gigchain.json"_s)));
        QVERIFY(m_doc->isDirty());
        QVERIFY(!m_doc->lastError().isEmpty());
    }

    void failedOpenKeepsUnsavedDocument()
    {
        QVERIFY(m_doc->addSong());
        {
            QFile bad(path(u"bad.gigchain.json"_s));
            QVERIFY(bad.open(QIODevice::WriteOnly));
            bad.write("{ not json");
        }
        QVERIFY(!m_doc->open(path(u"bad.gigchain.json"_s)));
        QVERIFY(!m_doc->lastError().isEmpty());
        QVERIFY(m_doc->isDirty());
        QCOMPARE(m_doc->setlist().songs.size(), std::size_t{2});
    }

    void removingCurrentPatchMovesToNeighbour()
    {
        QVERIFY(m_doc->addPatch(0));
        QVERIFY(m_doc->addPatch(0));
        QVERIFY(m_doc->selectPatch(0, 1));
        const core::PatchId third = m_doc->setlist().songs[0].patches[2].id;
        QVERIFY(m_doc->removePatch(0, 1));
        QCOMPARE(m_doc->patchIndex(), 1);
        QCOMPARE(m_doc->currentPatch()->id, third);
        QCOMPARE(m_engine->lastPatch.id, third);
    }

    void removingCurrentSongMovesToNextSong()
    {
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->selectPatch(1, 0));
        const core::SongId third = m_doc->setlist().songs[2].id;
        QVERIFY(m_doc->removeSong(1));
        QCOMPARE(m_doc->songIndex(), 1);
        QCOMPARE(m_doc->setlist().songs[1].id, third);
        QVERIFY(m_doc->hasPatch());
    }

    void editsElsewhereKeepTheCurrentPatch()
    {
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->selectPatch(1, 0));
        const core::PatchId current = m_doc->currentPatch()->id;
        const int applied = m_engine->applyCount;
        QVERIFY(m_doc->moveSong(1, 0));
        QCOMPARE(m_doc->songIndex(), 0);
        QCOMPARE(m_doc->currentPatch()->id, current);
        QCOMPARE(m_engine->applyCount, applied); // same patch: no reload
    }

    void channelEditsReachTheEngine()
    {
        QSignalSpy channels(m_doc.get(), &DocumentController::channelsChanged);
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QCOMPARE(channels.count(), 1);
        QCOMPARE(m_engine->lastPatch.channels.size(), std::size_t{1});
        QCOMPARE(m_doc->selectedChannel(), 0);
        const QString id = m_doc->currentPatch()->channels[0].id.value();

        QVERIFY(m_doc->setChannelVolume(0, -6.0));
        QCOMPARE(m_engine->volumes[id], -6.0);
        QVERIFY(m_doc->setChannelMute(0, true));
        QCOMPARE(m_engine->mutes[id], true);
        QVERIFY(m_doc->setChannelPan(0, -0.5));
        QCOMPARE(m_engine->pans[id], -0.5);
        QVERIFY(!m_doc->setChannelPan(0, 2.0));
        QCOMPARE(m_doc->currentPatch()->channels[0].pan, -0.5);

        QVERIFY(!m_doc->setChannelKeyRange(0, 80, 20));
        QVERIFY(m_doc->lastError().contains(u"keyLow"_s));
        QCOMPARE(m_doc->currentPatch()->channels[0].keyLow, 0);

        QVERIFY(m_doc->addEffect(0, u"spy/Reverb.vst3"_s, u"Spy Reverb"_s));
        QCOMPARE(m_engine->lastPatch.channels[0].effects.size(), std::size_t{1});
        QVERIFY(m_doc->setEffectBypass(0, 0, true));
        QVERIFY(m_engine->lastPatch.channels[0].effects[0].bypass);
        QVERIFY(!m_doc->setEffectBypass(0, 5, true)); // no such effect: reported, not ignored
        QVERIFY(!m_doc->lastError().isEmpty());
        QVERIFY(m_doc->setEffectBypass(0, 0, false));
        QVERIFY(m_doc->replaceEffect(0, 0, u"spy/Delay.vst3"_s, u"Spy Delay"_s));
        QCOMPARE(m_engine->lastPatch.channels[0].effects[0].pluginId, u"spy/Delay.vst3"_s);
        QVERIFY(!m_doc->replaceEffect(0, 9, u"spy/Delay.vst3"_s, u"Spy Delay"_s));
        QVERIFY(m_doc->setChannelInstrument(0, u"spy/Pad.vst3"_s, u"Spy Pad"_s));
        QCOMPARE(m_engine->lastPatch.channels[0].instrument->pluginId, u"spy/Pad.vst3"_s);
        QVERIFY(m_doc->removeEffect(0, 0));
        QVERIFY(m_doc->removeChannel(0));
        QCOMPARE(m_doc->selectedChannel(), -1);
    }

    void engineHasThePatchBeforeTheUiHearsAboutIt()
    {
        // Views (e.g. the plugin editor) ask the engine about the new channels
        // as soon as they are told; the engine must already be on that patch.
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->addPatch(0));
        QVERIFY(m_doc->addChannel(u"spy/Pad.vst3"_s, u"Spy Pad"_s));
        int checks = 0;
        const auto engineIsCurrent = [this, &checks] {
            ++checks;
            const core::Patch* patch = m_doc->currentPatch();
            QVERIFY(patch != nullptr);
            QCOMPARE(m_engine->lastPatch.id, patch->id);
            QCOMPARE(m_engine->lastPatch.channels.size(), patch->channels.size());
        };
        connect(m_doc.get(), &DocumentController::channelsChanged, this, engineIsCurrent);
        connect(m_doc.get(), &DocumentController::selectedChannelChanged, this, engineIsCurrent);

        m_doc->previousPatch();                                                  // patch change
        QVERIFY(m_doc->addChannel(u"spy/Reverb.vst3"_s, u"Spy Reverb"_s));        // channel added
        QVERIFY(m_doc->removeChannel(0));                                         // channel removed
        QVERIFY(checks >= 4);
    }

    void startsOnTheStartScreenByDefault()
    {
        QVERIFY(m_doc->saveAs(path(u"gig.gigchain.json"_s)));
        QVERIFY(m_doc->open(path(u"gig.gigchain.json"_s)));

        DocumentController second(*m_engine, *m_settings); // next start
        second.restoreLastSession();
        QVERIFY(!second.hasSetlist()); // nothing opens until the user picks it
        QVERIFY(second.lastError().isEmpty());
        QCOMPARE(second.recentFiles().first(), path(u"gig.gigchain.json"_s)); // offered on the start screen
    }

    void restoreLastSessionReopensTheFileWhenChosen()
    {
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->saveAs(path(u"gig.gigchain.json"_s)));
        QVERIFY(m_doc->open(path(u"gig.gigchain.json"_s))); // remembers it
        m_settings->setValue(DocumentController::reopenLastSetlistKey(), true);

        DocumentController second(*m_engine, *m_settings);
        second.restoreLastSession();
        QCOMPARE(second.filePath(), path(u"gig.gigchain.json"_s));
        QCOMPARE(second.setlist().songs.size(), std::size_t{2});
    }

    void restoreWithMissingFileReportsIt()
    {
        m_settings->setValue(u"session/lastFile"_s, path(u"gone.gigchain.json"_s));
        m_settings->setValue(DocumentController::reopenLastSetlistKey(), true);
        DocumentController second(*m_engine, *m_settings);
        second.restoreLastSession();
        QVERIFY(second.lastError().contains(u"gone.gigchain.json"_s));
        QVERIFY(!second.hasSetlist()); // nothing is made up in its place
    }

    void recentSetlistsKeepTheLastFive()
    {
        for (int i = 1; i <= 6; ++i) QVERIFY(m_doc->saveAs(path(u"set%1.gigchain.json"_s.arg(i))));
        QVERIFY(m_doc->open(path(u"set3.gigchain.json"_s))); // opened again: moves to the top
        const QStringList recent = m_doc->recentFiles();
        QCOMPARE(recent.size(), 5);
        QCOMPARE(recent.first(), path(u"set3.gigchain.json"_s));
        QCOMPARE(recent.count(path(u"set3.gigchain.json"_s)), 1); // no duplicates
        QVERIFY(!recent.contains(path(u"set1.gigchain.json"_s)));  // the oldest dropped off

        DocumentController next(*m_engine, *m_settings); // next start
        QCOMPARE(next.recentFiles(), recent);

        QVERIFY(QFile::remove(path(u"set6.gigchain.json"_s)));
        QVERIFY(!next.open(path(u"set6.gigchain.json"_s))); // moved or deleted
        QVERIFY(!next.recentFiles().contains(path(u"set6.gigchain.json"_s)));
        QCOMPARE(next.recentSetlists().size(), 4); // its details went with it
    }

    void recentSetlistsShowTheirSongCountAndWhenOpened()
    {
        const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
        QVERIFY(m_doc->saveAs(path(u"gig.gigchain.json"_s)));
        QVariantList recent = m_doc->recentSetlists();
        QCOMPARE(recent.size(), 1);
        QVariantMap entry = recent.first().toMap();
        QCOMPARE(entry.value(u"path"_s).toString(), path(u"gig.gigchain.json"_s));
        QCOMPARE(entry.value(u"name"_s).toString(), u"gig"_s); // no ".gigchain.json"
        QCOMPARE(entry.value(u"songs"_s).toInt(), 1);
        QVERIFY(entry.value(u"opened"_s).toDateTime() >= before);

        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->save()); // the count follows the saved file
        QCOMPARE(m_doc->recentSetlists().first().toMap().value(u"songs"_s).toInt(), 2);

        DocumentController next(*m_engine, *m_settings); // next start
        QCOMPARE(next.recentSetlists().first().toMap().value(u"songs"_s).toInt(), 2);
    }

    void recentSetlistsFromBeforeDetailsStillShow()
    {
        m_settings->setValue(u"session/recentFiles"_s, QStringList{path(u"old.gigchain.json"_s)});
        DocumentController next(*m_engine, *m_settings);
        const QVariantMap entry = next.recentSetlists().first().toMap();
        QCOMPARE(entry.value(u"name"_s).toString(), u"old"_s);
        QCOMPARE(entry.value(u"songs"_s).toInt(), -1); // unknown until opened
        QVERIFY(!entry.value(u"opened"_s).toDateTime().isValid());
    }

    void savingKeepsEachPluginsSettings()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->saveAs(path(u"gig.gigchain.json"_s)));
        QCOMPARE(m_engine->storeCount, 1); // asked for the plugins' settings when saving
        const auto saved = core::loadSetlistFile(path(u"gig.gigchain.json"_s));
        QVERIFY(saved.has_value());
        QCOMPARE(saved->songs[0].patches[0].channels[0].instrument->state, QByteArray("spy settings: spy/Piano.vst3"));
    }

    void aDuplicatedSongSoundsLikeTheOriginalDoesNow()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->duplicateSong(0));
        const auto& copy = m_doc->setlist().songs.at(1);
        QCOMPARE(copy.patches[0].channels[0].instrument->state, QByteArray("spy settings: spy/Piano.vst3"));
    }

    void clickingAnEffectAsksForItsWindow()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->addEffect(0, u"spy/Reverb.vst3"_s, u"Spy Reverb"_s));
        EffectWindows windows(*m_engine, *m_doc);
        QVERIFY(!windows.open(0, 0, nullptr)); // the spy's effects have no window
        QCOMPARE(m_engine->effectEditorRequests.size(), std::size_t{1});
        QCOMPARE(m_engine->effectEditorRequests[0].first, m_doc->currentPatch()->channels[0].id.value());
        QCOMPARE(m_engine->effectEditorRequests[0].second, 0);
        QVERIFY(m_doc->lastError().contains(u"no window"_s)); // said, not swallowed
        QCOMPARE(windows.openCount(), 0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"effect 5 is not on"_s));
        QVERIFY(!windows.open(0, 5, nullptr)); // no such effect: nothing asked, and said
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"channel 7 is not in the current patch"_s));
        QVERIFY(!windows.open(7, 0, nullptr)); // no such channel
        QCOMPARE(m_engine->effectEditorRequests.size(), std::size_t{1});
    }

    void masterEffectsBelongToTheRigNotTheSetlist()
    {
        EffectWindows windows(*m_engine, *m_doc);
        {
            MasterBus master(*m_engine, *m_doc, *m_settings, windows);
            master.load();
            QVERIFY(master.effects().empty()); // nothing by default
            QVERIFY(master.addEffect(u"spy/Reverb.vst3"_s, u"Spy Reverb"_s));
            QVERIFY(master.addEffect(u"spy/Pad.vst3"_s, u"Spy Limiter"_s));
            QCOMPARE(m_engine->masterEffects.size(), std::size_t{2}); // playing
            QCOMPARE(master.effectNames(), (QStringList{u"Spy Reverb"_s, u"Spy Limiter"_s}));
            QVERIFY(master.setEffectBypass(1, true));
            QVERIFY(m_engine->masterEffects[1].bypass);
            QVERIFY(!master.openEffect(0, nullptr)); // the spy's effects have no window
            QCOMPARE(m_engine->masterEditorRequests, std::vector<int>{0});
            QVERIFY(m_doc->lastError().contains(u"no window"_s));
        }
        // Next start: the same effects, with their settings, whatever setlist opens.
        m_doc->newSetlist();
        MasterBus again(*m_engine, *m_doc, *m_settings, windows);
        again.load();
        QCOMPARE(again.effectNames(), (QStringList{u"Spy Reverb"_s, u"Spy Limiter"_s}));
        QCOMPARE(again.effects()[0].state, QByteArray("spy master settings: spy/Reverb.vst3"));
        QVERIFY(again.effects()[1].bypass);
        QCOMPARE(m_engine->masterEffects.size(), std::size_t{2});

        QVERIFY(again.replaceEffect(0, u"spy/Piano.vst3"_s, u"Spy EQ"_s));
        QVERIFY(again.removeEffect(1));
        QCOMPARE(again.effectNames(), QStringList{u"Spy EQ"_s});
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"master effect 3 does not exist"_s));
        QVERIFY(!again.removeEffect(3));
    }

    void anEditedMasterEffectIsSavedWhenTheAppQuits()
    {
        EffectWindows windows(*m_engine, *m_doc);
        {
            MasterBus master(*m_engine, *m_doc, *m_settings, windows);
            QVERIFY(master.addEffect(u"spy/Reverb.vst3"_s, u"Spy Reverb"_s));
            m_settings->remove(u"master/effects"_s); // pretend only the edit is unsaved
            master.noteEdited();
        } // quitting
        MasterBus again(*m_engine, *m_doc, *m_settings, windows);
        again.load();
        QCOMPARE(again.effectNames(), QStringList{u"Spy Reverb"_s});
    }

    void theLimiterLightStaysOnLongEnoughToSee()
    {
        EngineStatus status(*m_engine, *m_doc);
        QVERIFY(!status.limiting());
        m_engine->limiterActivity = true;
        status.poll();
        QVERIFY(status.limiting());
        for (int i = 0; i < 5; ++i) status.poll();
        QVERIFY(status.limiting()); // still lit ~165 ms later
        for (int i = 0; i < 20; ++i) status.poll();
        QVERIFY(!status.limiting());
    }

    void aPluginEditMarksTheSetlistUnsaved()
    {
        QVERIFY(m_doc->saveAs(path(u"gig.gigchain.json"_s)));
        QVERIFY(!m_doc->isDirty());
        EngineStatus status(*m_engine, *m_doc);
        m_engine->pluginEdited = true; // a knob turned in a plugin window
        status.poll();
        QVERIFY(m_doc->isDirty()); // the title shows it and closing asks to save
        QVERIFY(!m_engine->pluginEdited);
    }

    void pastedChordSheetBecomesTheSongsChart()
    {
        QSignalSpy chart(m_doc.get(), &DocumentController::chartChanged);
        QVERIFY(m_doc->pasteChart(0, u"-------\nC        G\nHello my   friend\n"_s));
        QCOMPARE(m_doc->currentChart(), u"[C]Hello my [G]friend\n"_s); // cleaned, chords over the words
        QVERIFY(chart.count() >= 1);
        QVERIFY(m_doc->isDirty());

        QVERIFY(!m_doc->pasteChart(0, u"   \n"_s)); // nothing to paste
        QVERIFY(m_doc->lastError().contains(u"no text"_s));
        QCOMPARE(m_doc->currentChart(), u"[C]Hello my [G]friend\n"_s); // unchanged

        QVERIFY(m_doc->setSongChart(0, u"[Am]Edited"_s)); // typed in the editor
        QCOMPARE(m_doc->currentChart(), u"[Am]Edited"_s);
        QVERIFY(!m_doc->setSongChart(9, u"x"_s)); // no such song
    }

    void pastingAPageNamesTheSongAndCanBeUndone()
    {
        const QString page = u"Hallelujah Chords by Leonard Cohen\nKey: C\nBPM: 56\nDifficulty: novice\n[Verse 1]\nC        Am\nI heard there was\nLast update: Jan 1\n"_s;
        QCOMPARE(m_doc->currentSongName(), u"Song 1"_s); // still the placeholder name
        QVERIFY(m_doc->pasteChart(0, page));
        QCOMPARE(m_doc->currentSongName(), u"Hallelujah"_s); // named from the sheet
        QCOMPARE(m_doc->setlist().songs[0].key, u"C"_s);
        QCOMPARE(m_doc->setlist().songs[0].tempo, 56.0);
        QVERIFY(!m_doc->currentChart().contains(u"Difficulty"_s)); // clutter gone
        QVERIFY(!m_doc->currentChart().contains(u"Last update"_s));
        QVERIFY(m_doc->canUndoPaste());

        QVERIFY(m_doc->undoPaste()); // exactly what was pasted, and the old name
        QCOMPARE(m_doc->currentChart(), page);
        QCOMPARE(m_doc->currentSongName(), u"Song 1"_s);
        QVERIFY(!m_doc->canUndoPaste());
    }

    void pastingKeepsANameTheUserChose()
    {
        QVERIFY(m_doc->renameSong(0, u"Opener"_s));
        QVERIFY(m_doc->pasteChart(0, u"Hallelujah Chords by Leonard Cohen\nC        Am\nI heard there was\n"_s));
        QCOMPARE(m_doc->currentSongName(), u"Opener"_s);
    }

    void chartFilesImport()
    {
        const QString file = path(u"song.txt"_s);
        {
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write("Am      F\nGoodbye now\n"); // F above "now"
        }
        QVERIFY(m_doc->importChartFile(0, QUrl::fromLocalFile(file)));
        QCOMPARE(m_doc->currentChart(), u"[Am]Goodbye [F]now\n"_s);

        const QString pdf = path(u"song.pdf"_s);
        {
            QFile out(pdf);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write("%PDF-1.7");
        }
        QVERIFY(!m_doc->importChartFile(0, QUrl::fromLocalFile(pdf))); // locked format
        QVERIFY(m_doc->lastError().contains(u"PDF"_s));
        QVERIFY(!m_doc->importChartFile(0, QUrl::fromLocalFile(path(u"missing.txt"_s))));
    }

    void chartIsSavedWithTheSetlist()
    {
        QVERIFY(m_doc->setSongChart(0, u"[G]Saved"_s));
        QVERIFY(m_doc->saveAs(path(u"charts.gigchain.json"_s)));
        DocumentController other(*m_engine, *m_settings);
        QVERIFY(other.open(path(u"charts.gigchain.json"_s)));
        QCOMPARE(other.currentChart(), u"[G]Saved"_s);
    }

    void chartLinesForTheView()
    {
        const QVariantList lines = m_doc->chartLines(u"{comment: Chorus}\n[Dm]I love [C#m7]you"_s);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].toMap().value(u"kind"_s).toString(), u"comment"_s);
        QCOMPARE(lines[0].toMap().value(u"label"_s).toString(), u"Chorus"_s);
        const QVariantList segments = lines[1].toMap().value(u"segments"_s).toList();
        QCOMPARE(segments.size(), 2);
        QCOMPARE(segments[1].toMap().value(u"chord"_s).toString(), u"C#m7"_s);
        QCOMPARE(segments[1].toMap().value(u"text"_s).toString(), u"you"_s);
    }

    void editingCycleDoesNotLeak()
    {
        const auto edits = [this] {
            QVERIFY(m_doc->addSong());
            QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
            QVERIFY(m_doc->removeSong(m_doc->songIndex()));
        };
        // Undo keeps the last 100 steps: fill it first, so what grows after
        // is a leak and not the history (which must stay at its limit).
        for (int i = 0; i < 40; ++i) edits();
        QCOMPARE(test::leakedBlocks(edits), 0LL);
    }

    void everyEditCanBeUndoneAndRedone()
    {
        m_doc->newSetlist();
        QVERIFY(!m_doc->canUndo()); // a new setlist starts with no history
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->renameSong(0, u"Hallelujah"_s));
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->canUndo());
        QVERIFY(m_doc->undo()); // the channel goes
        QVERIFY(m_doc->currentPatch()->channels.empty());
        QCOMPARE(m_engine->lastPatch.channels.size(), std::size_t{0}); // and the engine plays the undone patch
        QVERIFY(m_doc->undo()); // the name goes back
        QCOMPARE(m_doc->currentSongName(), u"Song 1"_s);
        QVERIFY(m_doc->canRedo());
        QVERIFY(m_doc->redo());
        QVERIFY(m_doc->redo());
        QCOMPARE(m_doc->currentSongName(), u"Hallelujah"_s);
        QVERIFY(m_doc->canUndo()); // and back to the empty setlist, three steps down
        QVERIFY(m_doc->undo() && m_doc->undo() && m_doc->undo());
        QVERIFY(m_doc->setlist().songs.empty());
        QVERIFY(!m_doc->canUndo());
        QVERIFY(m_doc->redo() && m_doc->redo() && m_doc->redo());
        QCOMPARE(m_doc->currentPatch()->channels.size(), std::size_t{1});
        QVERIFY(!m_doc->canRedo());
        // A new edit after undoing drops what could be redone.
        QVERIFY(m_doc->undo());
        QVERIFY(m_doc->addSong());
        QVERIFY(!m_doc->canRedo());
    }

    void aFaderDragIsOneUndoStep()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        for (int i = 1; i <= 20; ++i) QVERIFY(m_doc->setChannelVolume(0, -0.5 * i));
        QCOMPARE(m_doc->currentPatch()->channels.at(0).volumeDb, -10.0);
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->currentPatch()->channels.at(0).volumeDb, 0.0); // back to before the drag, not one step
        QVERIFY(m_doc->undo());
        QVERIFY(m_doc->currentPatch()->channels.empty());
    }

    void openingASetlistClearsTheHistory()
    {
        QVERIFY(m_doc->renameSong(0, u"First"_s));
        QVERIFY(m_doc->saveAs(path(u"a.gigchain.json"_s)));
        QVERIFY(m_doc->open(path(u"a.gigchain.json"_s)));
        QVERIFY(!m_doc->canUndo());
        QVERIFY(!m_doc->canRedo());
    }

    void aSongsTempoPlaysWhenTheSongIsChosen()
    {
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->setSongTempo(0, 84.0));
        QCOMPARE(m_engine->tempoNow, 120.0); // song 2 is playing: its tempo is not set
        QVERIFY(m_doc->selectPatch(0, 0));
        QCOMPARE(m_doc->songTempo(), 84.0);
        QCOMPARE(m_engine->tempoNow, 84.0);
        QVERIFY(m_doc->selectPatch(1, 0));
        QCOMPARE(m_doc->songTempo(), 0.0);
        QCOMPARE(m_engine->tempoNow, 84.0); // a song without a tempo keeps the one playing
        QVERIFY(m_doc->setSongTempo(1, 128.0)); // the current song: at once
        QCOMPARE(m_engine->tempoNow, 128.0);
        QVERIFY(!m_doc->setSongTempo(1, 500.0));
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->songTempo(), 0.0);
    }

    void aBackingTrackIsKeptInTheSetlistsFolder()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Save the setlist first"_s));
        QVERIFY(!m_doc->setSongBackingTrack(0, QUrl::fromLocalFile(path(u"x.wav"_s))));

        QVERIFY(QDir().mkpath(path(u"show"_s)));
        QVERIFY(m_doc->saveAs(path(u"show/set.gigchain.json"_s)));
        const QString elsewhere = path(u"music/Hallelujah backing.wav"_s);
        QDir().mkpath(path(u"music"_s));
        {
            QFile file(elsewhere);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("RIFF....WAVE");
        }
        QVERIFY(m_doc->setSongBackingTrack(0, QUrl::fromLocalFile(elsewhere)));
        QCOMPARE(m_doc->songBackingTrack(), u"Hallelujah backing.wav"_s);
        QVERIFY(QFileInfo::exists(path(u"show/Hallelujah backing.wav"_s))); // copied next to the setlist
        QCOMPARE(QFileInfo(m_engine->track.path).absoluteFilePath(), QFileInfo(path(u"show/Hallelujah backing.wav"_s)).absoluteFilePath());
        // Saved and read back with the song.
        QVERIFY(m_doc->save());
        QVERIFY(m_doc->open(path(u"show/set.gigchain.json"_s)));
        QCOMPARE(m_doc->songBackingTrack(), u"Hallelujah backing.wav"_s);
        // Removing it stops it.
        QVERIFY(m_doc->setSongBackingTrack(0, QUrl()));
        QVERIFY(m_engine->track.path.isEmpty());
    }

    void aVelocityLayerAndAnInputChannelReachTheEngine()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->setChannelVelocityRange(0, 1, 70));
        QCOMPARE(m_engine->lastPatch.channels.at(0).velocityHigh, 70);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"velocityLow must not be above velocityHigh"_s));
        QVERIFY(!m_doc->setChannelVelocityRange(0, 90, 20));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Choose which input"_s));
        QVERIFY(!m_doc->addInputChannel(0, 0));
        QVERIFY(m_doc->addInputChannel(1, 2)); // no inputs open: added, with a warning to open them
        const core::Channel& input = m_engine->lastPatch.channels.at(1);
        QVERIFY(!input.instrument.has_value());
        QCOMPARE(input.inputLeft, 1);
        QCOMPARE(input.inputRight, 2);
        QCOMPARE(input.name, u"Input 1+2"_s);
    }

    void knobsAreMappedAndRangedPerChannel()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->addMapping(0, 1, 74, -1, 7, u"Cutoff"_s));
        QVERIFY(m_doc->addMapping(0, 1, 74, -1, 9, u"Drive"_s)); // the same knob learned again: replaces it
        QCOMPARE(m_doc->mappings(0).size(), 1);
        QCOMPARE(m_doc->mappings(0).at(0).toMap().value(u"parameterName"_s).toString(), u"Drive"_s);
        QCOMPARE(m_doc->mappings(0).at(0).toMap().value(u"targetName"_s).toString(), u"Spy Piano"_s);
        QVERIFY(m_doc->setMappingRange(0, 0, 0.25, 0.75));
        QCOMPARE(m_engine->lastPatch.channels.at(0).mappings.at(0).maximum, 0.75);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"must be between 0 and 1"_s));
        QVERIFY(!m_doc->setMappingRange(0, 0, -1.0, 2.0));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"target must be between"_s));
        QVERIFY(!m_doc->addMapping(0, 1, 75, 3, 1, u"On a missing effect"_s));
        QVERIFY(m_doc->removeMapping(0, 0));
        QVERIFY(m_doc->mappings(0).isEmpty());
    }

    void aKnobIsLearnedFromTheKeyboardAndThePlugin()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        EngineStatus status(*m_engine, *m_doc);
        QSignalSpy learned(&status, &EngineStatus::mappingLearned);
        status.startMappingLearn(0, -1);
        QVERIFY(status.learningMapping());
        m_engine->movedController = std::pair{1, 21};
        status.poll();
        QCOMPARE(status.learnedKnob(), u"Knob CC 21 (channel 1)"_s);
        QVERIFY(status.learningMapping()); // the parameter is still to come
        m_engine->touched = engine::PluginParameter{.id = 9, .name = u"Drive"_s};
        status.poll();
        QCOMPARE(learned.count(), 1);
        QVERIFY(!status.learningMapping());
        const auto mapping = m_engine->lastPatch.channels.at(0).mappings.at(0);
        QCOMPARE(mapping.controller, 21);
        QCOMPARE(mapping.parameter, quint32{9});

        // Picked from the list instead of moved in the plugin.
        status.startMappingLearn(0, -1);
        status.setLearnParameter(7, u"Cutoff"_s);
        m_engine->movedController = std::pair{1, 22};
        status.poll();
        QCOMPARE(m_engine->lastPatch.channels.at(0).mappings.size(), std::size_t{2});
    }

    void tapTempoFollowsTheTaps()
    {
        EngineStatus status(*m_engine, *m_doc);
        status.tapTempo();
        QVERIFY(m_engine->tempoRequests.empty()); // one tap is not a tempo
        for (int i = 0; i < 3; ++i) {
            QTest::qSleep(300); // tapping at 200 BPM: the time between taps is what is measured
            status.tapTempo();
        }
        QVERIFY(!m_engine->tempoRequests.empty());
        QVERIFY2(std::abs(m_engine->tempoRequests.back() - 200.0) < 15.0, qPrintable(QString::number(m_engine->tempoRequests.back())));
    }

    // ---- Song sections

    void theChartsSectionsPlayTheFirstInstrument()
    {
        addSectionsSong();
        const QVariantList sections = m_doc->currentSections();
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections.at(0).toMap().value(u"name"_s).toString(), u"Verse"_s);
        QCOMPARE(sections.at(0).toMap().value(u"bars"_s).toInt(), 2);
        QVERIFY(sections.at(0).toMap().value(u"guessed"_s).toBool());
        QCOMPARE(sectionChannels(0), QStringList{u"Piano"_s});
        QCOMPARE(sectionChannels(1), QStringList{u"Piano"_s});
        // To the engine, for this patch.
        QVERIFY(m_engine->sections.patch == m_doc->currentPatch()->id);
        QCOMPARE(m_engine->sections.sections.size(), std::size_t{2});
        QCOMPARE(m_engine->sections.sections.at(0).live, std::vector<core::ChannelId>{channelId(0)});
        QCOMPARE(m_engine->sections.sections.at(1).bars, 1);
        // The chart's title lines know their section.
        const QVariantList lines = m_doc->chartLines(m_doc->currentChart());
        QCOMPARE(lines.at(0).toMap().value(u"sectionIndex"_s).toInt(), 0);
        QCOMPARE(lines.at(1).toMap().value(u"sectionIndex"_s).toInt(), -1);
        QCOMPARE(lines.at(2).toMap().value(u"sectionIndex"_s).toInt(), 1);
    }

    void aSectionIsGivenItsInstrumentsAndUndoTakesThemBack()
    {
        addSectionsSong();
        QSignalSpy changed(m_doc.get(), &DocumentController::sectionsChanged);
        QCOMPARE(m_doc->sectionChoices(1).size(), 1); // Strings (Piano plays there already)
        QVERIFY(m_doc->addSectionChannel(1, 1));
        QCOMPARE(sectionChannels(1), (QStringList{u"Piano"_s, u"Strings"_s}));
        QVERIFY(m_doc->sectionChoices(1).isEmpty());
        QCOMPARE(m_engine->sections.sections.at(1).live, (std::vector<core::ChannelId>{channelId(0), channelId(1)}));
        QVERIFY(!changed.isEmpty());
        QVERIFY(m_doc->isDirty());

        QVERIFY(m_doc->removeSectionChannel(1, 0));
        QCOMPARE(sectionChannels(1), QStringList{u"Strings"_s});
        QVERIFY(m_doc->removeSectionChannel(1, 1));
        QVERIFY(sectionChannels(1).isEmpty()); // a silent chorus
        QVERIFY(m_engine->sections.sections.at(1).live.empty());
        QCOMPARE(sectionChannels(0), QStringList{u"Piano"_s}); // the verse untouched

        QVERIFY(m_doc->undo());
        QCOMPARE(sectionChannels(1), QStringList{u"Strings"_s});
        QCOMPARE(m_engine->sections.sections.at(1).live, std::vector<core::ChannelId>{channelId(1)});

        QVERIFY(m_doc->setSectionBars(0, 8));
        QCOMPARE(m_doc->currentSections().at(0).toMap().value(u"bars"_s).toInt(), 8);
        QVERIFY(!m_doc->currentSections().at(0).toMap().value(u"guessed"_s).toBool());
        QCOMPARE(m_engine->sections.sections.at(0).bars, 8);

        QVERIFY(!m_doc->setSectionBars(0, 0));
        QVERIFY(!m_doc->setSectionBars(0, 1000));
        QVERIFY(!m_doc->addSectionChannel(5, 0));
        QVERIFY2(m_doc->lastError().contains(u"Section 6"_s), qPrintable(m_doc->lastError()));
        QVERIFY(!m_doc->addSectionChannel(0, 9));
    }

    void sectionsReachTheEngineBeforeThePatch()
    {
        addSectionsSong();
        QVERIFY(m_doc->addSong()); // the new song is chosen
        QVERIFY(m_doc->setSongChart(1, u"{comment: Intro}\n[C]\n"_s));
        m_engine->calls.clear();
        m_engine->jumps.clear();
        const int stops = m_engine->stops;
        m_doc->previousSong();
        const auto patchAt = std::ranges::find(m_engine->calls, u"patch"_s);
        QVERIFY(patchAt != m_engine->calls.end());
        QVERIFY(patchAt != m_engine->calls.begin());
        QCOMPARE(*(patchAt - 1), u"sections"_s); // just before it
        QCOMPARE(m_engine->stops, stops + 1);         // another song: the count stops...
        QCOMPARE(m_engine->jumps, std::vector<int>{0}); // ... and starts over at its first section
        // An edit in the same song does not stop it.
        QVERIFY(m_doc->addSectionChannel(0, 1));
        QCOMPARE(m_engine->stops, stops + 1);
    }

    void aSongWithoutSectionsPlaysEverything()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Piano"_s));
        QVERIFY(m_doc->setSongChart(0, u"[C]just words\n"_s));
        QVERIFY(m_doc->currentSections().isEmpty());
        QVERIFY(m_engine->sections.sections.empty());
        QVERIFY(!m_doc->hasSections());
        m_doc->playSong();
        QVERIFY(!m_engine->played);
        QVERIFY(m_doc->notifications()->text(m_doc->notifications()->count() - 1).contains(u"no sections"_s));
    }

    void theSongPlaysStopsAndMovesOn()
    {
        addSectionsSong();
        EngineStatus status(*m_engine, *m_doc);
        m_doc->playSong();
        const auto notPlayed = std::pair{-1, true};
        QCOMPARE(m_engine->played.value_or(notPlayed), (std::pair{0, false})); // the first section; no click: no count-in
        status.poll();
        QVERIFY(status.songPlaying());
        QCOMPARE(status.songSection(), 0);
        QCOMPARE(status.songBar(), 1);
        m_doc->nextSection();
        QCOMPARE(m_engine->jumps.back(), 1);
        m_doc->nextSection(); // already the last: nowhere to go
        QCOMPARE(m_engine->jumps.back(), 1);
        m_engine->pendingActions = {engine::ControlAction::PlayBacking}; // the play/stop pedal stops the song
        status.poll();
        QVERIFY(!status.songPlaying());
        m_engine->click = true;
        m_engine->pendingActions = {engine::ControlAction::PlayBacking};
        status.poll();
        // From the section it was at; with the click on, a bar of it first.
        QCOMPARE(m_engine->played.value_or(notPlayed), (std::pair{1, true}));
        m_engine->pendingActions = {engine::ControlAction::NextSection};
        m_engine->jumps.clear();
        m_engine->position.section = 0;
        status.poll();
        QCOMPARE(m_engine->jumps, std::vector<int>{1});
        m_doc->selectSection(0);
        QCOMPARE(m_engine->jumps.back(), 0);
        m_doc->selectSection(4);
        QVERIFY(m_doc->lastError().contains(u"Section 5"_s));
    }

    void pastingTakesTheTempoAndTimeAndSaysSo()
    {
        const int before = m_doc->notifications()->count();
        QVERIFY(m_doc->pasteChart(0, u"Some Song Chords by Someone\nTempo: 96\nTime: 6/8\n[Verse]\nC G\nla la\n"_s));
        QCOMPARE(m_doc->songTempo(), 96.0);
        QCOMPARE(m_doc->songTimeNumerator(), 6);
        QCOMPARE(m_doc->songTimeDenominator(), 8);
        QCOMPARE(m_engine->timeSignature, (std::pair{6, 8}));
        QCOMPARE(m_engine->tempoNow, 96.0);
        QStringList said;
        for (int i = before; i < m_doc->notifications()->count(); ++i) said << m_doc->notifications()->text(i);
        QVERIFY2(said.join(u'|').contains(u"Tempo 96 BPM taken from the chart"_s), qPrintable(said.join(u'|')));
        QVERIFY2(said.join(u'|').contains(u"Time signature 6/8 taken from the chart"_s), qPrintable(said.join(u'|')));
        // A tempo the song already has stays.
        QVERIFY(m_doc->pasteChart(0, u"Tempo: 140\n[Verse]\nC G\n"_s));
        QCOMPARE(m_doc->songTempo(), 96.0);
        // Undoing the paste puts the time back.
        QVERIFY(m_doc->undoPaste());
        QCOMPARE(m_doc->songTimeNumerator(), 6); // the first paste's
        QVERIFY(m_doc->setSongTimeSignature(0, 3, 4));
        QCOMPARE(m_engine->timeSignature, (std::pair{3, 4}));
        QVERIFY(!m_doc->setSongTimeSignature(0, 3, 5));
        QVERIFY(m_doc->setSongSwitchEarly(0, true));
        QVERIFY(m_doc->songSwitchEarly());
    }

    void pedalsTapTheTempoAndStartTheBackingTrack()
    {
        EngineStatus status(*m_engine, *m_doc);
        m_engine->track = engine::BackingTrackState{.path = u"x.wav"_s, .loading = false, .loaded = true, .playing = false,
                                                    .position = 0.0, .length = 60.0};
        m_engine->pendingActions = {engine::ControlAction::PlayBacking};
        status.poll();
        QVERIFY(m_engine->track.playing);
        QVERIFY(status.trackPlaying());
        m_engine->pendingActions = {engine::ControlAction::PlayBacking};
        status.poll();
        QVERIFY(!m_engine->track.playing);
    }
};

QTEST_GUILESS_MAIN(TestDocumentController)
#include "tst_document_controller.moc"
