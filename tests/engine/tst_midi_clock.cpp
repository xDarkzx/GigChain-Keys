#include "EngineLog.h"
#include "MidiClockOut.h"

#include <QElapsedTimer>
#include <QtTest>

#include <chrono>
#include <thread>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

// MIDI clock sent to a real MIDI output. Windows always has its "Microsoft
// GS Wavetable Synth" output, so this runs on any machine with one.
class TestMidiClock : public QObject
{
    Q_OBJECT

private slots:
    // A check that runs every few seconds (listing MIDI ports) and fails the
    // same way each time says so once, not every time; a new failure, or
    // failing again after it worked, is said again.
    void aRepeatedFailureIsSaidOnce()
    {
        gigchain::engine::RepeatedWarning warning;
        QTest::ignoreMessage(QtWarningMsg, "Listing MIDI inputs failed: no ALSA sequencer");
        warning.fail(QStringLiteral("Listing MIDI inputs failed: no ALSA sequencer"));
        warning.fail(QStringLiteral("Listing MIDI inputs failed: no ALSA sequencer")); // not said again
        QTest::ignoreMessage(QtWarningMsg, "Listing MIDI inputs failed: something else");
        warning.fail(QStringLiteral("Listing MIDI inputs failed: something else"));
        warning.ok();
        QTest::ignoreMessage(QtWarningMsg, "Listing MIDI inputs failed: something else");
        warning.fail(QStringLiteral("Listing MIDI inputs failed: something else"));
    }

    void anUnknownOutputIsAnError()
    {
        MidiClockOut clock;
        const auto opened = clock.open(u"No Such MIDI Output"_s);
        QVERIFY(!opened);
        QVERIFY2(opened.error().message.contains(u"No Such MIDI Output"_s), qPrintable(opened.error().message));
        QVERIFY(clock.portName().isEmpty());
    }

    void ticksGoOutAtTheTempo()
    {
        const QStringList outputs = MidiClockOut::listPorts();
        if (outputs.isEmpty()) QSKIP("No MIDI outputs on this machine");
        MidiClockOut clock;
        clock.setTempo(150.0); // 150 BPM: 60 ticks a second
        QVERIFY2(clock.open(outputs.first()).has_value(), qPrintable(outputs.first()));
        QCOMPARE(clock.portName(), outputs.first());
        QElapsedTimer timer;
        timer.start();
        std::this_thread::sleep_for(std::chrono::seconds(1)); // the clock runs in real time: measure a second of it
        const double seconds = static_cast<double>(timer.nsecsElapsed()) / 1e9;
        const auto ticks = static_cast<double>(clock.ticksSent());
        clock.close();
        const double expected = seconds * 150.0 / 60.0 * 24.0;
        QVERIFY2(std::abs(ticks - expected) <= 3.0, qPrintable(u"%1 ticks in %2 s, expected %3"_s.arg(ticks).arg(seconds).arg(expected)));
        QVERIFY(!clock.takeFailed());
        QVERIFY(clock.portName().isEmpty());
    }

    void aTempoChangeChangesThePace()
    {
        const QStringList outputs = MidiClockOut::listPorts();
        if (outputs.isEmpty()) QSKIP("No MIDI outputs on this machine");
        MidiClockOut clock;
        clock.setTempo(60.0);
        QVERIFY(clock.open(outputs.first()).has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const uint64_t slow = clock.ticksSent();
        clock.setTempo(240.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const uint64_t fast = clock.ticksSent() - slow;
        clock.close();
        // Half a second at 60 BPM is 12 ticks; at 240 BPM, 48.
        QVERIFY2(slow >= 10 && slow <= 15, qPrintable(QString::number(slow)));
        QVERIFY2(fast >= 44 && fast <= 51, qPrintable(QString::number(fast)));
    }
};

QTEST_GUILESS_MAIN(TestMidiClock)
#include "tst_midi_clock.moc"
