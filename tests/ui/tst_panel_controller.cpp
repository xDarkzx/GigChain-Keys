// The panel bar (like a Yamaha's registration and part buttons, a Nord's
// program and section buttons): the song's sounds and the playing sound's
// parts as buttons, each assigned to a pad, button or key on the keyboard
// (ASSIGN), which then picks the sound or turns the part on or off.
#include "DocumentController.h"
#include "PanelController.h"

#include "SpyEngine.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

constexpr engine::MidiTrigger pad(uint8_t number)
{
    return engine::MidiTrigger{.kind = engine::MidiTrigger::Note, .channel = 9, .number = number};
}

} // namespace

class TestPanelController : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"settings.ini"_s), QSettings::IniFormat);
        m_engine = std::make_unique<test::SpyEngine>();
        m_doc = std::make_unique<DocumentController>(*m_engine, *m_settings);
        m_doc->newSetlist();
        QVERIFY(m_doc->addSong());
        // One song, three sounds: Piano, Piano + Strings (two parts), Organ.
        QVERIFY(m_doc->renamePatch(0, 0, u"Piano"_s));
        QVERIFY(m_doc->addPatch(0));
        QVERIFY(m_doc->renamePatch(0, 1, u"Piano + Strings"_s));
        QVERIFY(m_doc->addPatch(0));
        QVERIFY(m_doc->renamePatch(0, 2, u"Organ"_s));
        QVERIFY(m_doc->selectPatch(0, 1));
        QVERIFY(m_doc->addChannel(u"spy/Piano.vst3"_s, u"Piano"_s));
        QVERIFY(m_doc->addChannel(u"spy/Strings.vst3"_s, u"Strings"_s));
        m_panel = std::make_unique<PanelController>(*m_engine, *m_doc);
    }
    void cleanup()
    {
        m_panel.reset();
        m_doc.reset();
        m_engine.reset();
        m_settings.reset();
        m_dir.reset();
    }

    // The bar shows the song's sounds (the one playing lit) and the playing
    // sound's parts (on, or off when muted); clicking picks and toggles.
    void theBarShowsAndPlaysTheSoundsAndParts()
    {
        const QVariantList sounds = m_panel->sounds();
        QCOMPARE(sounds.size(), 3);
        QCOMPARE(sounds.at(1).toMap().value(u"name"_s).toString(), u"Piano + Strings"_s);
        QVERIFY(sounds.at(1).toMap().value(u"active"_s).toBool());
        QVERIFY(!sounds.at(0).toMap().value(u"active"_s).toBool());
        QCOMPARE(m_panel->parts().size(), 2);
        QVERIFY(m_panel->parts().at(1).toMap().value(u"on"_s).toBool());

        m_panel->togglePart(1); // strings out
        QVERIFY(m_doc->currentPatch()->channels.at(1).mute);
        QVERIFY(!m_panel->parts().at(1).toMap().value(u"on"_s).toBool());
        m_panel->togglePart(1); // and back in
        QVERIFY(!m_doc->currentPatch()->channels.at(1).mute);

        m_panel->pickSound(2);
        QCOMPARE(m_doc->cursor().patch, 2);
        QVERIFY(m_panel->sounds().at(2).toMap().value(u"active"_s).toBool());
    }

    // ASSIGN: tap a button, press a pad: that pad does it, in every song,
    // shown under the button; one pad does one job; the engine gets them.
    void aPadIsAssignedByTappingAButtonAndPressingIt()
    {
        m_panel->setAssigning(true);
        m_panel->arm(PanelController::SoundButton, 2);
        QCOMPARE(m_panel->armed(), (QVariantMap{{u"kind"_s, PanelController::SoundButton}, {u"index"_s, 2}}));
        m_panel->poll(); // nothing pressed yet
        QVERIFY(!m_doc->panelControls().sounds.at(2).isSet());
        m_engine->learned = pad(36);
        m_panel->poll();
        QCOMPARE(m_doc->panelControls().sounds.at(2), (core::LearnedControl{.kind = 0x90, .channel = 10, .number = 36}));
        QVERIFY(!m_panel->armed().value(u"kind"_s).isValid() || m_panel->armed().value(u"index"_s).toInt() < 0); // done: tap the next
        QVERIFY(m_panel->assigning());
        QVERIFY(!m_panel->sounds().at(2).toMap().value(u"assigned"_s).toString().isEmpty());
        QCOMPARE(m_engine->panelTriggers.sounds.at(2), pad(36));

        // The same pad for a part: it moves there.
        m_panel->arm(PanelController::PartButton, 0);
        m_engine->learned = pad(36);
        m_panel->poll();
        QVERIFY(!m_doc->panelControls().sounds.at(2).isSet());
        QVERIFY(m_doc->panelControls().parts.at(0).isSet());

        m_panel->clearAssignment(PanelController::PartButton, 0);
        QVERIFY(!m_doc->panelControls().parts.at(0).isSet());
        m_panel->setAssigning(false);
        QVERIFY(m_panel->armed().value(u"index"_s).toInt() < 0);
    }

    // On the keyboard: a sound's pad picks that sound; a part's pad turns
    // it on or off; a sound the song does not have is said, not guessed.
    void thePadsPickSoundsAndTurnPartsOnAndOff()
    {
        m_engine->pendingPanelPresses = {{.kind = engine::PanelPress::Sound, .index = 0}};
        m_panel->poll();
        QCOMPARE(m_doc->cursor().patch, 0);

        QVERIFY(m_doc->selectPatch(0, 1));
        m_engine->pendingPanelPresses = {{.kind = engine::PanelPress::Part, .index = 1}};
        m_panel->poll();
        QVERIFY(m_doc->currentPatch()->channels.at(1).mute);

        m_engine->pendingPanelPresses = {{.kind = engine::PanelPress::Sound, .index = 5}};
        m_panel->poll();
        QCOMPARE(m_doc->cursor().patch, 1); // still the sound playing
    }

    // A split set by playing: press the lowest key or pad, then the highest;
    // the part plays those, from the channel they came on (pads: often 10).
    // The other parts can then be left to the keys (they would hear the pads).
    void aSplitIsSetByPlaying()
    {
        QVERIFY(m_doc->selectPatch(0, 1)); // Piano + Strings
        m_panel->startZoneLearning(1);      // the strings: on the pads
        QVERIFY(m_panel->zoneLearning().value(u"active"_s).toBool());
        QCOMPARE(m_panel->zoneLearning().value(u"step"_s).toInt(), 0);
        m_engine->learned = pad(43);
        m_panel->poll();
        QCOMPARE(m_panel->zoneLearning().value(u"step"_s).toInt(), 1);
        m_engine->learned = pad(36); // the other end (in either order)
        m_panel->poll();
        QVERIFY(!m_panel->zoneLearning().value(u"active"_s).toBool());
        const core::Channel& strings = m_doc->currentPatch()->channels.at(1);
        QCOMPARE(strings.keyLow, 36);
        QCOMPARE(strings.keyHigh, 43);
        QCOMPARE(strings.midiChannel, 10);
        QVERIFY(m_panel->zoneLearning().value(u"othersHearIt"_s).toBool()); // the piano listens to every channel

        m_panel->keepOthersOnTheKeys(1, 1);
        QCOMPARE(m_doc->currentPatch()->channels.at(0).midiChannel, 1);
        QVERIFY(!m_panel->zoneLearning().value(u"othersHearIt"_s).toBool());

        // Cancelled half way: nothing changes.
        m_panel->startZoneLearning(0);
        m_engine->learned = pad(50);
        m_panel->poll();
        m_panel->cancelZoneLearning();
        QCOMPARE(m_doc->currentPatch()->channels.at(0).keyLow, 0);
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<test::SpyEngine> m_engine;
    std::unique_ptr<DocumentController> m_doc;
    std::unique_ptr<PanelController> m_panel;
};

QTEST_GUILESS_MAIN(TestPanelController)
#include "tst_panel_controller.moc"
