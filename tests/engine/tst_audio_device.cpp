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
