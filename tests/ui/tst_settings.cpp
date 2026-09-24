#include "DocumentController.h"
#include "SettingsController.h"
#include "SpyEngine.h"

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

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<test::SpyEngine> m_engine;
    std::unique_ptr<DocumentController> m_doc;
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
