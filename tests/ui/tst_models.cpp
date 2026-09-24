#include "ChannelModel.h"
#include "DocumentController.h"
#include "ArtworkBuilder.h"
#include "ArtworkCache.h"
#include "EditorService.h"
#include "EngineStatus.h"
#include "PluginListModel.h"
#include "SelectedChannel.h"
#include "SetlistModel.h"
#include "SpyEngine.h"

#include <QAbstractItemModelTester>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace openstage;
using namespace openstage::ui;
using namespace Qt::StringLiterals;

namespace {

QVariant roleData(const QAbstractItemModel& model, int row, const QByteArray& roleName)
{
    const auto roles = model.roleNames();
    const int role = roles.key(roleName, -1);
    return model.data(model.index(row, 0), role);
}

} // namespace

class TestModels : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<test::SpyEngine> m_engine;
    std::unique_ptr<DocumentController> m_doc;

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"s.ini"_s), QSettings::IniFormat);
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

    void setlistModelListsSongsThenPatches()
    {
        SetlistModel model(*m_doc);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(roleData(model, 0, "kind").toString(), u"song"_s);
        QCOMPARE(roleData(model, 1, "kind").toString(), u"patch"_s);
        QVERIFY(roleData(model, 1, "isCurrent").toBool());

        QVERIFY(m_doc->addPatch(0));
        QVERIFY(m_doc->addSong());
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(roleData(model, 3, "name").toString(), u"Song 2"_s);
        QCOMPARE(roleData(model, 3, "number").toInt(), 2);
        QVERIFY(roleData(model, 4, "isCurrent").toBool());
        QVERIFY(!roleData(model, 1, "isCurrent").toBool());

        QVERIFY(m_doc->renameSong(0, u"Opener"_s));
        QCOMPARE(roleData(model, 0, "name").toString(), u"Opener"_s);
    }

    void channelModelFollowsTheCurrentPatch()
    {
        ChannelModel model(*m_doc, *m_engine);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(roleData(model, 0, "instrumentName").toString(), u"Spy Piano"_s);
        QCOMPARE(roleData(model, 0, "icon").toString(), u"qrc:/qt/qml/OpenStage/Ui/icons/piano.svg"_s);
        QVERIFY(roleData(model, 0, "color").toString().startsWith(u'#'));
        QCOMPARE(roleData(model, 0, "pan").toDouble(), 0.0);
        QVERIFY(m_doc->setChannelPan(0, 0.5));
        QCOMPARE(roleData(model, 0, "pan").toDouble(), 0.5);
        QVERIFY(roleData(model, 0, "selected").toBool());

        QVERIFY(m_doc->addEffect(0, u"spy/Reverb.vst3"_s, u"Spy Reverb"_s));
        QCOMPARE(roleData(model, 0, "effectNames").toStringList(), QStringList{u"Spy Reverb"_s});
        QVERIFY(m_doc->setChannelVolume(0, -3.0));
        QCOMPARE(roleData(model, 0, "volumeDb").toDouble(), -3.0);

        model.refreshLevels();
        QCOMPARE(roleData(model, 0, "peak").toFloat(), 0.5F);

        QVERIFY(m_doc->addPatch(0)); // moves to an empty patch
        QCOMPARE(model.rowCount(), 0);
    }

    void selectedChannelMirrorsTheSelection()
    {
        SelectedChannel selected(*m_doc);
        QSignalSpy changed(&selected, &SelectedChannel::changed);
        QVERIFY(!selected.isValid());
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(selected.isValid());
        QCOMPARE(selected.name(), u"Spy Piano"_s);
        QVERIFY(m_doc->setChannelTranspose(0, 12));
        QCOMPARE(selected.transpose(), 12);
        QVERIFY(changed.count() >= 2);
    }

    void pluginListShowsArtworkAndDetails()
    {
        ArtworkCache cache(m_dir->filePath(u"artwork"_s));
        PluginListModel model(*m_engine, cache);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(roleData(model, 0, "name").toString(), u"Spy Pad"_s);
        QCOMPARE(roleData(model, 0, "version").toString(), u"1.0"_s);
        QCOMPARE(roleData(model, 0, "category").toString(), u"Synth"_s);
        QVERIFY(roleData(model, 0, "icon").toString().endsWith(u"wave-sine.svg"_s));
        QVERIFY(roleData(model, 0, "imageUrl").toString().isEmpty());

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QImage picture(200, 100, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        QVERIFY(cache.store(u"spy/Pad.vst3"_s, picture).has_value());
        QCOMPARE(changed.count(), 1);
        QVERIFY(roleData(model, 0, "imageUrl").toString().startsWith(u"file:"_s));
    }

    void artworkBuilderWalksEveryPluginWithoutArtwork()
    {
        ArtworkCache cache(m_dir->filePath(u"artwork"_s));
        ArtworkBuilder builder(*m_engine, cache);
        QSignalSpy finished(&builder, &ArtworkBuilder::finished);
        builder.start();
        QVERIFY(builder.isRunning());
        QVERIFY(finished.wait(5000));
        QVERIFY(!builder.isRunning());
        QCOMPARE(builder.done(), builder.total());
        QCOMPARE(builder.total(), 3);
        // The spy has no editors, so it was asked for each and nothing was stored.
        QCOMPARE(m_engine->pluginEditorRequests.size(), std::size_t{3});
        QVERIFY(!cache.has(u"spy/Piano.vst3"_s));
    }

    void pluginListGroupsAndFilters()
    {
        ArtworkCache cache(m_dir->filePath(u"artwork"_s));
        PluginListModel model(*m_engine, cache);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(roleData(model, 0, "kind").toString(), u"instrument"_s); // instruments first
        QCOMPARE(roleData(model, 2, "kind").toString(), u"effect"_s);

        model.setFilterText(u"pad"_s);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(roleData(model, 0, "name").toString(), u"Spy Pad"_s);
        model.setFilterText(u"other"_s); // matches vendor
        QCOMPARE(model.rowCount(), 1);
        model.setFilterText({});
        QCOMPARE(model.rowCount(), 3);

        QCOMPARE(model.instruments().size(), 2);
        const QVariantList effects = model.effects();
        QCOMPARE(effects.size(), 1);
        QCOMPARE(effects[0].toMap().value(u"name"_s).toString(), u"Spy Reverb"_s);
    }

    void editorServiceFollowsTheSelectedChannel()
    {
        ArtworkCache cache(m_dir->filePath(u"artwork"_s));
        EditorService service(*m_engine, *m_doc, cache);
        QSignalSpy target(&service, &EditorService::targetChanged);
        QCOMPARE(service.emptyReason(), u"Drag an instrument here to start this patch"_s);

        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->addChannel(u"spy/Pad.vst3"_s, u"Spy Pad"_s));
        QTRY_VERIFY(target.count() >= 1);

        m_doc->setSelectedChannel(0);
        const auto editor = service.createForSelection();
        QVERIFY(editor.has_value());
        QCOMPARE(m_engine->editorRequests.back(), m_doc->currentPatch()->channels[0].id.value());
        QCOMPARE(service.emptyReason(), u"Spy Piano has no editor to show"_s);

        m_doc->setSelectedChannel(-1);
        QCOMPARE(service.emptyReason(), u"Select a channel in the mixer"_s);
    }

    void mixerMovesDoNotRebuildTheEditor()
    {
        // Volume, pan, mute and solo never change which plugin is shown; the
        // editor must not be closed and reopened while a fader moves.
        ArtworkCache cache(m_dir->filePath(u"artwork"_s));
        EditorService service(*m_engine, *m_doc, cache);
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QTest::qWait(10);
        QSignalSpy target(&service, &EditorService::targetChanged);
        QVERIFY(m_doc->setChannelVolume(0, -3.0));
        QVERIFY(m_doc->setChannelPan(0, 0.3));
        QVERIFY(m_doc->setChannelMute(0, true));
        QVERIFY(m_doc->setChannelSolo(0, true));
        QVERIFY(m_doc->setChannelName(0, u"Keys"_s));
        QTest::qWait(20);
        QCOMPARE(target.count(), 0);
    }

    void editorServiceSignalsOncePerChange()
    {
        // A patch change emits several document signals; the editor (slow to
        // open) must be rebuilt once, not once per signal.
        ArtworkCache cache(m_dir->filePath(u"artwork"_s));
        EditorService service(*m_engine, *m_doc, cache);
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->addPatch(0));
        QTest::qWait(10);
        QSignalSpy target(&service, &EditorService::targetChanged);
        m_doc->previousPatch();
        QVERIFY(target.wait(500));
        QTest::qWait(20);
        QCOMPARE(target.count(), 1);
    }

    void engineStatusPollsAndForwardsNotices()
    {
        EngineStatus status(*m_engine, *m_doc);
        QSignalSpy polled(&status, &EngineStatus::polled);
        m_engine->pendingNotices.push_back(u"Audio device restarted"_s);
        status.poll();
        QCOMPARE(polled.count(), 1);
        QCOMPARE(status.cpuLoad(), 0.25F);
        QVERIFY(status.midiActivity());
        QCOMPARE(status.statusText(), u"Spy engine"_s);
        QCOMPARE(m_doc->lastError(), u"Audio device restarted"_s);

        status.setMasterVolumeDb(-6.0);
        QCOMPARE(m_engine->master, -6.0);
        status.playNote(60, true);
        status.playNote(60, false);
        QCOMPARE(m_engine->notes.size(), std::size_t{2});
        QCOMPARE(m_engine->notes[0][2], 100);
        QCOMPARE(m_engine->notes[1][2], 0);
    }
};

QTEST_GUILESS_MAIN(TestModels)
#include "tst_models.moc"
