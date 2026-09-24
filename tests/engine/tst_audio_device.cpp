// Integration tests against the machine's real audio outputs. Tests that need
// a device skip when none exists. Output is silence: nothing audible plays.
#include "AudioDevice.h"

#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>

using namespace openstage;
using namespace openstage::engine;
using namespace Qt::StringLiterals;

class TestAudioDevice : public QObject
{
    Q_OBJECT

private slots:
    void listsOutputsWithOneDefault()
    {
        const auto outputs = AudioDevice::listOutputs();
        if (outputs.empty()) QSKIP("No audio outputs on this machine");
        int wasapiDefaults = 0;
        for (const auto& device : outputs) {
            QVERIFY(!device.name.isEmpty());
            QVERIFY(device.outputChannels >= 2);
            QVERIFY2(!device.sampleRates.empty(), qPrintable(device.name)); // for the Settings rate list
            if (device.preferredSampleRate != 0) {
                QVERIFY(std::find(device.sampleRates.begin(), device.sampleRates.end(), device.preferredSampleRate) !=
                        device.sampleRates.end());
            }
            if (device.api == AudioApi::Wasapi && device.isDefault) ++wasapiDefaults;
        }
        QCOMPARE(wasapiDefaults, 1);
    }

    void defaultOutputRunsTheCallback()
    {
        if (AudioDevice::listOutputs().empty()) QSKIP("No audio outputs on this machine");
        AudioDevice device;
        std::atomic<int> blocks{0};
        std::atomic<bool> sizesOk{true};
        const auto opened = device.open(std::nullopt, 256, [&](AudioBlock out) {
            if (out.frames <= 0 || out.frames > device.maxBlock()) sizesOk = false;
            std::fill_n(out.left, out.frames, 0.0F);
            std::fill_n(out.right, out.frames, 0.0F);
            blocks.fetch_add(1);
        });
        QVERIFY2(opened.has_value(), opened ? "" : qPrintable(opened.error().message));
        QVERIFY(device.isOpen());
        QVERIFY(device.sampleRate() > 0.0);
        QVERIFY(!device.deviceName().isEmpty());
        QVERIFY(device.api() == AudioApi::Wasapi);

        for (int i = 0; i < 50 && blocks.load() < 10; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        QVERIFY2(blocks.load() >= 10, "audio callback did not run");
        QVERIFY(sizesOk.load());
        QVERIFY(device.poll().empty()); // nothing went wrong

        device.close();
        QVERIFY(!device.isOpen());
    }

    void opensAtTheRequestedRateAndPauses()
    {
        const auto outputs = AudioDevice::listOutputs();
        const auto system = std::find_if(outputs.begin(), outputs.end(),
                                         [](const AudioDeviceInfo& d) { return d.api == AudioApi::Wasapi && d.isDefault; });
        if (system == outputs.end()) QSKIP("No default system output");
        // A rate other than the device's own, when it offers one.
        unsigned int rate = system->sampleRates.front();
        for (const unsigned int r : system->sampleRates) {
            if (r != system->preferredSampleRate) rate = r;
        }
        AudioDevice device;
        std::atomic<int> blocks{0};
        const auto opened = device.open(DeviceChoice{AudioApi::Wasapi, system->name}, 256, [&](AudioBlock out) {
            std::fill_n(out.left, out.frames, 0.0F);
            std::fill_n(out.right, out.frames, 0.0F);
            blocks.fetch_add(1);
        }, rate);
        QVERIFY2(opened.has_value(), opened ? "" : qPrintable(opened.error().message));
        QCOMPARE(device.sampleRate(), static_cast<double>(rate));
        QCOMPARE(device.requestedSampleRate(), rate);

        for (int i = 0; i < 50 && blocks.load() < 5; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        QVERIFY(blocks.load() >= 5);

        QVERIFY(device.pause().has_value()); // no callback runs after this returns
        const int paused = blocks.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        QCOMPARE(blocks.load(), paused);

        QVERIFY(device.resume().has_value());
        for (int i = 0; i < 50 && blocks.load() < paused + 5; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        QVERIFY(blocks.load() >= paused + 5);
        QVERIFY(device.poll().empty());
    }

    void unknownDeviceIsAnError()
    {
        AudioDevice device;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"No audio output named \"No Such Device\""_s));
        const auto opened = device.open(DeviceChoice{AudioApi::Wasapi, u"No Such Device"_s}, 256, [](AudioBlock) {});
        QVERIFY(!opened);
        QVERIFY(opened.error().code == core::ErrorCode::InvalidData);
        QVERIFY(!device.isOpen());
    }
};

QTEST_GUILESS_MAIN(TestAudioDevice)
#include "tst_audio_device.moc"
