#include "DocumentController.h"
#include "InputPermission.h"
#include "SettingsController.h"
#include "SettingsMigration.h"

#include "gigchain/core/Branding.h"
#include "SpyEngine.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestSettings : public QObject
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
    }

    void loadShowsWhatIsRunning()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        QCOMPARE(settings.driver(), u"system"_s);
        QCOMPARE(settings.device(), u"Spy Speakers"_s);
        QCOMPARE(settings.sampleRate(), 48000);
        QCOMPARE(settings.bufferFrames(), 256);
        QCOMPARE(settings.devices().size(), 2); // system outputs only; ASIO is listed when chosen
        QCOMPARE(settings.sampleRates(), (QVariantList{44100, 48000, 96000}));
        QVERIFY(std::abs(settings.latencyMs() - 256.0 / 48.0) < 0.01);
        QCOMPARE(settings.midiInputs().size(), 2);
    }

    void choosingAsioListsAsioDevices()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        settings.setDriver(u"asio"_s);
        QCOMPARE(settings.devices(), (QStringList{u"Spy ASIO"_s}));
        QCOMPARE(settings.device(), u"Spy ASIO"_s); // the first one is picked
        QCOMPARE(settings.sampleRates(), (QVariantList{44100, 48000}));
        QCOMPARE(settings.sampleRate(), 48000); // still offered: kept
    }

    void anInputDeviceAndTheMidiClockApplyAndAreRemembered()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        QCOMPARE(settings.inputDevices(), (QStringList{u"Spy Mic"_s}));
        QCOMPARE(settings.midiOutputs(), (QStringList{u"Spy Drum Machine"_s}));
        settings.setInputDevice(u"No Such Mic"_s); // not offered: ignored
        QVERIFY(settings.inputDevice().isEmpty());
        settings.setInputDevice(u"Spy Mic"_s);
        settings.setClockOutput(u"Spy Drum Machine"_s);
        settings.setFollowClock(true);
        QVERIFY(settings.apply());
        QCOMPARE(m_engine->setup.inputDevice, u"Spy Mic"_s);
        QCOMPARE(m_engine->midi.clockOutput, u"Spy Drum Machine"_s);
        QVERIFY(m_engine->midi.followClock);
        const auto options = SettingsController::engineOptions(*m_settings); // next start
        QCOMPARE(options.audio.inputDevice, u"Spy Mic"_s);
        QCOMPARE(options.midi.clockOutput, u"Spy Drum Machine"_s);
        QVERIFY(options.midi.followClock);

        // The inputs share the output's driver: switching to ASIO drops a system input.
        settings.load();
        settings.setDriver(u"asio"_s);
        QVERIFY(settings.inputDevice().isEmpty());
    }

    void theNewPedalActionsAreOffered()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        const QVariantList controls = settings.controls();
        QCOMPARE(controls.size(), engine::kControlActionCount);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::TapTempo)).toMap().value(u"label"_s).toString(),
                 u"Tap tempo"_s);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::PlayBacking)).toMap().value(u"label"_s).toString(),
                 u"Song / backing track: play / stop"_s);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::NextSection)).toMap().value(u"label"_s).toString(),
                 u"Next part of the song (on the next bar)"_s);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::RepeatPart)).toMap().value(u"label"_s).toString(),
                 u"Repeat this part once more"_s);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::HoldPart)).toMap().value(u"label"_s).toString(),
                 u"Hold this part (loops until pressed again)"_s);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::NextPatch)).toMap().value(u"label"_s).toString(),
                 u"Next sound"_s);
        // A keyboard's own Play, Stop, ◀◀ and Click buttons, learned once.
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::PlaySong)).toMap().value(u"label"_s).toString(),
                 u"Play the song"_s);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::StopSong)).toMap().value(u"label"_s).toString(),
                 u"Stop the song"_s);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::PreviousPart)).toMap().value(u"label"_s).toString(),
                 u"Previous part of the song (on the next bar)"_s);
        QCOMPARE(controls.at(static_cast<int>(engine::ControlAction::ToggleClick)).toMap().value(u"label"_s).toString(),
                 u"Click on / off"_s);
        // Learnable like the others, and kept.
        settings.learnControl(static_cast<int>(engine::ControlAction::NextSection));
        QCOMPARE(settings.learning(), static_cast<int>(engine::ControlAction::NextSection));
        const engine::MidiTrigger pedal{.kind = engine::MidiTrigger::ControlChange, .channel = 0, .number = 67};
        m_engine->learned = pedal;
        settings.pollLearning();
        QCOMPARE(settings.learning(), -1);
        QCOMPARE(settings.controls().at(static_cast<int>(engine::ControlAction::NextSection)).toMap().value(u"trigger"_s).toString(),
                 pedal.describe());
    }

    void applyChangesTheEngineAndRemembers()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        settings.setDevice(u"Spy Headphones"_s);
        settings.setBufferFrames(128);
        QVERIFY(settings.apply());
        QCOMPARE(m_engine->setupChanges, 1);
        QCOMPARE(m_engine->setup.device, u"Spy Headphones"_s);
        QCOMPARE(m_engine->setup.bufferFrames, 128u);

        const auto options = SettingsController::engineOptions(*m_settings); // next start
        QCOMPARE(options.audio.device, u"Spy Headphones"_s);
        QCOMPARE(options.audio.bufferFrames, 128u);
        QVERIFY(options.audio.driver == engine::AudioDriver::System);
    }

    void applyingUnchangedAudioLeavesItRunning()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        QVERIFY(settings.apply());
        QCOMPARE(m_engine->setupChanges, 0); // no dropout for nothing
    }

    void aFailedDeviceIsReportedAndNotSaved()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        m_engine->failingDevice = u"Spy Headphones"_s;
        settings.setDevice(u"Spy Headphones"_s);
        QVERIFY(!settings.apply());
        QVERIFY(settings.error().contains(u"Spy Headphones cannot open"_s));
        QVERIFY(m_doc->lastError().contains(u"Spy Headphones cannot open"_s));
        QCOMPARE(settings.device(), u"Spy Headphones"_s); // the dialog keeps the choice to fix
        QVERIFY(SettingsController::engineOptions(*m_settings).audio.device.isEmpty()); // nothing saved
    }

    void onlyTheFirstMidiPortIsOnByDefault()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        const QVariantList inputs = settings.midiInputs();
        QCOMPARE(inputs.size(), 2);
        QVERIFY(inputs[0].toMap().value(u"enabled"_s).toBool());
        QVERIFY(!inputs[1].toMap().value(u"enabled"_s).toBool()); // MIDIIN2 stays off
        QVERIFY(settings.apply());
        QCOMPARE(m_engine->midiChanges, 0); // untouched page: MIDI is not reopened
    }

    void midiChoicesApplyAndAreRemembered()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        settings.setMidiInputEnabled(u"Spy Keys 0"_s, false);
        settings.setMidiInputEnabled(u"MIDIIN2 (Spy Keys) 1"_s, true);
        settings.setMidiInputChannel(u"MIDIIN2 (Spy Keys) 1"_s, 2);
        QCOMPARE(m_engine->midiChanges, 0); // nothing happens before OK
        QVERIFY(settings.apply());
        QCOMPARE(m_engine->midi.enabled, (QStringList{u"MIDIIN2 (Spy Keys) 1"_s}));
        QCOMPARE(m_engine->midi.channels.at(u"MIDIIN2 (Spy Keys) 1"_s), 2);

        const auto options = SettingsController::engineOptions(*m_settings); // next start
        QVERIFY(options.midi.configured);
        QCOMPARE(options.midi.enabled, (QStringList{u"MIDIIN2 (Spy Keys) 1"_s}));
        QCOMPARE(options.midi.channels.at(u"MIDIIN2 (Spy Keys) 1"_s), 2);
    }

    // Each input plays, gives its buttons and knobs only (the keyboard's DAW
    // port, by default), or is off; the choice is kept.
    void midiInputModesApplyAndAreRemembered()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        QCOMPARE(settings.midiInputs().at(0).toMap().value(u"mode"_s).toInt(), 0);
        QCOMPARE(settings.midiInputs().at(1).toMap().value(u"mode"_s).toInt(), 1); // the DAW port: its buttons
        settings.setMidiInputMode(u"MIDIIN2 (Spy Keys) 1"_s, 2);
        QCOMPARE(settings.midiInputs().at(1).toMap().value(u"mode"_s).toInt(), 2);
        QVERIFY(settings.apply());
        QCOMPARE(m_engine->midi.off, QStringList{u"MIDIIN2 (Spy Keys) 1"_s});
        settings.setMidiInputMode(u"MIDIIN2 (Spy Keys) 1"_s, 1);
        QVERIFY(settings.apply());
        QCOMPARE(m_engine->midi.controls, QStringList{u"MIDIIN2 (Spy Keys) 1"_s});
        QVERIFY(m_engine->midi.off.isEmpty());
        const auto options = SettingsController::engineOptions(*m_settings); // next start
        QCOMPARE(options.midi.controls, QStringList{u"MIDIIN2 (Spy Keys) 1"_s});
        settings.resetToDefaults();
        QCOMPARE(settings.midiInputs().at(1).toMap().value(u"mode"_s).toInt(), 1);
    }

    // Saved before port names left out their place in Windows' list
    // ("Impact GXP61 0"): the keyboard chosen then still plays.
    void midiChoicesSavedByAnEarlierVersionStillApply()
    {
        m_settings->setValue(u"midi/configured"_s, true);
        m_settings->setValue(u"midi/enabled"_s, QStringList{u"Impact GXP61 0"_s});
        m_settings->setValue(u"midi/channels"_s, QVariantMap{{u"Impact GXP61 0"_s, 3}});
        m_settings->setValue(u"midi/clockOutput"_s, u"MIDIOUT2 (Impact GXP61) 1"_s);
        auto options = SettingsController::engineOptions(*m_settings);
        QCOMPARE(options.midi.enabled, QStringList{u"Impact GXP61"_s});
        QCOMPARE(options.midi.channels, (std::map<QString, int>{{u"Impact GXP61"_s, 3}}));
        QCOMPARE(options.midi.clockOutput, u"MIDIOUT2 (Impact GXP61)"_s);

        // Only once: a name of today that ends in a number keeps it.
        m_settings->setValue(u"midi/enabled"_s, QStringList{u"Keystation 49"_s});
        options = SettingsController::engineOptions(*m_settings);
        QCOMPARE(options.midi.enabled, QStringList{u"Keystation 49"_s});
    }

    void reopeningTheLastSetlistIsOffUntilChosen()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        QVERIFY(!settings.reopenLastSetlist());
        settings.setReopenLastSetlist(true);
        QVERIFY(!m_settings->value(DocumentController::reopenLastSetlistKey()).toBool()); // nothing before OK
        QVERIFY(settings.apply());
        QVERIFY(m_settings->value(DocumentController::reopenLastSetlistKey()).toBool());
        QCOMPARE(m_engine->setupChanges, 0); // audio untouched

        SettingsController again(*m_engine, *m_doc, *m_settings);
        again.load();
        QVERIFY(again.reopenLastSetlist());
        again.resetToDefaults();
        QVERIFY(!again.reopenLastSetlist());
    }

    void safetyLimiterIsOnAtMinusOneUntilChanged()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        QVERIFY(m_engine->limiterOn); // on from the start
        QCOMPARE(m_engine->limiterCeiling, -1.0);
        settings.load();
        QVERIFY(settings.limiterEnabled());
        QCOMPARE(settings.limiterCeilingDb(), -1.0);

        settings.setLimiterCeilingDb(-3.0);
        settings.setLimiterEnabled(false);
        QCOMPARE(m_engine->limiterCeiling, -1.0); // nothing before OK
        QVERIFY(settings.apply());
        QVERIFY(!m_engine->limiterOn);
        QCOMPARE(m_engine->limiterCeiling, -3.0);

        SettingsController next(*m_engine, *m_doc, *m_settings); // next start
        QVERIFY(!m_engine->limiterOn);
        QCOMPARE(m_engine->limiterCeiling, -3.0);
        next.load();
        next.resetToDefaults();
        QVERIFY(next.limiterEnabled());
        QCOMPARE(next.limiterCeilingDb(), -1.0);
    }

    void pluginsThatCrashedCanBeTriedAgain()
    {
        m_engine->blocked = {u"C:/Plugins/Test Plugin A.vst3"_s};
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        QCOMPARE(settings.blockedPlugins().size(), 1);
        QCOMPARE(settings.blockedPlugins()[0].toMap().value(u"name"_s).toString(), u"Test Plugin A"_s);
        QSignalSpy changed(&settings, &SettingsController::changed);
        settings.unblockPlugin(u"C:/Plugins/Test Plugin A.vst3"_s);
        QVERIFY(m_engine->blocked.isEmpty()); // at once, not on OK
        QVERIFY(settings.blockedPlugins().isEmpty());
        QCOMPARE(changed.count(), 1);
    }

    void aPedalIsLearnedForNextSongAndKept()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        QCOMPARE(settings.controls().size(), engine::kControlActionCount);
        QVERIFY(settings.controls()[0].toMap().value(u"trigger"_s).toString().isEmpty()); // nothing by default

        settings.learnControl(static_cast<int>(engine::ControlAction::NextSong));
        QCOMPARE(settings.learning(), 0);
        settings.pollLearning(); // nothing pressed yet
        QCOMPARE(settings.learning(), 0);
        m_engine->learned = engine::MidiTrigger{engine::MidiTrigger::ControlChange, 0, 64}; // the pedal goes down
        settings.pollLearning();
        QCOMPARE(settings.learning(), -1);
        QCOMPARE(settings.controls()[0].toMap().value(u"trigger"_s).toString(), u"Pedal/CC 64 (channel 1)"_s);
        QVERIFY(!m_engine->triggers[0].isSet()); // nothing before OK

        // The same pedal learned for another action moves there.
        settings.learnControl(static_cast<int>(engine::ControlAction::Panic));
        m_engine->learned = engine::MidiTrigger{engine::MidiTrigger::ControlChange, 0, 64};
        settings.pollLearning();
        QVERIFY(settings.controls()[0].toMap().value(u"trigger"_s).toString().isEmpty());
        settings.learnControl(static_cast<int>(engine::ControlAction::NextSong));
        m_engine->learned = engine::MidiTrigger{engine::MidiTrigger::Note, 9, 36};
        settings.pollLearning();

        QVERIFY(settings.apply());
        QCOMPARE(m_engine->triggers[0], (engine::MidiTrigger{engine::MidiTrigger::Note, 9, 36}));
        QCOMPARE(m_engine->triggers[4], (engine::MidiTrigger{engine::MidiTrigger::ControlChange, 0, 64}));

        m_engine->triggers = {};
        SettingsController next(*m_engine, *m_doc, *m_settings); // next start: working at once
        QCOMPARE(m_engine->triggers[0], (engine::MidiTrigger{engine::MidiTrigger::Note, 9, 36}));
        next.load();
        next.clearControl(0);
        QVERIFY(next.apply());
        QVERIFY(!m_engine->triggers[0].isSet());
    }

    void pluggingInAKeyboardShowsUpWhileOpen()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        m_engine->midiPresent.clear(); // nothing plugged in yet
        settings.load();
        QVERIFY(settings.midiInputs().isEmpty());
        m_engine->midiPresent = {u"Spy Keys 0"_s, u"MIDIIN2 (Spy Keys) 1"_s};
        QSignalSpy changed(&settings, &SettingsController::changed);
        settings.refreshMidi();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(settings.midiInputs().size(), 2);
        QVERIFY(settings.midiInputs()[0].toMap().value(u"enabled"_s).toBool());
        settings.refreshMidi(); // nothing new: no churn
        QCOMPARE(changed.count(), 1);
    }

    void resetGoesBackToSystemDefaults()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        settings.setDevice(u"Spy Headphones"_s);
        settings.setBufferFrames(1024);
        settings.setMidiInputEnabled(u"Spy Keys 0"_s, false);
        settings.setMidiInputEnabled(u"MIDIIN2 (Spy Keys) 1"_s, true);
        settings.resetToDefaults();
        QCOMPARE(settings.driver(), u"system"_s);
        QCOMPARE(settings.device(), u"Spy Speakers"_s); // the Windows default output
        QCOMPARE(settings.bufferFrames(), 256);
        QCOMPARE(settings.midiInputs()[0].toMap().value(u"enabled"_s).toBool(), true);
        QCOMPARE(settings.midiInputs()[1].toMap().value(u"enabled"_s).toBool(), false);
    }

    // The driver box offers what the engine found, named as this system
    // names it (Windows: WASAPI; Linux: PulseAudio; the Mac: Core Audio),
    // system audio first.
    void theDriversAreThisSystems()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        const QVariantList drivers = settings.drivers();
        QCOMPARE(drivers.size(), 2); // the spy has system outputs and an ASIO one
        QCOMPARE(drivers.at(0).toMap().value(u"id"_s).toString(), u"system"_s);
