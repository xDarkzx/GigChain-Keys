#include "DocumentController.h"
#include "LeakCheck.h"
#include "SpyEngine.h"

#include "gigchain/core/SetlistFile.h"

#include <QFile>
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

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"settings.ini"_s), QSettings::IniFormat);
        m_engine = std::make_unique<test::SpyEngine>();
        m_doc = std::make_unique<DocumentController>(*m_engine, *m_settings);
    }

    void cleanup()
    {
        m_doc.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    void startsWithOneSongAndAppliesIt()
    {
        QVERIFY(m_doc->hasPatch());
        QCOMPARE(m_doc->currentSongName(), u"Song 1"_s);
        QCOMPARE(m_doc->currentPatchName(), u"Patch 1"_s);
        QCOMPARE(m_engine->applyCount, 1);
        QVERIFY(!m_doc->isDirty());
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

    void restoreLastSessionReopensTheFile()
    {
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->saveAs(path(u"gig.gigchain.json"_s)));
        QVERIFY(m_doc->open(path(u"gig.gigchain.json"_s))); // remembers it

        DocumentController second(*m_engine, *m_settings);
        second.restoreLastSession();
        QCOMPARE(second.filePath(), path(u"gig.gigchain.json"_s));
        QCOMPARE(second.setlist().songs.size(), std::size_t{2});
    }

    void restoreWithMissingFileReportsIt()
    {
        m_settings->setValue(u"session/lastFile"_s, path(u"gone.gigchain.json"_s));
        DocumentController second(*m_engine, *m_settings);
        second.restoreLastSession();
        QVERIFY(second.lastError().contains(u"gone.gigchain.json"_s));
        QCOMPARE(second.setlist().songs.size(), std::size_t{1});
    }

    void editingCycleDoesNotLeak()
    {
        QCOMPARE(test::leakedBlocks([this] {
                     QVERIFY(m_doc->addSong());
                     QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
                     QVERIFY(m_doc->removeSong(m_doc->songIndex()));
                 }),
                 0LL);
    }
};

QTEST_GUILESS_MAIN(TestDocumentController)
#include "tst_document_controller.moc"
