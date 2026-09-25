#include "ChannelModel.h"
#include "DocumentController.h"
#include "EditorService.h"
#include "OfficialArtwork.h"
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

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

// Art sources that exist nowhere, so these tests never read the real machine.
OfficialArtwork::Sources noArtwork()
{
    OfficialArtwork::Sources sources;
    sources.arturiaRoot = u"Z:/none/Arturia"_s;
    sources.niServiceCenter = u"Z:/none/ServiceCenter"_s;
    sources.niResources = u"Z:/none/NI Resources"_s;
    sources.niContentDir = [](const QString&) { return QString(); };
    return sources;
}

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
        m_doc->newSetlist();
        QVERIFY(m_doc->addSong());
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
        const OfficialArtwork artwork(noArtwork());
        ChannelModel model(*m_doc, *m_engine, artwork);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(roleData(model, 0, "instrumentName").toString(), u"Spy Piano"_s);
        QCOMPARE(roleData(model, 0, "icon").toString(), u"qrc:/qt/qml/GigChain/Ui/icons/piano.svg"_s);
        QVERIFY(roleData(model, 0, "color").toString().startsWith(u'#'));
        QCOMPARE(roleData(model, 0, "pan").toDouble(), 0.0);
        QVERIFY(m_doc->setChannelPan(0, 0.5));
        QCOMPARE(roleData(model, 0, "pan").toDouble(), 0.5);
        QVERIFY(roleData(model, 0, "selected").toBool());

        QVERIFY(m_doc->addEffect(0, u"spy/Reverb.vst3"_s, u"Spy Reverb"_s));
        QCOMPARE(roleData(model, 0, "effectNames").toStringList(), QStringList{u"Spy Reverb"_s});
        QCOMPARE(roleData(model, 0, "effectBypassed").toList(), QVariantList{false});
        QVERIFY(m_doc->setEffectBypass(0, 0, true));
        QCOMPARE(roleData(model, 0, "effectBypassed").toList(), QVariantList{true});
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

    void pluginListShowsDetails()
    {
        const OfficialArtwork artwork(noArtwork());
        PluginListModel model(*m_engine, artwork);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(roleData(model, 0, "name").toString(), u"Spy Pad"_s);
        QCOMPARE(roleData(model, 0, "version").toString(), u"1.0"_s);
        QCOMPARE(roleData(model, 0, "category").toString(), u"Synth"_s);
        QVERIFY(roleData(model, 0, "icon").toString().endsWith(u"wave-sine.svg"_s));

        model.setInstrumentsOnly(true); // the browser list: installed instruments only
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(roleData(model, 1, "kind").toString(), u"instrument"_s);
    }

    void hiddenInstrumentsStayHidden()
    {
        const OfficialArtwork artwork(noArtwork());
        QSettings settings(m_dir->filePath(u"hide.ini"_s), QSettings::IniFormat);
        {
            PluginListModel model(*m_engine, artwork, &settings);
            model.setInstrumentsOnly(true);
            QCOMPARE(model.rowCount(), 2);
            QSignalSpy menus(&model, &PluginListModel::menusChanged);
            model.hide(u"spy/Pad.vst3"_s);
            QCOMPARE(model.rowCount(), 1);
            QCOMPARE(roleData(model, 0, "name").toString(), u"Spy Piano"_s);
            // hidden plugins leave the pickers too, and the pickers are told
            QCOMPARE(menus.count(), 1);
            const QVariantList vendors = model.instrumentMenu().value(u"vendors"_s).toList();
            QCOMPARE(vendors.size(), 1);
            const QVariantList plugins = vendors[0].toMap().value(u"plugins"_s).toList();
            QCOMPARE(plugins.size(), 1);
            QCOMPARE(plugins[0].toMap().value(u"name"_s).toString(), u"Spy Piano"_s);
        }
        PluginListModel again(*m_engine, artwork, &settings); // next start
        again.setInstrumentsOnly(true);
        QCOMPARE(again.rowCount(), 1);
        again.showAll();
        QCOMPARE(again.rowCount(), 2);
        QCOMPARE(again.instrumentMenu().value(u"vendors"_s).toList()[0].toMap().value(u"plugins"_s).toList().size(), 2);
    }

    void favouritesStayOnTopAndRatingsAreRemembered()
    {
        const OfficialArtwork artwork(noArtwork());
        QSettings settings(m_dir->filePath(u"fav.ini"_s), QSettings::IniFormat);
        {
            PluginListModel model(*m_engine, artwork, &settings);
            model.setInstrumentsOnly(true);
            QCOMPARE(roleData(model, 0, "name").toString(), u"Spy Pad"_s); // by name
            QVERIFY(!roleData(model, 0, "favorite").toBool());
            model.setFavorite(u"spy/Piano.vst3"_s, true);
            QCOMPARE(roleData(model, 0, "name").toString(), u"Spy Piano"_s); // favourites first
            QVERIFY(roleData(model, 0, "favorite").toBool());

            model.setRating(u"spy/Pad.vst3"_s, 4);
            QCOMPARE(roleData(model, 1, "rating").toInt(), 4);
            model.setRating(u"spy/Pad.vst3"_s, 9); // out of range: clamped
            QCOMPARE(roleData(model, 1, "rating").toInt(), 5);
        }
        PluginListModel again(*m_engine, artwork, &settings); // next start
        again.setInstrumentsOnly(true);
        QCOMPARE(roleData(again, 0, "name").toString(), u"Spy Piano"_s);
        QCOMPARE(roleData(again, 1, "rating").toInt(), 5);
        again.setFavorite(u"spy/Piano.vst3"_s, false);
        again.setRating(u"spy/Pad.vst3"_s, 0); // 0 = no rating
        QCOMPARE(roleData(again, 0, "name").toString(), u"Spy Pad"_s);
        QCOMPARE(roleData(again, 0, "rating").toInt(), 0);
    }

    void pluginDetailsForTheInfoPanel()
    {
        const OfficialArtwork artwork(noArtwork());
        PluginListModel model(*m_engine, artwork);
        model.setInstrumentsOnly(true);
        QCOMPARE(roleData(model, 1, "website").toString(), u"https://spy.example"_s);
        QCOMPARE(roleData(model, 1, "email").toString(), u"help@spy.example"_s);
        QCOMPARE(roleData(model, 1, "sdkVersion").toString(), u"VST 3.8.0"_s);
        QCOMPARE(roleData(model, 1, "tags").toStringList(), (QStringList{u"Instrument"_s, u"Piano"_s}));
        QCOMPARE(roleData(model, 1, "location").toString(), u"spy\\Piano.vst3"_s); // shown Windows-style
    }

    void showInFolderRefusesUnknownPlugins()
    {
        const OfficialArtwork artwork(noArtwork());
        PluginListModel model(*m_engine, artwork);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Show in folder: no plugin .*nope\\.vst3"_s));
        QCOMPARE(model.showInFolder(u"C:/nope.vst3"_s), u"No installed plugin C:/nope.vst3"_s);
    }

    void pluginListGroupsAndFilters()
    {
        const OfficialArtwork artwork(noArtwork());
        PluginListModel model(*m_engine, artwork);
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
        QCOMPARE(model.findInstrument(u"pad"_s).value(u"name"_s).toString(), u"Spy Pad"_s);
        QVERIFY(model.findInstrument(u"Kontakt"_s).isEmpty());
        const QVariantMap menu = model.effectMenu();
        const QVariantList categories = menu.value(u"categories"_s).toList();
        QCOMPARE(categories.size(), 1);
        QCOMPARE(categories[0].toMap().value(u"title"_s).toString(), u"Reverb"_s);
        QCOMPARE(categories[0].toMap().value(u"plugins"_s).toList()[0].toMap().value(u"name"_s).toString(), u"Spy Reverb"_s);
        const QVariantList vendors = menu.value(u"vendors"_s).toList();
        QCOMPARE(vendors.size(), 1);
        QCOMPARE(vendors[0].toMap().value(u"title"_s).toString(), u"Other"_s);
        const QVariantList instrumentVendors = model.instrumentMenu().value(u"vendors"_s).toList();
        QCOMPARE(instrumentVendors.size(), 1);
        QCOMPARE(instrumentVendors[0].toMap().value(u"title"_s).toString(), u"Spy"_s);
        QCOMPARE(instrumentVendors[0].toMap().value(u"plugins"_s).toList().size(), 2);
        const QVariantList effects = model.effects();
        QCOMPARE(effects.size(), 1);
        QCOMPARE(effects[0].toMap().value(u"name"_s).toString(), u"Spy Reverb"_s);
    }

    void editorServiceFollowsTheSelectedChannel()
    {
        EditorService service(*m_engine, *m_doc);
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
        EditorService service(*m_engine, *m_doc);
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
        EditorService service(*m_engine, *m_doc);
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
        QVERIFY(status.memoryMb() > 1.0); // this process's working set
        QVERIFY(status.midiActivity());
        QCOMPARE(status.statusText(), u"Spy engine"_s);
        QCOMPARE(status.masterPeak(), 0.4F); // the master strip's meter
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
