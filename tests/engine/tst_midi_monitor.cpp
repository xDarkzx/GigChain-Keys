#include "MidiMonitor.h"

#include <QtTest>

#include <array>

using namespace gigchain::engine;

namespace {

MidiEvent message(uint8_t status, uint8_t data1, uint8_t data2)
{
    return MidiEvent{.status = status, .data1 = data1, .data2 = data2, .sampleOffset = 0};
}

} // namespace

class TestMidiMonitor : public QObject
{
    Q_OBJECT

private slots:
    void keysLightWithHowHardTheyWerePlayed()
    {
        MidiMonitor monitor;
        const std::array down{message(0x90, 60, 110), message(0x91, 64, 30)}; // any channel
        monitor.apply(down);
        MidiActivity now = monitor.read();
        QCOMPARE(int(now.velocity.at(60)), 110);
        QCOMPARE(int(now.velocity.at(64)), 30);
        QCOMPARE(int(now.velocity.at(61)), 0);

        const std::array up{message(0x80, 60, 64), message(0x91, 64, 0)}; // a note-on at 0 is a note-off
        monitor.apply(up);
        now = monitor.read();
        QCOMPARE(int(now.velocity.at(60)), 0);
        QCOMPARE(int(now.velocity.at(64)), 0);
    }

    void wheelsAndPedalAreFollowed()
    {
        MidiMonitor monitor;
        QCOMPARE(monitor.read().pitchBend, 8192); // at rest: centred
        const std::array moves{message(0xE0, 0x7F, 0x7F), message(0xB0, 1, 90), message(0xB0, 64, 127)};
        monitor.apply(moves);
        MidiActivity now = monitor.read();
        QCOMPARE(now.pitchBend, 16383); // bent all the way up
        QCOMPARE(now.modWheel, 90);
        QVERIFY(now.sustain);

        const std::array release{message(0xE0, 0, 0x40), message(0xB0, 64, 0)};
        monitor.apply(release);
        now = monitor.read();
        QCOMPARE(now.pitchBend, 8192);
        QVERIFY(!now.sustain);
    }

    void allNotesOffAndPanicLetEveryKeyUp()
    {
        MidiMonitor monitor;
        const std::array held{message(0x90, 48, 100), message(0x90, 52, 100), message(0xB0, 64, 127)};
        monitor.apply(held);
        const std::array allOff{message(0xB0, 123, 0)};
        monitor.apply(allOff);
        QCOMPARE(int(monitor.read().velocity.at(48)), 0);
        QVERIFY(!monitor.read().sustain);

        monitor.apply(held);
        monitor.clear(); // Panic
        QCOMPARE(monitor.read(), MidiActivity{});
    }
};

QTEST_GUILESS_MAIN(TestMidiMonitor)
#include "tst_midi_monitor.moc"
