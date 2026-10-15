// "Learn this keyboard": a keyboard taught to the app sends its controls as
// the ones the setlist and settings were learned with (on another keyboard),
// so swapping keyboards needs no learning again.
#include "MidiInput.h"
#include "gigchain/engine/MidiSetup.h"

#include <QtTest>

#include <array>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

constexpr MidiTrigger cc(uint8_t channel, uint8_t number) { return MidiTrigger{.kind = MidiTrigger::ControlChange, .channel = channel, .number = number}; }
constexpr MidiTrigger note(uint8_t channel, uint8_t number) { return MidiTrigger{.kind = MidiTrigger::Note, .channel = channel, .number = number}; }

MidiEvent midi(uint8_t status, uint8_t data1, uint8_t data2) { return MidiEvent{.status = status, .data1 = data1, .data2 = data2}; }

// An event as it reaches the app through a rig (`kept` false: dropped).
struct Arrived
{
    bool kept = false;
    MidiEvent event;
};
Arrived through(MidiEvent e, const KeyboardRig& rig)
{
    const bool kept = translateControl(e, rig);
    return Arrived{.kept = kept, .event = e};
}

} // namespace

class TestKeyboardRig : public QObject
{
    Q_OBJECT

private slots:
    // The backup keyboard's knob (CC 21 on channel 2) is the setlist's (CC 74 on channel 1).
    void aTaughtKnobArrivesAsTheLearnedOne()
    {
        const KeyboardRig rig{{.from = cc(1, 21), .to = cc(0, 74)}};
        const Arrived moved = through(midi(0xB1, 21, 90), rig);
        QVERIFY(moved.kept);
        QCOMPARE(moved.event.status, uint8_t{0xB0});
        QCOMPARE(moved.event.data1, uint8_t{74});
        QCOMPARE(moved.event.data2, uint8_t{90}); // where it is: unchanged
    }

    // Anything else passes untouched: the keys, other knobs, other channels.
    void everythingElsePassesAsItIs()
    {
        const KeyboardRig rig{{.from = cc(0, 21), .to = cc(0, 74)}};
        for (const MidiEvent e : {midi(0x90, 60, 100), midi(0x80, 60, 0), midi(0xB0, 22, 5), midi(0xB1, 21, 5), midi(0xE0, 0, 64)}) {
            const Arrived out = through(e, rig);
            QVERIFY(out.kept);
            QCOMPARE(out.event.status, e.status);
            QCOMPARE(out.event.data1, e.data1);
            QCOMPARE(out.event.data2, e.data2);
        }
        QVERIFY(through(midi(0x90, 60, 100), {}).kept); // no rig: as it is
    }

    // A pad (a note) can stand for a learned button that was a CC, and back:
    // pressed is on (127 / the velocity), let go is off (0).
    void aPadAndAButtonStandForEachOther()
    {
        const KeyboardRig padForButton{{.from = note(9, 36), .to = cc(0, 64)}};
        const Arrived down = through(midi(0x99, 36, 100), padForButton);
        QVERIFY(down.kept);
        QCOMPARE(down.event.status, uint8_t{0xB0});
        QCOMPARE(down.event.data1, uint8_t{64});
        QCOMPARE(down.event.data2, uint8_t{127});
        const Arrived up = through(midi(0x89, 36, 0), padForButton);
        QVERIFY(up.kept);
        QCOMPARE(up.event.data2, uint8_t{0});

        const KeyboardRig buttonForPad{{.from = cc(0, 20), .to = note(9, 40)}};
        const Arrived pressed = through(midi(0xB0, 20, 127), buttonForPad);
        QVERIFY(pressed.kept);
        QCOMPARE(pressed.event.status, uint8_t{0x99});
        QCOMPARE(pressed.event.data1, uint8_t{40});
        QVERIFY(pressed.event.data2 > 0);
        const Arrived released = through(midi(0xB0, 20, 0), buttonForPad);
        QVERIFY(released.kept);
        QCOMPARE(released.event.status, uint8_t{0x89}); // a note-off
    }

    // A button standing for a learned program change sends it when pressed, nothing when let go.
    void aButtonCanStandForAProgramChange()
    {
        const KeyboardRig rig{{.from = cc(0, 20), .to = MidiTrigger{.kind = MidiTrigger::ProgramChange, .channel = 0, .number = 5}}};
        const Arrived pressed = through(midi(0xB0, 20, 127), rig);
        QVERIFY(pressed.kept);
        QCOMPARE(pressed.event.status, uint8_t{0xC0});
        QCOMPARE(pressed.event.data1, uint8_t{5});
        QVERIFY(!through(midi(0xB0, 20, 0), rig).kept);
    }

    // The rig is the keyboard's: every port of it gets it (its keys' and its
    // DAW port, where the knobs often are), other keyboards none.
    void everyPortOfTheTaughtKeyboardGetsItsRig()
    {
        MidiSetup setup;
        setup.configured = true;
        setup.enabled = {u"Impact GXP61"_s, u"Keystation 49"_s};
        setup.rigs[midiDeviceOf(u"Impact GXP61"_s)] = {{.from = cc(0, 21), .to = cc(0, 74)}};
        const std::vector<MidiPort> ports =
            resolveMidiInputs({u"Impact GXP61"_s, u"MIDIIN2 (Impact GXP61)"_s, u"Keystation 49"_s}, setup);
        QCOMPARE(ports.size(), std::size_t{3});
        QCOMPARE(ports.at(0).rig.size(), std::size_t{1});
        QCOMPARE(ports.at(1).rig, ports.at(0).rig); // its DAW port
        QVERIFY(ports.at(2).rig.empty());
    }
};

QTEST_GUILESS_MAIN(TestKeyboardRig)
#include "tst_keyboard_rig.moc"
