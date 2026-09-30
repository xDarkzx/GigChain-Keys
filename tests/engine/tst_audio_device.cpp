// Integration tests against the machine's real audio outputs. Tests that need
// a device skip when none exists. Output is silence: nothing audible plays.
#include "AudioDevice.h"
#include "Handles.h"

#include <RtAudio.h>

#include <QProcess>
#include <QStandardPaths>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

class TestAudioDevice : public QObject
{
    Q_OBJECT

    // What WASAPI does when the device is pulled out: its thread reports a
    // driver error (not a "disconnect") and the stream stops.
    static void pullOut(AudioDevice& device)
    {
        QVERIFY(device.m_rtaudio != nullptr);
        device.m_rtaudio->stopStream();
        device.onError(0, "RtApiWasapi::wasapiThread: Unable to retrieve render buffer size.");
    }
    static void waitForBlocks(const std::atomic<int>& blocks, int atLeast)
    {
        for (int i = 0; i < 100 && blocks.load() < atLeast; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    static QString joined(const std::vector<Notice>& notices)
    {
        QStringList texts;
        for (const Notice& n : notices) texts << n.text;
        return texts.join(u" | "_s);
    }

private slots:
    // Unplugged and back (or a driver hiccup): the sound starts again by itself.
    void aStreamThatStopsByItselfStartsAgain()
    {
        if (AudioDevice::listOutputs().empty()) QSKIP("No audio outputs on this machine");
        AudioDevice device;
        std::atomic<int> blocks{0};
        QVERIFY(device.open(std::nullopt, 256, [&](AudioBlock out, const AudioInputs&) {
            std::fill_n(out.left, out.frames, 0.0F);
            std::fill_n(out.right, out.frames, 0.0F);
            blocks.fetch_add(1);
        }).has_value());
        waitForBlocks(blocks, 5);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"reported: RtApiWasapi::wasapiThread"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"stopped working"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"started again|now playing through"_s));
        pullOut(device);
        const int before = blocks.load();
        const std::vector<Notice> notices = device.poll();
        QVERIFY2(!notices.empty(), "the stopped stream went unnoticed");
        QVERIFY2(joined(notices).contains(u"started again"_s) || joined(notices).contains(u"now playing through"_s),
                 qPrintable(joined(notices)));
        QVERIFY(device.isOpen());
        QVERIFY(!device.standingIn());
        waitForBlocks(blocks, before + 5);
        QVERIFY2(blocks.load() >= before + 5, "no sound after starting again");
        QVERIFY(device.poll().empty()); // settled
    }

