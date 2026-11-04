#include "ChannelModel.h"
#include "DocumentController.h"
#include "EditorService.h"
#include "EffectWindows.h"
#include "OfficialArtwork.h"
#include "EngineStatus.h"
#include "MixerLayout.h"
#include "Notifications.h"
#include "PluginListModel.h"
#include "SelectedChannel.h"
#include "SetlistModel.h"
#include "SpyEngine.h"
#include "SustainPadsController.h"
#include "gigchain/engine/BuiltinReadings.h"
#include "gigchain/engine/SidechainCurves.h"

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

// A plugin folder that exists nowhere, so these tests never read the real machine.
QString noArtwork()
{
    return u"Z:/none/VST3"_s;
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
        QCOMPARE(roleData(model, 0, "officialIcon").toBool(), false); // no icon in its folder: a category icon

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
        // A plugin's "website" that is not one (a file, a program) is not offered as a link.
        QCOMPARE(roleData(model, 0, "name").toString(), u"Spy Pad"_s);
        QCOMPARE(roleData(model, 0, "website").toString(), QString());
        QCOMPARE(roleData(model, 1, "email").toString(), u"help@spy.example"_s);
        QCOMPARE(roleData(model, 1, "sdkVersion").toString(), u"VST 3.8.0"_s);
        QCOMPARE(roleData(model, 1, "tags").toStringList(), (QStringList{u"Instrument"_s, u"Piano"_s}));
        // Shown the system's way (Windows: backslashes).
        QCOMPARE(roleData(model, 1, "location").toString(), QDir::toNativeSeparators(u"spy/Piano.vst3"_s));
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
        QCOMPARE(service.emptyReason(), u"Drag an instrument here to start this sound"_s);

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

    void theAppsOwnInstrumentIsShownByTheApp()
    {
        // Saw Stack Pro has no window of its own: the main area draws its
        // knobs, which reach the engine; a plugin with a window is not one.
        EditorService service(*m_engine, *m_doc);
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Spy Piano"_s));
        QVERIFY(m_doc->addChannel(u"builtin:saw-stack-pro"_s, u"Saw Stack Pro"_s));
        m_doc->setSelectedChannel(0);
        QVERIFY(!service.builtinSelected());
        QVERIFY(service.builtinParameters().isEmpty());
        m_doc->setSelectedChannel(1);
        QVERIFY(service.builtinSelected());
        QCOMPARE(service.builtinName(), u"Saw Stack Pro"_s);
        const QVariantList parameters = service.builtinParameters();
        QCOMPARE(parameters.size(), static_cast<qsizetype>(m_engine->parameters.size()));
        QCOMPARE(parameters.front().toMap().value(u"name"_s).toString(), u"Cutoff"_s);
        service.setBuiltinValue(7, 0.25);
        QCOMPARE(m_engine->parameterValues.at(7), 0.25);
        QCOMPARE(service.builtinValue(7), 0.25);
        // The sidechain's window draws the engine's own curves (not a copy of
        // them) and its playhead where the engine is.
        const QVariantList classic = service.sidechainCurve(0, 0.5, 33);
        QCOMPARE(classic.size(), qsizetype{33});
        for (int i = 0; i < 33; ++i) QCOMPARE(classic.at(i).toDouble(), engine::sidechain::curveGain(0, i / 32.0, 0.5));
        QVERIFY(service.sidechainCurve(10, 0.5, 33) != classic);
        m_engine->parameterValues[engine::sidechain::kPositionParameter] = 0.375;
        QCOMPARE(service.sidechainPosition(), 0.375);
        // Which of the app's own it is (its window is chosen by it), and what it shows besides its settings.
        QCOMPARE(service.builtinId(), u"builtin:saw-stack-pro"_s);
        m_engine->parameterValues[engine::readings::kPianoRecordings] = 0.5;
        m_engine->parameterValues[engine::readings::kPianoSource] = 1.0 / 3.0;
        QCOMPARE(service.builtinReading(u"pianoRecordings"_s), 0.5);
        QCOMPARE(service.builtinReading(u"pianoSource"_s), 1.0 / 3.0);
        QCOMPARE(service.builtinReading(u"no such reading"_s), -1.0);
        m_doc->setSelectedChannel(0);
        QCOMPARE(service.builtinId(), QString());
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

    // Where the mixer is, remembered for next time: its place, detached or
    // not, and how wide it is down a side (never too narrow for a strip,
    // nor wider than makes sense).
    void theMixersPlaceIsRemembered()
    {
        {
            MixerLayout layout(*m_settings);
            QCOMPARE(layout.placement(), MixerLayout::Placement::Bottom); // as it always was
            QVERIFY(!layout.detached());
            QSignalSpy placed(&layout, &MixerLayout::placementChanged);
            layout.setPlacement(MixerLayout::Placement::Right);
            layout.setPlacement(MixerLayout::Placement::Right);
            QCOMPARE(placed.count(), 1);
            layout.toggleDetached();
            layout.setSideWidth(100); // too narrow for a strip
            QCOMPARE(layout.sideWidth(), MixerLayout::kMinSideWidth);
            layout.setSideWidth(700); // (not the default: remembered, not just started with)
        }
        MixerLayout again(*m_settings);
        QCOMPARE(again.placement(), MixerLayout::Placement::Right);
        QVERIFY(again.detached());
        QCOMPARE(again.sideWidth(), 700);
        again.toggleDetached();
        QVERIFY(!MixerLayout(*m_settings).detached());
        // A place it never had (a newer version's, an edited file): along the bottom.
        m_settings->setValue(u"mixer/placement"_s, 9);
        QCOMPARE(MixerLayout(*m_settings).placement(), MixerLayout::Placement::Bottom);
    }

    // A key as a chart writes it, as Sustain Pads' key.
    void aWrittenKeyIsReadAsSustainPadsKey()
    {
        QCOMPARE(SustainPadsController::keyOf(u"C"_s), 0);
        QCOMPARE(SustainPadsController::keyOf(u"Dm"_s), 2);
        QCOMPARE(SustainPadsController::keyOf(u"F#"_s), 6);
        QCOMPARE(SustainPadsController::keyOf(u"Bb"_s), 10);
        QCOMPARE(SustainPadsController::keyOf(u" ebmaj7 "_s), 3);
        QCOMPARE(SustainPadsController::keyOf(u"Cb"_s), 11);
        QCOMPARE(SustainPadsController::keyOf(u"B#"_s), 0);
        QCOMPARE(SustainPadsController::keyOf(u"G♭"_s), 6);
        QCOMPARE(SustainPadsController::keyOf(u""_s), -1);
        QCOMPARE(SustainPadsController::keyOf(u"H"_s), -1);
        QCOMPARE(SustainPadsController::keyOf(u"?"_s), -1);
    }

    // A key tapped only picks the key: it never starts the pad (a key picked
    // ahead on stage must stay silent) nor stops it; Activate does both.
    // Playing, a new key fades over to it. The engine hears every change.
    void sustainPadsKeysPickOnlyActivatePlays()
    {
        EffectWindows windows(*m_engine, *m_doc);
        SustainPadsController pads(*m_engine, *m_doc, *m_settings, windows);
        QVERIFY(!pads.active());
        QVERIFY(!m_engine->sustainPads.empty());
        QVERIFY(!m_engine->sustainPads.back().active); // never on by itself
        pads.pickKey(7); // G, picked ahead: silent
        QCOMPARE(pads.key(), 7);
        QVERIFY(!pads.active());
        QCOMPARE(m_engine->sustainPads.back().key, 7);
        QVERIFY(!m_engine->sustainPads.back().active);
        pads.pickKey(7); // again: still only picked
        QVERIFY(!m_engine->sustainPads.back().active);
        pads.setActive(true); // Activate: it plays in G
        QVERIFY(m_engine->sustainPads.back().active);
        QCOMPARE(m_engine->sustainPads.back().key, 7);
        pads.pickKey(2); // to D, still playing
        QCOMPARE(m_engine->sustainPads.back().key, 2);
        QVERIFY(m_engine->sustainPads.back().active);
        pads.pickKey(2); // D again: still playing (only Activate stops it)
        QVERIFY(pads.active());
        QVERIFY(m_engine->sustainPads.back().active);
        pads.setActive(false);
        QVERIFY(!m_engine->sustainPads.back().active);
        QCOMPARE(m_engine->sustainPads.back().key, 2);
        pads.pickKey(-1); // (kept in range: B)
        QCOMPARE(pads.key(), 11);
        QVERIFY(!pads.active());
    }

    // The player's pad, filter, shimmer, fade, volume and output come back
    // next time; the pad never starts by itself. Out of range: kept in range.
    void sustainPadsSettingsAreRemembered()
    {
        {
            EffectWindows windows(*m_engine, *m_doc);
            SustainPadsController pads(*m_engine, *m_doc, *m_settings, windows);
            pads.setPreset(4);
            pads.setTone(0.8);
            pads.setShimmer(0.9);
            pads.setMotion(0.2);
            pads.setFadeSeconds(12.0);
            pads.setVolumeDb(-12.0);
            pads.setOutputPair(2);
            pads.setOctave(1);
            pads.setFollowSongKey(false);
            pads.setShown(true);
            pads.setDetached(true);
            pads.pickKey(9);
            pads.setActive(true);
            QVERIFY(pads.active());
        }
        EffectWindows againWindows(*m_engine, *m_doc);
        SustainPadsController again(*m_engine, *m_doc, *m_settings, againWindows);
        QVERIFY(!again.active());
        QCOMPARE(again.preset(), 4);
        QCOMPARE(again.key(), 9);
        QCOMPARE(again.tone(), 0.8);
        QCOMPARE(again.shimmer(), 0.9);
        QCOMPARE(again.motion(), 0.2);
        QCOMPARE(again.fadeSeconds(), 12.0);
        QCOMPARE(again.volumeDb(), -12.0);
        QCOMPARE(again.outputPair(), 2);
        QCOMPARE(again.octave(), 1);
        QVERIFY(!again.followSongKey());
        QVERIFY(again.shown());
        QVERIFY(again.detached());
        QCOMPARE(m_engine->sustainPads.back(), again.settings()); // and the engine has them
        again.setPreset(99);
        again.setVolumeDb(20.0);
        again.setFadeSeconds(0.0);
        again.setTone(-3.0);
        again.setOctave(-5);
        QCOMPARE(again.preset(), 7);
        QCOMPARE(again.volumeDb(), 0.0);
        QCOMPARE(again.fadeSeconds(), 0.1);
        QCOMPARE(again.tone(), 0.0);
        QCOMPARE(again.octave(), -1);
        QCOMPARE(SustainPadsController::presetNames().size(), 8);
        QCOMPARE(SustainPadsController::presetNames().at(0), u"Warm"_s);
    }

    // A song with a key (from its chart) takes the pad to its key when it is
    // picked, as Tonic does; a key tapped on the panel then stays. Not when
    // following is off.
    void sustainPadsFollowTheSongsKey()
    {
        EffectWindows windows(*m_engine, *m_doc);
        SustainPadsController pads(*m_engine, *m_doc, *m_settings, windows);
        QVERIFY(pads.followSongKey());
        QCOMPARE(pads.songKeyIndex(), -1); // the song has none
        QVERIFY(m_doc->pasteChart(0, u"{title: Hymn}\n{key: Eb}\n[Eb]Holy [Bb]holy"_s));
        QCOMPARE(pads.songKey(), u"Eb"_s);
        QCOMPARE(pads.key(), 3); // its key set: there
        pads.pickKey(5);         // F tapped
        QCOMPARE(pads.key(), 5);
        QVERIFY(m_doc->setSongTempo(0, 72.0)); // the song changes otherwise: F stays
        QCOMPARE(pads.key(), 5);

        QVERIFY(m_doc->addSong()); // a song without a key: the pad stays where it is
        QCOMPARE(pads.songKeyIndex(), -1);
        QCOMPARE(pads.key(), 5);
        QVERIFY(m_doc->pasteChart(1, u"{key: A}\n[A]Praise"_s));
        QCOMPARE(pads.key(), 9);

        pads.setFollowSongKey(false);
        QVERIFY(m_doc->addSong());
        QVERIFY(m_doc->pasteChart(2, u"{key: D}\n[D]Glory"_s));
        QCOMPARE(pads.key(), 9); // not followed
        pads.setFollowSongKey(true); // following again: the song's key
        QCOMPARE(pads.key(), 2);
    }

    // A VST loaded into Sustain Pads plays them from then on, and next time
    // with the patch chosen in its window (its settings kept when the app
    // closes); Pads Pro again forgets it. Not loadable: said, Pads Pro stays.
    void sustainPadsPlayAVstLoadedIntoThemNextTimeToo()
    {
        {
            EffectWindows windows(*m_engine, *m_doc);
            SustainPadsController pads(*m_engine, *m_doc, *m_settings, windows);
            QCOMPARE(pads.instrumentId(), QString());
            QCOMPARE(pads.instrumentName(), u"Pads Pro"_s);
            QSignalSpy changed(&pads, &SustainPadsController::instrumentChanged);
            QVERIFY(pads.loadInstrument(u"C:/VST3/Ethereal Pads.vst3"_s, u"Ethereal Pads"_s));
            QCOMPARE(changed.count(), 1);
            QCOMPARE(m_engine->sustainPadsInstrument, u"C:/VST3/Ethereal Pads.vst3"_s);
            QCOMPARE(pads.instrumentName(), u"Ethereal Pads"_s);
            m_engine->sustainPadsState = "patch: Heaven"; // chosen in its window
        } // (the app closes: its settings are kept)
        m_engine->sustainPadsInstrument.clear();
        m_engine->sustainPadsState.clear();
        {
            EffectWindows windows(*m_engine, *m_doc);
            SustainPadsController again(*m_engine, *m_doc, *m_settings, windows);
            QCOMPARE(again.instrumentName(), u"Ethereal Pads"_s);
            QCOMPARE(m_engine->sustainPadsInstrument, u"C:/VST3/Ethereal Pads.vst3"_s);
            QCOMPARE(m_engine->sustainPadsState, QByteArray("patch: Heaven")); // given back
            again.usePadsPro();
            QCOMPARE(m_engine->sustainPadsInstrument, QString());
            QCOMPARE(again.instrumentName(), u"Pads Pro"_s);
        }
        m_engine->sustainPadsInstrument = u"untouched"_s;
        EffectWindows windows(*m_engine, *m_doc);
        SustainPadsController third(*m_engine, *m_doc, *m_settings, windows);
        QCOMPARE(m_engine->sustainPadsInstrument, u"untouched"_s); // Pads Pro: nothing loaded
        QCOMPARE(third.instrumentId(), QString());

        // Refused (an effect, a crash): said why, and Pads Pro still plays.
        m_engine->sustainPadsRefusal = u"Valhalla is an effect, not an instrument"_s;
        QVERIFY(!third.loadInstrument(u"C:/VST3/Valhalla.vst3"_s, u"Valhalla"_s));
        QCOMPARE(third.instrumentId(), QString());
        const Notifications& shown = *m_doc->notifications();
        QVERIFY(shown.count() > 0);
        const QString said = shown.text(shown.count() - 1);
        QVERIFY2(said.contains(u"Valhalla"_s) && said.contains(u"not an instrument"_s), qPrintable(said));
    }

    // A new sound chosen in the VST's window is kept soon after (not only
    // when its window closes or the app ends: a crash would lose it).
    void sustainPadsKeepANewSoundChosenInTheVst()
    {
        EffectWindows windows(*m_engine, *m_doc);
        SustainPadsController pads(*m_engine, *m_doc, *m_settings, windows);
        QVERIFY(pads.loadInstrument(u"C:/VST3/Ethereal Pads.vst3"_s, u"Ethereal Pads"_s));
        QVERIFY(!m_settings->contains(u"sustainPads/instrumentState"_s));
        m_engine->sustainPadsState = "patch: Choir Heaven";
        m_engine->sustainPadsEdited = true; // its window said so
        pads.poll();
        QTRY_COMPARE_WITH_TIMEOUT(QByteArray::fromBase64(m_settings->value(u"sustainPads/instrumentState"_s).toByteArray()),
                                  QByteArray("patch: Choir Heaven"), 4000);
    }

    // Its strip's mute reaches the engine and comes back next time.
    void sustainPadsMuteIsRemembered()
    {
        {
            EffectWindows windows(*m_engine, *m_doc);
            SustainPadsController pads(*m_engine, *m_doc, *m_settings, windows);
            QVERIFY(!pads.muted());
            pads.setMuted(true);
            QVERIFY(m_engine->sustainPads.back().mute);
        }
        EffectWindows windows(*m_engine, *m_doc);
        SustainPadsController again(*m_engine, *m_doc, *m_settings, windows);
        QVERIFY(again.muted());
        QVERIFY(m_engine->sustainPads.back().mute);
    }

    // The meter comes from the engine, with its poll.
    void sustainPadsMeterIsPolled()
    {
        EffectWindows windows(*m_engine, *m_doc);
        SustainPadsController pads(*m_engine, *m_doc, *m_settings, windows);
        QSignalSpy level(&pads, &SustainPadsController::levelChanged);
        pads.poll();
        QCOMPARE(level.count(), 1);
        QCOMPARE(pads.peak(), static_cast<double>(0.3F));
        pads.poll(); // the same: no change to say
        QCOMPARE(level.count(), 1);
    }

    void engineStatusPollsAndForwardsNotices()
    {
        EngineStatus status(*m_engine, *m_doc);
        QSignalSpy polled(&status, &EngineStatus::polled);
        m_engine->pendingNotices.push_back(engine::Notice::info(u"MIDI input connected: Impact GXP61"_s));
        m_engine->pendingNotices.push_back(engine::Notice::error(u"No audio output is available"_s));
        status.poll();
        QCOMPARE(polled.count(), 1);
        QCOMPARE(status.cpuLoad(), 0.25F);
        QVERIFY(status.memoryMb() > 1.0); // this process's working set
        QVERIFY(status.midiActivity());
        QCOMPARE(status.statusText(), u"Spy engine"_s);
        QCOMPARE(status.masterPeak(), 0.4F); // the master strip's meter
        // Every notice is shown, each at its own level: a keyboard plugged in
        // is news, not an error.
        const Notifications& shown = *m_doc->notifications();
        QCOMPARE(shown.rowCount(), 2);
        QCOMPARE(shown.text(0), u"MIDI input connected: Impact GXP61"_s);
        QCOMPARE(shown.level(0), Notifications::Info);
        QCOMPARE(shown.text(1), u"No audio output is available"_s);
        QCOMPARE(shown.level(1), Notifications::Error);
        QCOMPARE(m_doc->lastError(), u"No audio output is available"_s); // the last error, only errors

        // A learned pedal and pad switch songs; the panic button stops the sound.
        QVERIFY(m_doc->addSong());
        const int song = m_doc->songIndex();
        m_engine->pendingActions = {engine::ControlAction::PreviousSong};
        status.poll();
        QCOMPARE(m_doc->songIndex(), song - 1);
        m_engine->pendingActions = {engine::ControlAction::NextSong, engine::ControlAction::Panic};
        status.poll();
        QCOMPARE(m_doc->songIndex(), song);
        QCOMPARE(m_engine->panics, 1);
        const int last = shown.rowCount() - 1;
        QVERIFY(shown.text(last).contains(u"Panic"_s));
        QCOMPARE(shown.level(last), Notifications::Info);

        // A keyboard's patch buttons (Program Change N) pick patch N+1 of the
        // song; one the song does not have is said, and nothing changes.
        QVERIFY(m_doc->addPatch(m_doc->songIndex()));
        QVERIFY(m_doc->addPatch(m_doc->songIndex()));
        m_engine->pendingProgram = 1;
        status.poll();
        QCOMPARE(m_doc->patchIndex(), 1);
        m_engine->pendingProgram = 0;
        status.poll();
        QCOMPARE(m_doc->patchIndex(), 0);
        m_engine->pendingProgram = 9;
        status.poll();
        QCOMPARE(m_doc->patchIndex(), 0);
        QVERIFY2(shown.text(shown.rowCount() - 1).contains(u"Program 10"_s), qPrintable(shown.text(shown.rowCount() - 1)));
        QCOMPARE(shown.level(shown.rowCount() - 1), Notifications::Warning);

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