#ifdef Q_OS_WIN
        QCOMPARE(drivers.at(0).toMap().value(u"name"_s).toString(), u"Windows Audio (WASAPI)"_s);
#elif defined(Q_OS_MACOS)
        QCOMPARE(drivers.at(0).toMap().value(u"name"_s).toString(), u"Core Audio"_s);
#else
        QCOMPARE(drivers.at(0).toMap().value(u"name"_s).toString(), u"PulseAudio"_s);
#endif
        QCOMPARE(drivers.at(1).toMap().value(u"id"_s).toString(), u"asio"_s);
        // Every driver's name reads back as itself.
        for (const QString& id : {u"system"_s, u"asio"_s, u"jack"_s, u"alsa"_s}) {
            m_settings->setValue(u"audio/driver"_s, id);
            QCOMPARE(SettingsController::engineOptions(*m_settings).audio.driver,
                     id == u"asio"_s   ? engine::AudioDriver::Asio
                     : id == u"jack"_s ? engine::AudioDriver::Jack
                     : id == u"alsa"_s ? engine::AudioDriver::Alsa
                                       : engine::AudioDriver::System);
        }
    }

    void previousSettingsAreCarriedOver()
    {
        QSettings previous(m_dir->filePath(u"previous.ini"_s), QSettings::IniFormat);
        previous.setValue(u"audio/bufferFrames"_s, 128);
        previous.setValue(u"plugins/favorites"_s, QStringList{u"C:/x/Piano.vst3"_s});
        // The last setlist was renamed to the new extension (the demo was, to
        // the .json form of the time).
        QFile renamed(m_dir->filePath(u"gig"_s + branding::jsonSetlistSuffix()));
        QVERIFY(renamed.open(QIODevice::WriteOnly));
        renamed.close();
        const QString oldExtension = branding::previousFileExtensions().value(0);
        QVERIFY(!oldExtension.isEmpty());
        previous.setValue(u"session/lastFile"_s, m_dir->filePath(u"gig."_s + oldExtension + u".json"_s));

        QVERIFY(carryOverSettings(previous, *m_settings));
        QCOMPARE(m_settings->value(u"audio/bufferFrames"_s).toInt(), 128);
        QCOMPARE(m_settings->value(u"plugins/favorites"_s).toStringList(), (QStringList{u"C:/x/Piano.vst3"_s}));
        QCOMPARE(m_settings->value(u"session/lastFile"_s).toString(), renamed.fileName());

        // Only ever into empty settings: never overwrites what the new name has.
        previous.setValue(u"audio/bufferFrames"_s, 512);
        QVERIFY(!carryOverSettings(previous, *m_settings));
        QCOMPARE(m_settings->value(u"audio/bufferFrames"_s).toInt(), 128);
    }

    // The Perform view's chart size (A−/A+): kept at once, remembered at the
    // next start, and never too small to read or too big to fit.
    void theChartSizeIsRemembered()
    {
        {
            SettingsController settings(*m_engine, *m_doc, *m_settings);
            QCOMPARE(settings.chartTextSize(), 1.7); // the stage size, from the first start
            QSignalSpy changed(&settings, &SettingsController::chartTextSizeChanged);
            settings.setChartTextSize(2.2);
            QCOMPARE(changed.size(), 1);
            QCOMPARE(settings.chartTextSize(), 2.2);
            settings.setChartTextSize(0.2);
            QCOMPARE(settings.chartTextSize(), 1.0);
            settings.setChartTextSize(9.0);
            QCOMPARE(settings.chartTextSize(), 3.0);
            settings.setChartTextSize(2.4);
        }
        SettingsController nextStart(*m_engine, *m_doc, *m_settings);
        QCOMPARE(nextStart.chartTextSize(), 2.4);
    }

    // ---- Hearing the audio inputs (the Mac asks the player; refused, its
    // inputs are silent without any error, so the app says so)

    // An input in use that the system does not let the app hear: said, with
    // where to allow it, and logged.
    void aRefusedInputIsSaid()
    {
        m_engine->setup.inputDevice = u"Spy Mic"_s;
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        int asked = 0;
        settings.setInputPermission(InputPermission([] { return InputPermission::Answer::Denied; },
                                                    [&asked](const auto&) { ++asked; }));
        const int before = m_doc->notifications()->count();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Privacy & Security"_s));
        settings.checkInputPermission();
        QCOMPARE(asked, 0); // refused before: not asked again
        QCOMPARE(m_doc->notifications()->count(), before + 1);
        const QString text = m_doc->notifications()->text(before);
        QVERIFY2(text.contains(u"Microphone"_s) && text.contains(u"Privacy & Security"_s), qPrintable(text));
    }

    // Not asked yet: the player is asked, and once allowed the input is
    // opened again (until then it was silent).
    void anInputIsAskedForAndOpenedOnceAllowed()
    {
        m_engine->setup.inputDevice = u"Spy Mic"_s;
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        std::function<void(InputPermission::Answer)> reply;
        settings.setInputPermission(InputPermission([] { return InputPermission::Answer::Undetermined; },
                                                    [&reply](std::function<void(InputPermission::Answer)> answer) {
                                                        reply = std::move(answer);
                                                    }));
        const int changes = m_engine->setupChanges;
        const int before = m_doc->notifications()->count();
        settings.checkInputPermission();
        QVERIFY(reply); // asked
        QCOMPARE(m_engine->setupChanges, changes);
        reply(InputPermission::Answer::Granted);
        QCOMPARE(m_engine->setupChanges, changes + 1); // opened again
        QCOMPARE(m_engine->setup.inputDevice, u"Spy Mic"_s);
        QCOMPARE(m_doc->notifications()->count(), before);
    }

    void anInputAskedForAndRefusedIsSaid()
    {
        m_engine->setup.inputDevice = u"Spy Mic"_s;
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.setInputPermission(InputPermission([] { return InputPermission::Answer::Undetermined; },
                                                    [](const std::function<void(InputPermission::Answer)>& answer) {
                                                        answer(InputPermission::Answer::Denied);
                                                    }));
        const int before = m_doc->notifications()->count();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Privacy & Security"_s));
        settings.checkInputPermission();
        QCOMPARE(m_doc->notifications()->count(), before + 1);
    }

    // No input in use: nothing to ask.
    void withoutAnInputNothingIsAsked()
    {
        m_engine->setup.inputDevice.clear();
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        int checked = 0;
        settings.setInputPermission(InputPermission(
            [&checked] {
                ++checked;
                return InputPermission::Answer::Denied;
            },
            [](const auto&) {}));
        settings.checkInputPermission();
        QCOMPARE(checked, 0);
    }

    // Choosing an input in Settings asks too.
    void choosingAnInputAsks()
    {
        SettingsController settings(*m_engine, *m_doc, *m_settings);
        settings.load();
        settings.setInputPermission(InputPermission([] { return InputPermission::Answer::Denied; }, [](const auto&) {}));
        settings.setInputDevice(u"Spy Mic"_s);
        const int before = m_doc->notifications()->count();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Privacy & Security"_s));
        QVERIFY(settings.apply());
        QCOMPARE(m_doc->notifications()->count(), before + 1);
    }

    // Where there is no permission to ask for (Windows, Linux), the system
    // grants it (Qt's answer): no question, no notice.
    void theSystemGrantsInputsWhereItDoesNotAsk()
    {
#ifdef Q_OS_MACOS
        QSKIP("The Mac asks the player");
#else
        QCOMPARE(InputPermission().answer(), InputPermission::Answer::Granted);
#endif
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<test::SpyEngine> m_engine;
    std::unique_ptr<DocumentController> m_doc;
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
