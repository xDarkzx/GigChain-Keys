#include "DocumentController.h"
#include "EffectWindows.h"
#include "EngineStatus.h"
#include "LoopController.h"
#include "MasterBus.h"
#include "LeakCheck.h"
#include "SpyEngine.h"

#include "gigchain/core/Limits.h"
#include "gigchain/core/SetlistFile.h"

#include <QDir>
#include <QElapsedTimer>
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

    // The first instrument dragged in, with nothing open yet (the start
    // screen), starts a setlist with Song 1 and plays it: no "open a
    // setlist first" in the way.
    void theFirstInstrumentStartsASetlistAndItsFirstSong()
    {
        DocumentController fresh(*m_engine, *m_settings);
        QVERIFY(!fresh.hasSetlist());
        QVERIFY(fresh.addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(fresh.hasSetlist());
        QCOMPARE(fresh.setlist().songs.size(), std::size_t{1});
        QCOMPARE(fresh.currentSongName(), u"Song 1"_s);
        QVERIFY(fresh.currentPatch() != nullptr);
        QCOMPARE(fresh.currentPatch()->channels.size(), std::size_t{1});
        QCOMPARE(m_engine->lastPatch.channels.size(), std::size_t{1}); // playing
        QVERIFY(fresh.lastError().isEmpty());

        // The same for an input channel (a microphone, a guitar).
        DocumentController voice(*m_engine, *m_settings);
        QVERIFY(voice.addInputChannel(1, 2));
        QVERIFY(voice.hasSetlist());
        QCOMPARE(voice.setlist().songs.size(), std::size_t{1});
        QCOMPARE(voice.currentPatch()->channels.size(), std::size_t{1});

        // A setlist open but empty: its first song too.
        DocumentController empty(*m_engine, *m_settings);
        empty.newSetlist();
        QVERIFY(empty.addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s)); // not "Patch does not exist"
        QCOMPARE(empty.setlist().songs.size(), std::size_t{1});
        QCOMPARE(empty.currentPatch()->channels.size(), std::size_t{1});
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
        QCOMPARE(m_doc->displayName(), u"gig.gigchain"_s);
        QVERIFY(QFile::exists(path(u"gig.gigchain"_s)));
    }

    // A setlist from before the .gigchain file type: saving it moves it to
    // the new name (the old file goes, the recent list follows).
    void anOlderSetlistIsSavedAsTheNewFileType()
    {
        QVERIFY(m_doc->saveAs(path(u"old.gigchain.json"_s)));
        QVERIFY(m_doc->open(path(u"old.gigchain.json"_s)));
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->save());
        QCOMPARE(m_doc->filePath(), path(u"old.gigchain"_s));
        QVERIFY(QFile::exists(path(u"old.gigchain"_s)));
        QVERIFY(!QFile::exists(path(u"old.gigchain.json"_s)));
        QCOMPARE(m_doc->recentFiles().first(), path(u"old.gigchain"_s));
        QVERIFY(!m_doc->recentFiles().contains(path(u"old.gigchain.json"_s)));
        QCOMPARE(m_doc->recentSetlists().first().toMap().value(u"name"_s).toString(), u"old"_s);
        QVERIFY(m_doc->open(path(u"old.gigchain"_s)));
        QCOMPARE(m_doc->setlist().songs.size(), std::size_t{2});
    }

    // ... but never over another setlist that already has the new name.
    void savingAnOlderSetlistNeverOverwritesAnother()
    {
        QVERIFY(m_doc->saveAs(path(u"gig.gigchain"_s))); // one song
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->saveAs(path(u"gig.gigchain.json"_s))); // two songs
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->save());
        QCOMPARE(m_doc->filePath(), path(u"gig.gigchain.json"_s));
        QVERIFY(m_doc->open(path(u"gig.gigchain"_s)));
        QCOMPARE(m_doc->setlist().songs.size(), std::size_t{1}); // untouched
        QVERIFY(m_doc->open(path(u"gig.gigchain.json"_s)));
        QCOMPARE(m_doc->setlist().songs.size(), std::size_t{3});
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
        const auto& instrument = m_engine->lastPatch.channels.at(0).instrument;
        if (!instrument) QFAIL("the channel lost its instrument");
        QCOMPARE(instrument->pluginId, u"spy/Pad.vst3"_s);
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
        if (!saved) QFAIL("the saved setlist did not load");
        const auto& instrument = saved->songs.at(0).patches.at(0).channels.at(0).instrument;
        if (!instrument) QFAIL("the saved channel has no instrument");
        QCOMPARE(instrument->state, QByteArray("spy settings: spy/Piano.vst3"));
    }

    void aDuplicatedSongSoundsLikeTheOriginalDoesNow()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->duplicateSong(0));
        const auto& copy = m_doc->setlist().songs.at(1);
        const auto& instrument = copy.patches.at(0).channels.at(0).instrument;
        if (!instrument) QFAIL("the copy has no instrument");
        QCOMPARE(instrument->state, QByteArray("spy settings: spy/Piano.vst3"));
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

        // A whole book pasted by mistake is refused before it is read
        // (reading it would freeze the app), with the reason.
        const QString huge = u"C        G\nHello my friend\n"_s.repeated(core::limits::kMaxChartSourceLength / 26 + 1);
        QVERIFY(huge.size() > core::limits::kMaxChartSourceLength);
        QVERIFY(!m_doc->pasteChart(0, huge));
        QVERIFY(m_doc->lastError().contains(u"too long"_s));
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

        // One Ctrl+Z takes the whole paste back: the chart, the name, the key
        // and the tempo, as they were before it (not one of them at a time).
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->currentChart(), QString());
        QCOMPARE(m_doc->currentSongName(), u"Song 1"_s);
        QCOMPARE(m_doc->setlist().songs.at(0).key, QString());
        QCOMPARE(m_doc->setlist().songs.at(0).tempo, 0.0);
        QVERIFY(m_doc->redo()); // and back again, all of it
        QCOMPARE(m_doc->currentSongName(), u"Hallelujah"_s);
        QCOMPARE(m_doc->setlist().songs.at(0).tempo, 56.0);
        QVERIFY(m_doc->currentChart().contains(u"[C]I heard [Am]there was"_s));
    }

    // The chart editor's cells: each word of a line with the chords on it
    // (a word split where a chord sits inside it; a chord after the last word
    // in a cell of its own), so a chord is drawn over its word by layout.
    void aLineIsGivenAsWordCells()
    {
        const QVariantList lines = m_doc->chartLines(u"[G]I found a [D]love for me\nm[E]o[F]n\n[C]la [G]\n"_s);
        QCOMPARE(lines.size(), 3);
        const auto cells = [&lines](int line) { return lines.at(line).toMap().value(u"cells"_s).toList(); };
        const auto cell = [&cells](int line, int i) { return cells(line).at(i).toMap(); };
        const auto names = [&cell](int line, int i) {
            QStringList list;
            for (const QVariant& chord : cell(line, i).value(u"chords"_s).toList()) list << chord.toMap().value(u"name"_s).toString();
            return list.join(u' ');
        };
        // "I found a love for me": a cell a word, G on "I", D on "love".
        QCOMPARE(cells(0).size(), 6);
        QCOMPARE(cell(0, 0).value(u"text"_s).toString(), u"I"_s);
        QCOMPARE(names(0, 0), u"G"_s);
        QCOMPARE(cell(0, 0).value(u"space"_s).toBool(), true); // a space after it
        QCOMPARE(cell(0, 3).value(u"text"_s).toString(), u"love"_s);
        QCOMPARE(cell(0, 3).value(u"at"_s).toInt(), 10);
        QCOMPARE(names(0, 3), u"D"_s);
        QCOMPARE(cell(0, 3).value(u"chords"_s).toList().front().toMap().value(u"index"_s).toInt(), 1); // the line's chord 1
        QCOMPARE(names(0, 4), QString());
        QCOMPARE(cell(0, 5).value(u"space"_s).toBool(), false); // the last word
        // "mon" with E and F inside it: split at each chord, no space between.
        QCOMPARE(cells(1).size(), 3);
        QCOMPARE(cell(1, 0).value(u"text"_s).toString(), u"m"_s);
        QCOMPARE(cell(1, 1).value(u"text"_s).toString(), u"o"_s);
        QCOMPARE(names(1, 1), u"E"_s);
        QCOMPARE(cell(1, 1).value(u"space"_s).toBool(), false);
        QCOMPARE(names(1, 2), u"F"_s);
        // A chord after the last word: a cell with no words.
        QCOMPARE(cells(2).size(), 2);
        QCOMPARE(cell(2, 1).value(u"text"_s).toString(), QString());
        QCOMPARE(names(2, 1), u"G"_s);
        QCOMPARE(cell(2, 1).value(u"at"_s).toInt(), 3);
    }

    // Enter while typing a line: the words typed and the new line after the
    // caret, one undo step (chords stay on their words).
    void enterSavesTheLineAndSplitsItInOneStep()
    {
        QVERIFY(m_doc->setSongChart(0, u"[G]I found a love"_s));
        QVERIFY(m_doc->setSongChart(0, u"[G]I found a love"_s)); // (a recorded starting point)
        QVERIFY(m_doc->editLineAndSplit(0, u"I found a love for me"_s, 15));
        QCOMPARE(m_doc->currentChart(), u"[G]I found a love \nfor me"_s);
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->currentChart(), u"[G]I found a love"_s);
        QVERIFY(!m_doc->editLineAndSplit(7, u"x"_s, 0)); // no such line: said, nothing changed
        QCOMPARE(m_doc->currentChart(), u"[G]I found a love"_s);
    }

    // A chord's diagram: which keys each hand presses, by name, in each
    // inversion; the one chosen for the song is kept (an undo step), and
    // the diagram opens on it.
    void aChordsDiagramShowsTheKeysAndKeepsAChoice()
    {
        QVariantMap d = m_doc->chordDiagram(u"E/D#"_s, -1);
        QVERIFY(d.value(u"understood"_s).toBool());
        QCOMPARE(d.value(u"inversion"_s).toInt(), 0); // nothing chosen: root position
        QCOMPARE(d.value(u"chosen"_s).toInt(), -1);
        QCOMPARE(d.value(u"left"_s).toList(), (QVariantList{39}));          // D#2
        QCOMPARE(d.value(u"right"_s).toList(), (QVariantList{64, 68, 71})); // E4 G#4 B4
        QCOMPARE(d.value(u"leftNames"_s).toString(), u"D#"_s);
        QCOMPARE(d.value(u"rightNames"_s).toString(), u"E G# B"_s);
        QCOMPARE(d.value(u"inversions"_s).toStringList(), (QStringList{u"Root position"_s, u"1st inversion"_s, u"2nd inversion"_s}));
        d = m_doc->chordDiagram(u"E/D#"_s, 1);
        QCOMPARE(d.value(u"right"_s).toList(), (QVariantList{68, 71, 76})); // G# B E
        QCOMPARE(d.value(u"rightNames"_s).toString(), u"G# B E"_s);
        // Flat chords read in flats.
        QCOMPARE(m_doc->chordDiagram(u"Bb"_s, 0).value(u"rightNames"_s).toString(), u"Bb D F"_s);
        // Not a chord: said so, no keys.
        QVERIFY(!m_doc->chordDiagram(u"N.C."_s, 0).value(u"understood"_s).toBool());

        QVERIFY(m_doc->setChordInversion(u"E/D#"_s, 1));
        QCOMPARE(m_doc->chordDiagram(u"E/D#"_s, -1).value(u"inversion"_s).toInt(), 1); // opens on the choice
        QCOMPARE(m_doc->chordDiagram(u"E/D#"_s, -1).value(u"chosen"_s).toInt(), 1);
        QCOMPARE(m_doc->setlist().songs.at(0).chordInversions.at(u"E/D#"_s), 1);
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->chordDiagram(u"E/D#"_s, -1).value(u"chosen"_s).toInt(), -1);
        QVERIFY(!m_doc->setChordInversion(u"E/D#"_s, 3)); // a triad has no 3rd inversion
        QVERIFY(m_doc->lastError().contains(u"inversion"_s));
    }

    // The song's flow: the chart's order until one is set; a flow of its own
    // (a verse after a verse, the chorus twice) goes to chord follow, which
    // keeps to it; an undo step; a section the chart does not have is refused.
    void aSongsFlowIsSetAndFollowed()
    {
        QVERIFY(m_doc->setSongFollowChords(0, true));
        QVERIFY(m_doc->setSongChart(0, u"{comment: Verse 1}\n[Am]a [F]b\n{comment: Chorus}\n[C]c [G]d\n{comment: Verse 2}\n[Dm]e [E]f\n"_s));
        const auto labels = [this] {
            QStringList list;
            for (const QVariant& part : m_doc->songFlow()) list << part.toMap().value(u"label"_s).toString();
            return list;
        };
        QCOMPARE(labels(), (QStringList{u"Verse 1"_s, u"Chorus"_s, u"Verse 2"_s}));
        QVERIFY(!m_doc->songFlowSet());
        QCOMPARE(m_engine->follow.partStarts, (std::vector<int>{0, 2, 4}));

        const auto part = [](const QString& name) { return QVariantMap{{u"name"_s, name}, {u"occurrence"_s, 1}}; };
        QVERIFY(m_doc->setSongFlow({part(u"Verse 1"_s), part(u"Verse 2"_s), part(u"Chorus"_s), part(u"Chorus"_s)}));
        QVERIFY(m_doc->songFlowSet());
        QCOMPARE(labels(), (QStringList{u"Verse 1"_s, u"Verse 2"_s, u"Chorus"_s, u"Chorus"_s}));
        QCOMPARE(m_engine->follow.partStarts, (std::vector<int>{0, 2, 4, 6})); // the chorus twice
        QCOMPARE(m_engine->follow.steps.at(2).section, 2);                      // Verse 2 straight after Verse 1

        QVERIFY(!m_doc->setSongFlow({part(u"Bridge"_s)}));
        QVERIFY2(m_doc->lastError().contains(u"Bridge"_s), qPrintable(m_doc->lastError()));
        QCOMPARE(labels().size(), 4); // unchanged

        QVERIFY(m_doc->undo());
        QVERIFY(!m_doc->songFlowSet());
        QCOMPARE(m_engine->follow.partStarts, (std::vector<int>{0, 2, 4}));
        QVERIFY(m_doc->redo());
        // Which part of the flow a followed chord is in (Perform lights its tile).
        QCOMPARE(m_doc->followPart(0), 0);
        QCOMPARE(m_doc->followPart(5), 2); // the first chorus
        QCOMPARE(m_doc->followPart(7), 3); // the second
        QCOMPARE(m_doc->followPart(99), -1);
        // A tile tapped: that very part (the second chorus), not the next chorus along.
        m_doc->selectFlowPart(3);
        QCOMPARE(m_engine->jumps.back(), 1);
        QCOMPARE(m_engine->jumpParts.back(), 3);
        m_doc->selectFlowPart(9);
        QVERIFY(m_doc->lastError().contains(u"part"_s));
        // A section renamed in the chart: the flow keeps to it.
        QVERIFY(m_doc->renameChartSection(2, u"Refrain"_s));
        QCOMPARE(labels(), (QStringList{u"Verse 1"_s, u"Verse 2"_s, u"Refrain"_s, u"Refrain"_s}));
        QCOMPARE(m_engine->follow.partStarts, (std::vector<int>{0, 2, 4, 6}));
        QVERIFY(m_doc->undo());
        QCOMPARE(labels(), (QStringList{u"Verse 1"_s, u"Verse 2"_s, u"Chorus"_s, u"Chorus"_s}));
        QVERIFY(m_doc->setSongFlow({})); // back to the chart's order
        QVERIFY(!m_doc->songFlowSet());

        // A spoken intro (no chords) is a tile but nothing to follow: the
        // tiles after it still light for their own chords.
        QVERIFY(m_doc->setSongChart(0, u"[G]in\n{comment: Intro}\nwords\n{comment: Verse 1}\n[Am]a [F]b\n{comment: Chorus}\n[C]c [G]d\n"_s));
        QCOMPARE(labels(), (QStringList{u"Intro"_s, u"Verse 1"_s, u"Chorus"_s}));
        QCOMPARE(m_doc->followPart(0), -1); // before the first section
        QCOMPARE(m_doc->followPart(1), 1);  // Verse 1
        QCOMPARE(m_doc->followPart(3), 2);  // Chorus
        m_doc->selectFlowPart(0); // the intro: its section, no part to follow
        QCOMPARE(m_engine->jumps.back(), 0);
        QCOMPARE(m_engine->jumpParts.back(), -1);
        m_doc->selectFlowPart(2);
        QCOMPARE(m_engine->jumps.back(), 2);
        QCOMPARE(m_engine->jumpParts.back(), 2); // the map's parts: before the intro, Verse 1, Chorus
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
        QVERIFY(m_doc->saveAs(path(u"show/set.gigchain"_s)));
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
        QVERIFY(m_doc->open(path(u"show/set.gigchain"_s)));
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
        QElapsedTimer tapped; // the taps as they really came (a busy machine sleeps longer than asked)
        tapped.start();
        status.tapTempo();
        QVERIFY(m_engine->tempoRequests.empty()); // one tap is not a tempo
        for (int i = 0; i < 3; ++i) {
            QTest::qSleep(300); // tapping at about 200 BPM: the time between taps is what is measured
            status.tapTempo();
        }
        const double played = 60000.0 / (static_cast<double>(tapped.elapsed()) / 3.0); // three beats
        QVERIFY(!m_engine->tempoRequests.empty());
        QVERIFY2(std::abs(m_engine->tempoRequests.back() - played) < 5.0,
                 qPrintable(u"%1 BPM for taps at %2 BPM"_s.arg(m_engine->tempoRequests.back()).arg(played)));
        QVERIFY(played > 100.0 && played <= 201.0); // (and the taps were fast enough to be one tempo)
    }

    // ---- Song sections

    // Layers by default: every instrument plays in every section not set up.
    void theChartsSectionsPlayEveryInstrument()
    {
        addSectionsSong();
        const QVariantList sections = m_doc->currentSections();
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections.at(0).toMap().value(u"name"_s).toString(), u"Verse"_s);
        QCOMPARE(sections.at(0).toMap().value(u"bars"_s).toInt(), 2);
        QVERIFY(sections.at(0).toMap().value(u"guessed"_s).toBool());
        QCOMPARE(sectionChannels(0), (QStringList{u"Piano"_s, u"Strings"_s}));
        QCOMPARE(sectionChannels(1), (QStringList{u"Piano"_s, u"Strings"_s}));
        // To the engine, for this patch.
        QVERIFY(m_engine->sections.patch == m_doc->currentPatch()->id);
        QCOMPARE(m_engine->sections.sections.size(), std::size_t{2});
        QCOMPARE(m_engine->sections.sections.at(0).live, (std::vector<core::ChannelId>{channelId(0), channelId(1)}));
        QVERIFY(!m_engine->sections.unsectioned); // (no section: everything)
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
        QVERIFY(m_doc->sectionChoices(1).isEmpty()); // both play there already
        // The verse: piano alone (the strings come in for the chorus).
        QVERIFY(m_doc->removeSectionChannel(0, 1));
        QCOMPARE(sectionChannels(0), QStringList{u"Piano"_s});
        QCOMPARE(m_doc->sectionChoices(0).size(), 1); // Strings, to put back
        QCOMPARE(m_engine->sections.sections.at(0).live, std::vector<core::ChannelId>{channelId(0)});
        QCOMPARE(sectionChannels(1), (QStringList{u"Piano"_s, u"Strings"_s}));
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

    // One at a time: only the selected instrument plays (sections not set up,
    // and a song without sections); selecting another switches at once;
    // a section set up keeps its own; an undo step; each strip says why it
    // is silent.
    void aSoundPlaysOneInstrumentAtATime()
    {
        addSectionsSong();
        QSignalSpy mode(m_doc.get(), &DocumentController::playModeChanged);
        QVERIFY(m_doc->removeSectionChannel(1, 0)); // the chorus: strings only (set up)
        m_doc->setSelectedChannel(0);
        QVERIFY(m_doc->setPlayMode(1));
        QCOMPARE(m_doc->playMode(), 1);
        QCOMPARE(mode.count(), 1);
        QCOMPARE(m_engine->sections.sections.at(0).live, std::vector<core::ChannelId>{channelId(0)}); // the piano
        QCOMPARE(m_engine->sections.sections.at(1).live, std::vector<core::ChannelId>{channelId(1)}); // as set
        QCOMPARE(m_engine->sections.unsectioned, std::optional(std::vector<core::ChannelId>{channelId(0)}));
        m_doc->setSelectedChannel(1); // a strip clicked
        QCOMPARE(m_engine->sections.sections.at(0).live, std::vector<core::ChannelId>{channelId(1)});
        QCOMPARE(m_engine->sections.unsectioned, std::optional(std::vector<core::ChannelId>{channelId(1)}));

        // Why a strip is silent now (the verse in force).
        m_engine->jumpToSection(0);
        QVERIFY(m_doc->silentReason(1).isEmpty());
        QVERIFY2(m_doc->silentReason(0).contains(u"One at a time"_s), qPrintable(m_doc->silentReason(0)));
        QVERIFY(m_doc->setPlayMode(0));
        QVERIFY(m_doc->silentReason(0).isEmpty());
        QVERIFY(m_doc->setChannelMute(0, true));
        QCOMPARE(m_doc->silentReason(0), u"Muted"_s);
        QVERIFY(m_doc->setChannelMute(0, false));
        QVERIFY(m_doc->setChannelSolo(1, true));
        QVERIFY(m_doc->silentReason(0).contains(u"soloed"_s));
        QVERIFY(m_doc->setChannelSolo(1, false));
        m_engine->jumpToSection(1);
        QVERIFY2(m_doc->silentReason(0).contains(u"Not in Chorus"_s), qPrintable(m_doc->silentReason(0)));

        // Undo: one at a time again, then the layers.
        QVERIFY(m_doc->undo()); // the solo off
        QVERIFY(m_doc->undo()); // the solo
        QVERIFY(m_doc->undo()); // the mute off
        QVERIFY(m_doc->undo()); // the mute
        QVERIFY(m_doc->undo()); // all together
        QCOMPARE(m_doc->playMode(), 1);
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->playMode(), 0);
        QVERIFY(!m_engine->sections.unsectioned);
        QVERIFY(!m_doc->setPlayMode(7));
        QVERIFY(m_doc->lastError().contains(u"7"_s));
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

    // Nothing to play (no chords, no sections): Play says so and does nothing.
    void aSongWithNothingToPlaySaysSo()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Piano"_s));
        QVERIFY(m_doc->setSongChart(0, u"just words\n"_s));
        QVERIFY(m_doc->currentSections().isEmpty());
        QVERIFY(m_engine->sections.sections.empty()); // everything plays
        QVERIFY(!m_doc->hasSections());
        m_doc->playSong();
        QVERIFY(!m_engine->played);
        QVERIFY(m_doc->notifications()->text(m_doc->notifications()->count() - 1).contains(u"nothing to play"_s));
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
        // The next-part pedal, playing on the timeline: on the next bar line.
        m_engine->pendingActions = {engine::ControlAction::NextSection};
        m_engine->liveControls.clear();
        status.poll();
        QCOMPARE(m_engine->liveControls, std::vector<QString>{u"next"_s});
        m_engine->pendingActions = {engine::ControlAction::RepeatPart, engine::ControlAction::HoldPart};
        status.poll();
        QCOMPARE(m_engine->liveControls, (std::vector<QString>{u"next"_s, u"repeat"_s, u"hold"_s}));
        // Stopped: the pedal moves on now.
        m_engine->stopSong();
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

    // The song's timeline: its flow's parts to the engine; the chord lit by
    // time (the same chord twice: one, then the other); the live controls;
    // the keyboard's transport buttons.
    void aSongPlaysOnItsTimeline()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Piano"_s));
        QVERIFY(m_doc->setSongChart(0, u"{comment: Verse}\n[D]la [D]la [G]la [A]la\n{comment: Chorus}\n[G]oh [A]oh\n"_s));
        QVERIFY(!m_doc->songFollowChords()); // new songs: the timeline
        QVERIFY(!m_doc->following());
        QVERIFY(m_engine->follow.steps.empty()); // nothing followed by ear
        const auto part = [](const QString& name) { return QVariantMap{{u"name"_s, name}, {u"occurrence"_s, 1}}; };
        QVERIFY(m_doc->setSongFlow({part(u"Verse"_s), part(u"Chorus"_s), part(u"Chorus"_s)}));
        QCOMPARE(m_engine->sections.parts, (std::vector<int>{0, 1, 1}));
        QCOMPARE(m_doc->timelinePlace(2), 2);
        QCOMPARE(m_doc->timelinePlace(3), -1);
        // The verse's 4 chords over its guessed bars: D, D (each its own step), G, A.
        const int bars = m_engine->sections.sections.at(0).bars;
        const double each = bars * 4.0 / 4;
        QCOMPARE(m_doc->timelineStep(0, 0.0), 0);
        QCOMPARE(m_doc->timelineStep(0, each), 1); // the second D
        QCOMPARE(m_doc->followLine(1), m_doc->followLine(0)); // (on the same line, in its own place)
        // Each D lit (and, the one before it playing, outlined as next) in its own place.
        const QVariantList segments = m_doc->chartLines(m_doc->currentChart()).at(1).toMap().value(u"segments"_s).toList();
        QCOMPARE(segments.at(0).toMap().value(u"steps"_s).toList(), (QVariantList{0}));
        QCOMPARE(segments.at(1).toMap().value(u"steps"_s).toList(), (QVariantList{1}));
        QCOMPARE(m_doc->timelineStep(0, 3 * each + 0.1), 3);
        QCOMPARE(m_doc->timelineStep(2, 0.0), 6); // the second chorus's G
        QCOMPARE(m_doc->timelineStep(5, 0.0), -1);

        // The live controls while it plays.
        EngineStatus status(*m_engine, *m_doc);
        m_doc->playSong();
        m_doc->nextPart();
        m_doc->repeatPart();
        m_doc->holdPart();
        m_doc->stopAtEndOfPart();
        m_doc->selectFlowPart(2); // a tile: that very part, on the next bar line
        QCOMPARE(m_engine->liveControls,
                 (std::vector<QString>{u"next"_s, u"repeat"_s, u"hold"_s, u"stop"_s, u"part 2"_s}));
        status.poll();
        QVERIFY(status.chordStarted()); // the chart lights the chord the time has come to
        QCOMPARE(status.chordStep(), 0);

        // The keyboard's transport buttons.
        m_engine->transportRequests = engine::transport::kStop;
        status.poll();
        QVERIFY(!status.songPlaying());
        m_engine->transportRequests = engine::transport::kStart;
        status.poll();
        QCOMPARE(m_engine->played.value_or(std::pair{9, true}), (std::pair{-1, false})); // from the top
        QVERIFY(status.songPlaying());
        m_engine->transportRequests = engine::transport::kToggle; // the sustain pedal twice
        status.poll();
        QVERIFY(!status.songPlaying());
        m_engine->transportRequests = engine::transport::kContinue;
        status.poll();
        QVERIFY(status.songPlaying());
    }

    // A chart without section titles still plays on its timeline: one part,
    // a bar per chord, every instrument; the chords lit along it.
    void aSongWithoutSectionTitlesPlaysOnItsTimeline()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Piano"_s));
        QVERIFY(m_doc->addChannel(u"spy/Pad.vst3"_s, u"Pad"_s));
        QVERIFY(m_doc->setSongChart(0, u"[C]Just [G]words [Am]and [F]chords\n"_s));
        QVERIFY(m_doc->hasSections());
        QVERIFY(m_doc->currentSections().isEmpty()); // (no titles to show)
        QCOMPARE(m_engine->sections.sections.size(), std::size_t{1});
        QCOMPARE(m_engine->sections.sections.at(0).bars, 4);
        QCOMPARE(m_engine->sections.sections.at(0).live, (std::vector<core::ChannelId>{channelId(0), channelId(1)}));
        QCOMPARE(m_doc->timelineStep(0, 0.0), 0);
        QCOMPARE(m_doc->timelineStep(0, 12.5), 3); // the 4th bar: F
        m_doc->playSong();
        QCOMPARE(m_engine->played.value_or(std::pair{9, true}).first, 0);
        // Following its chords instead: nothing to count.
        QVERIFY(m_doc->setSongFollowChords(0, true));
        QVERIFY(!m_doc->hasSections());
        QVERIFY(m_engine->sections.sections.empty());
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
        // Undoing the paste (one step) puts the chart back; the time stays the first paste's.
        QVERIFY(m_doc->undo());
        QVERIFY(m_doc->currentChart().contains(u"[C]la [G]la"_s)); // the first paste's chart
        QCOMPARE(m_doc->songTimeNumerator(), 6);
        QVERIFY(m_doc->setSongTimeSignature(0, 3, 4));
        QCOMPARE(m_engine->timeSignature, (std::pair{3, 4}));
        QVERIFY(!m_doc->setSongTimeSignature(0, 3, 5));
        QVERIFY(m_doc->setSongSwitchEarly(0, true));
        QVERIFY(m_doc->songSwitchEarly());
    }

    // ---- The loop pedal

    void loopButtonsActOnTheirChannel()
    {
        addSectionsSong(); // Piano, Strings
        LoopController loops(*m_engine, *m_doc, *m_settings);
        loops.record(1);
        loops.playStop(1);
        loops.undo(0);
        loops.clear(0);
        using C = engine::LoopCommand;
        const std::vector<std::pair<core::ChannelId, C>> asked{
            {channelId(1), C::Record}, {channelId(1), C::PlayStop}, {channelId(0), C::Undo}, {channelId(0), C::Clear}};
        QVERIFY(m_engine->loopCommands == asked);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Loop button for channel 6 ignored"_s));
        loops.record(5);
        QCOMPARE(m_engine->loopCommands.size(), std::size_t{4});

        // What the engine says the loops are doing.
        m_engine->channelLoops = {engine::ChannelLoop{.channel = channelId(1), .state = engine::LoopState::Playing,
                                                      .progress = 0.5, .bar = 3, .bars = 4, .layers = 1}};
        QSignalSpy changed(&loops, &LoopController::loopsChanged);
        loops.poll();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(loops.channelLoops().at(0).toMap().value(u"state"_s).toString(), u"empty"_s);
        const QVariantMap strings = loops.channelLoops().at(1).toMap();
        QCOMPARE(strings.value(u"state"_s).toString(), u"playing"_s);
        QCOMPARE(strings.value(u"bar"_s).toInt(), 3);
        QCOMPARE(loops.playingCount(), 1);
        QCOMPARE(loops.allLoops().at(0).toMap().value(u"name"_s).toString(), u"Strings"_s);
        loops.poll();
        QCOMPARE(changed.count(), 1); // nothing new: no signal
    }

    void theKeyboardChoosesTheInstrumentAndLoopsIt()
    {
        addSectionsSong();
        QVERIFY(m_doc->addChannel(u"spy/Lead.vst3"_s, u"Lead"_s)); // three channels
        m_doc->setSelectedChannel(-1);
        LoopController loops(*m_engine, *m_doc, *m_settings);
        using A = engine::LoopAction;
        m_engine->pendingLoopActions = {A::Record};
        loops.poll();
        QCOMPARE(m_doc->selectedChannel(), 0); // none chosen: the first, shown
        QCOMPARE(m_engine->loopCommands.back().first, channelId(0));
        m_engine->pendingLoopActions = {A::NextChannel};
        loops.poll();
        m_engine->pendingLoopActions = {A::PlayStop};
        loops.poll();
        QCOMPARE(m_doc->selectedChannel(), 1);
        QCOMPARE(m_engine->loopCommands.back(), (std::pair{channelId(1), engine::LoopCommand::PlayStop}));
        m_engine->pendingLoopActions = {A::Clear}; // Record + Loop held
        loops.poll();
        QCOMPARE(m_engine->loopCommands.back(), (std::pair{channelId(1), engine::LoopCommand::Clear}));
        // A knob with a range: split among the three.
        m_engine->selectorMove = engine::SelectorMove{.value = 127, .steps = 0};
        loops.poll();
        QCOMPARE(m_doc->selectedChannel(), 2);
        m_engine->selectorMove = engine::SelectorMove{.value = 0, .steps = 0};
        loops.poll();
        QCOMPARE(m_doc->selectedChannel(), 0);
        // An encoder: steps, stopping at the ends.
        m_engine->selectorMove = engine::SelectorMove{.value = -1, .steps = 5};
        loops.poll();
        QCOMPARE(m_doc->selectedChannel(), 2);
        m_engine->selectorMove = engine::SelectorMove{.value = -1, .steps = -1};
        loops.poll();
        QCOMPARE(m_doc->selectedChannel(), 1);
        m_engine->pendingLoopActions = {A::PreviousChannel};
        loops.poll();
        m_engine->pendingLoopActions = {A::PreviousChannel};
        loops.poll();
        QCOMPARE(m_doc->selectedChannel(), 0);
        m_engine->pendingLoopActions = {A::StopAll};
        const int stops = m_engine->loopStops;
        loops.poll();
        QCOMPARE(m_engine->loopStops, stops + 1);
    }

    void looperControlsAreLearnedAndKeptWithTheSetlist()
    {
        addSectionsSong();
        LoopController loops(*m_engine, *m_doc, *m_settings);
        const engine::MidiTrigger pad{.kind = engine::MidiTrigger::Note, .channel = 9, .number = 36};
        loops.learn(static_cast<int>(engine::LoopAction::Record));
        QCOMPARE(loops.learning(), 0);
        loops.poll(); // nothing pressed yet
        QCOMPARE(loops.learning(), 0);
        m_engine->learned = pad;
        loops.poll();
        QCOMPARE(loops.learning(), -1);
        QCOMPARE(m_doc->loopControls().buttons.at(core::LoopControls::Record),
                 (core::LearnedControl{.kind = 0x90, .channel = 10, .number = 36}));
        QVERIFY(m_engine->loopButtons.at(0) == pad); // playing at once
        QCOMPARE(loops.controls().at(0).toMap().value(u"trigger"_s).toString(), pad.describe());
        QVERIFY(m_doc->isDirty());
        // The same pad for Loop: it moves there.
        loops.learn(1);
        m_engine->learned = pad;
        loops.poll();
        QVERIFY(!m_doc->loopControls().buttons.at(core::LoopControls::Record).isSet());
        QVERIFY(m_doc->loopControls().buttons.at(core::LoopControls::PlayStop).isSet());

        // A knob turned a little: an endless encoder (small steps)...
        loops.learn(LoopController::kLearnSelector);
        m_engine->controllerMoves = {{1, 21, 1}, {1, 21, 1}, {1, 21, 2}};
        loops.poll();
        QCOMPARE(loops.learning(), LoopController::kLearnSelector); // three moves: not enough yet
        m_engine->controllerMoves = {{1, 21, 127}};
        loops.poll();
        QCOMPARE(loops.learning(), -1);
        QCOMPARE(m_doc->loopControls().selector, (core::LearnedControl{.kind = 0xB0, .channel = 1, .number = 21}));
        QCOMPARE(loops.selectorMode(), int(core::LoopControls::Relative));
        QCOMPARE(m_engine->selector.mode, engine::SelectorKnob::Relative);
        // ... or one with a range.
        loops.learn(LoopController::kLearnSelector);
        m_engine->controllerMoves = {{2, 74, 10}, {2, 74, 30}, {2, 74, 50}, {2, 74, 70}};
        loops.poll();
        QCOMPARE(m_doc->loopControls().selector.number, 74);
        QCOMPARE(loops.selectorMode(), int(core::LoopControls::Absolute));

        // Saved with the setlist and read back.
        QVERIFY(m_doc->saveAs(path(u"loops.gigchain"_s)));
        const QString saved = m_doc->filePath(); // (with the setlist ending)
        m_doc->newSetlist();
        QVERIFY(!m_doc->loopControls().selector.isSet());
        QVERIFY2(m_doc->open(saved), qPrintable(m_doc->lastError()));
        QCOMPARE(m_doc->loopControls().selector.number, 74);
        QCOMPARE(m_engine->selector.knob.number, uint8_t{74}); // and to the engine
        // Undone like any edit.
        loops.forget(LoopController::kLearnSelector);
        QVERIFY(!m_doc->loopControls().selector.isSet());
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->loopControls().selector.number, 74);
    }

    void loopsStopWithTheSongAndGoWithIt()
    {
        addSectionsSong();
        QCOMPARE(m_doc->songLoopBars(), 4); // a new song: 4-bar loops
        QCOMPARE(m_engine->loopBars, 4);
        QVERIFY(m_doc->setSongLoopBars(0, 8));
        QCOMPARE(m_engine->loopBars, 8);
        QVERIFY(!m_doc->setSongLoopBars(0, 65));
        QCOMPARE(m_doc->songLoopBars(), 8);
        QVERIFY(m_doc->setSongLoopSync(0, false));
        QVERIFY(m_engine->loopSync.has_value() && !*m_engine->loopSync);
        const int stops = m_engine->loopStops;
        m_doc->stopSong();
        QCOMPARE(m_engine->loopStops, stops + 1);
        const int clears = m_engine->loopClears;
        QVERIFY(m_doc->addSong()); // another song is chosen
        QCOMPARE(m_engine->loopClears, clears + 1);
        QVERIFY(m_engine->loopSync.value_or(false)); // its own setting: synced
        QCOMPARE(m_engine->loopBars, 4);             // and 4 bars
        const int after = m_engine->loopClears;
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Piano"_s)); // an edit in the same song: loops stay
        QCOMPARE(m_engine->loopClears, after);
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

    void aSongsChordsAreFollowed()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->setSongFollowChords(0, true));
        QVERIFY(m_doc->setSongChart(0, u"{sov: Verse 1}\n[Am]One [F]two\n{eov}\n{soc: Chorus}\n[C]three [G]four\n{eoc}\n"_s));
        QCOMPARE(m_engine->follow.steps.size(), std::size_t{4});
        QCOMPARE(m_engine->follow.sectionStarts, (std::vector<int>{0, 2}));
        QVERIFY(m_doc->following());
        QCOMPARE(m_doc->followFirstChord(), u"Am"_s);
        QCOMPARE(m_doc->followLabel(0), u"Verse 1 · chord 1 of 2"_s);
        QCOMPARE(m_doc->followLabel(3), u"Chorus · chord 2 of 2"_s);
        // chartLines: {sov}, the verse line, {soc}, the chorus line.
        QCOMPARE(m_doc->followLine(2), 3);
        const QVariantList lines = m_doc->chartLines(m_doc->currentChart());
        const QVariantList chorus = lines.at(3).toMap().value(u"segments"_s).toList();
        QCOMPARE(chorus.at(0).toMap().value(u"steps"_s).toList(), (QVariantList{2}));

        // By tempo instead: not followed.
        QVERIFY(m_doc->setSongFollowChords(0, false));
        QVERIFY(!m_doc->songFollowChords());
        QVERIFY(m_engine->follow.steps.empty());
        QVERIFY(!m_doc->following());
        QVERIFY(m_doc->followFirstChord().isEmpty());
    }

    void aChordTheAppCannotReadIsMarked()
    {
        const QVariantList lines = m_doc->chartLines(u"[Am]One [Xq7]two\n"_s);
        const QVariantList segments = lines.at(0).toMap().value(u"segments"_s).toList();
        QVERIFY(segments.at(0).toMap().value(u"understood"_s).toBool());
        QVERIFY(!segments.at(1).toMap().value(u"understood"_s).toBool());
        QVERIFY(segments.at(1).toMap().value(u"steps"_s).toList().isEmpty());
    }

    // Choosing a song sends its top section before its chords: the jump is
    // then older than the map and does not start following (the song waits
    // for its first chord).
    void aNewSongsTopIsChosenBeforeItsChords()
    {
        addSectionsSong();
        QVERIFY(m_doc->setSongFollowChords(0, true));
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->setSongFollowChords(1, true));
        QVERIFY(m_doc->setSongChart(1, u"{comment: Intro}\n[Am]one [F]two\n"_s));
        m_engine->jumps.clear();
        m_doc->previousSong();
        QCOMPARE(m_engine->jumps, std::vector<int>{0});
        QCOMPARE(m_engine->follow.steps.size(), std::size_t{3});
        QCOMPARE(m_engine->jumpsAtFollow, std::size_t{1}); // the jump came first
    }

    // Following chords, the backing-track pedal plays and pauses the track
    // alone: nothing to stop (the chords lead), and the loops play on.
    void theTrackPedalWhileFollowingPlaysTheTrack()
    {
        addSectionsSong();
        EngineStatus status(*m_engine, *m_doc);
        m_engine->setBackingTrack(u"C:/songs/backing.wav"_s);
        m_engine->followPosition = engine::ChordFollowPosition{.active = true, .started = true, .step = 1, .section = 0};
        m_engine->position.playing = true; // (what the real engine reports once the first chord is heard)
        const int stops = m_engine->stops;
        status.playPauseTrack();
        QVERIFY(m_engine->track.playing);
        QCOMPARE(m_engine->stops, stops);
        QVERIFY(!m_engine->played.has_value());
        status.playPauseTrack();
        QVERIFY(!m_engine->track.playing);
        QCOMPARE(m_engine->stops, stops);
    }

    // The Chart tab edited in place: words typed, a chord put on a word and
    // dragged to another, a section added and named; each an undo step, and
    // each line of the chart says where it is so the screen can edit it.
    void theChartIsEditedInPlace()
    {
        QVERIFY(m_doc->setSongChart(0, u"{comment: Verse}\nI need your love"_s));
        const QVariantList lines = m_doc->chartLines(m_doc->currentChart());
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(1).toMap().value(u"line"_s).toInt(), 1);

        QVERIFY(m_doc->placeChordAt(1, 13, u"Gm"_s)); // dropped on "lo|ve"
        QCOMPARE(m_doc->currentChart(), u"{comment: Verse}\nI need your [Gm]love"_s);
        QCOMPARE(m_doc->currentChartChords(), QStringList{u"Gm"_s});
        const QVariantList segments = m_doc->chartLines(m_doc->currentChart()).at(1).toMap().value(u"segments"_s).toList();
        QCOMPARE(segments.at(1).toMap().value(u"at"_s).toInt(), 12);         // where its words start
        QCOMPARE(segments.at(1).toMap().value(u"chordIndex"_s).toInt(), 0);  // the line's first chord

        QVERIFY(m_doc->moveChordTo(1, 0, 1, 7)); // dragged to "your"
        QCOMPARE(m_doc->currentChart(), u"{comment: Verse}\nI need [Gm]your love"_s);
        QVERIFY(m_doc->setLineLyrics(1, u"I need all your love"_s)); // typed: Gm stays on "your"
        QCOMPARE(m_doc->currentChart(), u"{comment: Verse}\nI need all [Gm]your love"_s);
        QVERIFY(m_doc->renameChordAt(1, 0, u"Gm7"_s));
        QVERIFY(m_doc->addChartSection(u"Chorus"_s));
        QCOMPARE(m_doc->currentChart(), u"{comment: Verse}\nI need all [Gm7]your love\n\n{comment: Chorus}\n"_s);
        QVERIFY(m_doc->renameChartSection(0, u"Verse 1"_s));
        QCOMPARE(m_doc->currentSections().size(), 2);

        // Each change its own undo step.
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->currentChart(), u"{comment: Verse}\nI need all [Gm7]your love\n\n{comment: Chorus}\n"_s);
        QVERIFY(m_doc->undo());
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->currentChart(), u"{comment: Verse}\nI need all [Gm]your love"_s);

        // A change that cannot be made is said, and the chart is untouched.
        const QString before = m_doc->currentChart();
        const int shown = m_doc->notifications()->count();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"not a line of words"_s));
        QVERIFY(!m_doc->placeChordAt(0, 0, u"C"_s)); // on the section title
        QCOMPARE(m_doc->currentChart(), before);
        QCOMPARE(m_doc->notifications()->count(), shown + 1);
    }

    // Typing a line is one undo step, not one per letter.
    void typingALineIsOneUndoStep()
    {
        QVERIFY(m_doc->setSongChart(0, u"Words"_s));
        QVERIFY(m_doc->setLineLyrics(0, u"Words a"_s));
        QVERIFY(m_doc->setLineLyrics(0, u"Words ab"_s));
        QVERIFY(m_doc->setLineLyrics(0, u"Words abc"_s));
        QVERIFY(m_doc->undo());
        QCOMPARE(m_doc->currentChart(), u"Words"_s);
    }

    // A chart with more chords than can be followed (repeats played out):
    // the player is told once, and the tempo leads.
    void aChartTooLongToFollowIsSaid()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QString chart = u"{c: Verse x16}\n"_s;
        for (int i = 0; i < 300; ++i) chart += u"[C]a [G]b "_s;
        chart += u"x16\n"_s;
        const int before = m_doc->notifications()->count();
        QVERIFY(m_doc->setSongChart(0, chart));
        QVERIFY(m_engine->follow.steps.empty());
        QVERIFY(!m_doc->following());
        QCOMPARE(m_doc->notifications()->count(), before + 1);
        QVERIFY2(m_doc->notifications()->text(before).contains(u"too many chords"_s),
                 qPrintable(m_doc->notifications()->text(before)));
        // Edited again (still too long): not said again.
        QVERIFY(m_doc->setSongChart(0, chart + u"more words\n"_s));
        QCOMPARE(m_doc->notifications()->count(), before + 1);
    }

    // Each chord played updates the toolbar ("Chorus · chord 2 of 8") and
    // the chart's place: on the longest chart it takes less than a frame.
    void followingALongChartKeepsUp()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->setSongFollowChords(0, true));
        QString chart;
        for (int s = 0; chart.size() < 99'000; ++s) {
            chart += u"{c: Part %1}\n"_s.arg(s + 1);
            for (int line = 0; line < 4; ++line) chart += u"[Am]These are the words of a very long [G]song that goes on and on\n"_s;
        }
        QVERIFY(m_doc->setSongChart(0, chart.left(99'000)));
        QVERIFY(m_doc->following());
        QElapsedTimer timer;
        timer.start();
        const int step = 1500;
        const QString label = m_doc->followLabel(step);
        const int line = m_doc->followLine(step);
        const qint64 perChord = timer.elapsed();
        timer.restart();
        const QVariantList lines = m_doc->chartLines(m_doc->currentChart());
        const qint64 wholeChart = timer.elapsed();
        qInfo() << "per chord:" << perChord << "ms (" << label << ", line" << line << "); chartLines:" << wholeChart << "ms,"
                << lines.size() << "lines";
        QVERIFY(!label.isEmpty() && line > 0);
        QVERIFY2(perChord < 16, qPrintable(u"%1 ms per chord"_s.arg(perChord)));
    }

    // The engine refusing a song's chords is shown to the player.
    void aRefusedChordMapIsShown()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        m_engine->followRefusal = core::Error{core::ErrorCode::InvalidData, u"Chord follow refused: chord 3 is wrong"_s};
        const int before = m_doc->notifications()->count();
        QVERIFY(m_doc->setSongChart(0, u"[Am]One [F]two [C]three\n"_s));
        QCOMPARE(m_doc->notifications()->count(), before + 1);
        QCOMPARE(m_doc->notifications()->text(before), u"Chord follow refused: chord 3 is wrong"_s);
        QVERIFY(!m_doc->following()); // not shown as following
    }

    void anEditedChartKeepsItsPlace()
    {
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->setSongFollowChords(0, true));
        QVERIFY(m_doc->setSongChart(0, u"[Am]One [F]two [C]three [G]four\n"_s));
        m_engine->followPosition = engine::ChordFollowPosition{.active = true, .started = true, .step = 2, .section = -1};
        QVERIFY(m_doc->setSongChart(0, u"[Am]One [F]two [C]tres [G]four\n"_s)); // a word fixed while playing C
        QCOMPARE(m_engine->follow.resumeAt, 2);
    }
};

QTEST_GUILESS_MAIN(TestDocumentController)
#include "tst_document_controller.moc"