    // The chosen device missing: the system default plays meanwhile, and the
    // chosen one is taken back when it is plugged in again.
    void aMissingDeviceIsStoodInForAndTakenBack()
    {
        const auto outputs = AudioDevice::listOutputs();
        const auto system = std::ranges::find_if(outputs, [](const AudioDeviceInfo& d) { return d.api == AudioApi::System && d.isDefault; });
        if (system == outputs.end()) QSKIP("No default system output");
        AudioDevice device;
        std::atomic<int> blocks{0};
        const DeviceChoice chosen{.api = AudioApi::System, .name = system->name};
        QVERIFY(device.open(chosen, 256, [&](AudioBlock out, const AudioInputs&) {
            std::fill_n(out.left, out.frames, 0.0F);
            std::fill_n(out.right, out.frames, 0.0F);
            blocks.fetch_add(1);
        }).has_value());
        waitForBlocks(blocks, 5);

        // Pulled out, and not plugged in anywhere (as far as the device can tell).
        device.m_wanted = DeviceChoice{.api = AudioApi::System, .name = u"Unplugged Interface"_s};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"reported: RtApiWasapi::wasapiThread"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"stopped working"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"until it is back"_s));
        pullOut(device);
        std::vector<Notice> notices = device.poll();
        QVERIFY2(joined(notices).contains(u"until it is back"_s), qPrintable(joined(notices)));
        QVERIFY(device.standingIn());
        QCOMPARE(device.deviceName(), system->name); // the stand-in: the system default
        const int standing = blocks.load();
        waitForBlocks(blocks, standing + 5);
        QVERIFY2(blocks.load() >= standing + 5, "no sound while standing in");
        QVERIFY(device.poll().empty()); // still missing: nothing new to say

        // Plugged in again.
        device.m_wanted = chosen;
        device.devicesChanged();
        notices = device.poll();
        QVERIFY2(joined(notices).contains(u"is back"_s), qPrintable(joined(notices)));
        QVERIFY(!device.standingIn());
        QCOMPARE(device.deviceName(), system->name);
        const int back = blocks.load();
        waitForBlocks(blocks, back + 5);
        QVERIFY2(blocks.load() >= back + 5, "no sound after taking the device back");
    }

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
            if (device.api == AudioApi::System && device.isDefault) ++wasapiDefaults;
        }
        QCOMPARE(wasapiDefaults, 1);
    }

    void defaultOutputRunsTheCallback()
    {
        if (AudioDevice::listOutputs().empty()) QSKIP("No audio outputs on this machine");
        AudioDevice device;
        std::atomic<int> blocks{0};
        std::atomic<bool> sizesOk{true};
        const auto opened = device.open(std::nullopt, 256, [&](AudioBlock out, const AudioInputs&) {
            if (out.frames <= 0 || out.frames > device.maxBlock()) sizesOk = false;
            std::fill_n(out.left, out.frames, 0.0F);
            std::fill_n(out.right, out.frames, 0.0F);
            blocks.fetch_add(1);
        });
        QVERIFY2(opened.has_value(), opened ? "" : qPrintable(opened.error().message));
        QVERIFY(device.isOpen());
        QVERIFY(device.sampleRate() > 0.0);
        QVERIFY(!device.deviceName().isEmpty());
        QVERIFY(device.api() == AudioApi::System);

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
                                         [](const AudioDeviceInfo& d) { return d.api == AudioApi::System && d.isDefault; });
        if (system == outputs.end()) QSKIP("No default system output");
        // A rate other than the device's own, when it offers one.
        unsigned int rate = system->sampleRates.front();
        for (const unsigned int r : system->sampleRates) {
            if (r != system->preferredSampleRate) rate = r;
        }
        AudioDevice device;
        std::atomic<int> blocks{0};
        const auto opened = device.open(DeviceChoice{.api = AudioApi::System, .name = system->name}, 256,
                                        [&](AudioBlock out, const AudioInputs&) {
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

    // Panic pauses and resumes the stream; a gig presses it many times. The
    // soak saw File handles grow by about 2 a panic: not from here.
    void pausingAndResumingLeaksNoHandles()
    {
        if (AudioDevice::listOutputs().empty()) QSKIP("No audio outputs on this machine");
        AudioDevice device;
        QVERIFY(device.open(std::nullopt, 256, [](AudioBlock out, const AudioInputs&) {
            std::fill_n(out.left, out.frames, 0.0F);
            std::fill_n(out.right, out.frames, 0.0F);
        }).has_value());
        QVERIFY(device.pause().has_value()); // the first round opens what it keeps
        QVERIFY(device.resume().has_value());
        const auto before = test::handlesByType();
        for (int i = 0; i < 30; ++i) {
            QVERIFY(device.pause().has_value());
            QVERIFY(device.resume().has_value());
        }
        const auto after = test::handlesByType();
        for (const auto& [type, count] : after) {
            const int was = before.contains(type) ? before.at(type) : 0;
            QVERIFY2(count <= was + 2, qPrintable(u"%1 handles went from %2 to %3"_s.arg(type).arg(was).arg(count)));
        }
    }

    // Only this system's drivers, system audio first.
    void thisSystemsDriversAreListed()
    {
        const std::vector<AudioDriver> drivers = systemAudioDrivers();
        QVERIFY(!drivers.empty());
        QCOMPARE(drivers.front(), AudioDriver::System);
        const auto has = [&drivers](AudioDriver driver) { return std::ranges::find(drivers, driver) != drivers.end(); };
#ifdef Q_OS_WIN
        QVERIFY(has(AudioDriver::Asio));
        QVERIFY(!has(AudioDriver::Jack) && !has(AudioDriver::Alsa));
        QCOMPARE(apiName(AudioDriver::System), u"WASAPI"_s);
#elif defined(Q_OS_MACOS)
        QCOMPARE(drivers.size(), std::size_t{1}); // Core Audio only
        QCOMPARE(apiName(AudioDriver::System), u"Core Audio"_s);
#else
        QVERIFY(has(AudioDriver::Jack) && has(AudioDriver::Alsa));
        QVERIFY(!has(AudioDriver::Asio));
        QCOMPARE(apiName(AudioDriver::System), u"PulseAudio"_s);
#endif
    }

#ifdef Q_OS_LINUX
    // The JACK server going away: RtAudio closes the stream itself, on a
    // thread of its own, and reports the disconnect from inside that close.
    // Closing it again meanwhile (as recovering does) must wait for that
    // thread, never close what it is closing. The test holds RtAudio's thread
    // inside the report (it waits on the error lock) to close at that moment.
    void theJackServerGoingAwayIsSurvived()
    {
        if (QStandardPaths::findExecutable(u"jackd"_s).isEmpty()) QSKIP("jackd is not installed");
        // (One name every run: JACK keeps a few servers' names, and one that
        // ended badly keeps its place until the name is used again.)
        const QByteArray server = "gigchain-test";
        qputenv("JACK_DEFAULT_SERVER", server);
        QProcess jackd;
        const auto stop = qScopeGuard([&jackd] {
            qunsetenv("JACK_DEFAULT_SERVER");
            if (jackd.state() == QProcess::NotRunning) return;
            jackd.terminate();
            if (!jackd.waitForFinished(3000)) jackd.kill();
            jackd.waitForFinished(3000);
        });
        jackd.setProcessChannelMode(QProcess::MergedChannels);
        jackd.start(u"jackd"_s, {u"-n"_s, QString::fromLatin1(server), u"-d"_s, u"dummy"_s, u"-r"_s, u"48000"_s,
                                 u"-p"_s, u"256"_s});
        QVERIFY2(jackd.waitForStarted(), qPrintable(jackd.errorString()));
        const auto jackOutput = [] {
            const auto outputs = AudioDevice::listOutputs();
            const auto found = std::ranges::find_if(outputs, [](const AudioDeviceInfo& d) { return d.api == AudioApi::Jack; });
            return found == outputs.end() ? std::optional<QString>{} : std::optional<QString>{found->name};
        };
        std::optional<QString> name;
        for (int i = 0; i < 100 && !(name = jackOutput()); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        QVERIFY2(name.has_value(), jackd.readAll().constData());

        AudioDevice device;
        std::atomic<int> blocks{0};
        const auto opened = device.open(DeviceChoice{.api = AudioApi::Jack, .name = *name}, 256,
                                        [&](AudioBlock out, const AudioInputs&) {
            std::fill_n(out.left, out.frames, 0.0F);
            std::fill_n(out.right, out.frames, 0.0F);
            blocks.fetch_add(1);
        });
        QVERIFY2(opened.has_value(), opened ? "" : qPrintable(opened.error().message));
        waitForBlocks(blocks, 5);
        QVERIFY(blocks.load() >= 5);

        // RtAudio's closing thread stops in its report until released.
        std::atomic<bool> holding{false};
        std::atomic<bool> release{false};
        std::thread holder([&] {
            const std::scoped_lock lock(device.m_errorMutex);
            holding = true;
            while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        });
        while (!holding) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        jackd.terminate();
        for (int i = 0; i < 400 && !device.m_deviceLost; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        QVERIFY2(device.m_deviceLost, "the JACK server's going away was not reported");

        // Closing now, as recovering does; RtAudio's thread goes on shortly.
        std::thread releaser([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            release = true;
        });
        device.close();
        releaser.join();
        holder.join();
        QVERIFY(!device.isOpen());
        QVERIFY(jackd.waitForFinished(5000));

        // And the recovery says what happened (the JACK output is gone).
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"reported:.*Jack server is shutting down"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"stopped working"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"until it is back|No audio output is available"_s));
        const std::vector<Notice> notices = device.poll();
        QVERIFY2(joined(notices).contains(u"until it is back"_s) || joined(notices).contains(u"No audio output"_s),
                 qPrintable(joined(notices)));
    }
#endif

    void unknownDeviceIsAnError()
    {
        AudioDevice device;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"No audio output named \"No Such Device\""_s));
        const auto opened =
            device.open(DeviceChoice{.api = AudioApi::System, .name = u"No Such Device"_s}, 256, [](AudioBlock, const AudioInputs&) {});
        QVERIFY(!opened);
        QVERIFY(opened.error().code == core::ErrorCode::InvalidData);
        QVERIFY(!device.isOpen());
    }
};

QTEST_GUILESS_MAIN(TestAudioDevice)
#include "tst_audio_device.moc"
