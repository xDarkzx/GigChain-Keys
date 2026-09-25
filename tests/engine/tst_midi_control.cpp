#include "gigchain/engine/MidiControl.h"

#include <QtTest>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

class TestMidiControl : public QObject
{
    Q_OBJECT

private slots:
    void aPedalPressesOnTheWayDownAndIsAlwaysTaken()
    {
        const MidiTrigger pedal{MidiTrigger::ControlChange, 0, 64}; // sustain pedal, channel 1
        const TriggerMatch down = matchTrigger(pedal, 0xB0, 64, 127);
        QVERIFY(down.belongs && down.pressed);
        const TriggerMatch up = matchTrigger(pedal, 0xB0, 64, 0);
        QVERIFY(up.belongs && !up.pressed); // taken too: no stray sustain-off reaches a piano
        QVERIFY(!matchTrigger(pedal, 0xB1, 64, 127).belongs); // another channel
        QVERIFY(!matchTrigger(pedal, 0xB0, 1, 127).belongs);  // the mod wheel is not the pedal
        QVERIFY(!matchTrigger(pedal, 0x90, 64, 127).belongs); // a note is not a CC
    }

    void aPadPressesOnNoteOn()
    {
        const MidiTrigger pad{MidiTrigger::Note, 9, 36}; // drum pad, channel 10
        QVERIFY(matchTrigger(pad, 0x99, 36, 100).pressed);
        const TriggerMatch off = matchTrigger(pad, 0x89, 36, 0);
        QVERIFY(off.belongs && !off.pressed);
        const TriggerMatch zeroVelocity = matchTrigger(pad, 0x99, 36, 0); // a note-off by another name
        QVERIFY(zeroVelocity.belongs && !zeroVelocity.pressed);
        QVERIFY(!matchTrigger(pad, 0x99, 37, 100).belongs);
    }

    void aProgramChangeAlwaysPresses()
    {
        const MidiTrigger program{MidiTrigger::ProgramChange, 0, 4};
        QVERIFY(matchTrigger(program, 0xC0, 4, 0).pressed);
        QVERIFY(!matchTrigger(program, 0xC0, 5, 0).belongs);
    }

    void nothingMatchesAnUnsetTrigger()
    {
        QVERIFY(!matchTrigger(MidiTrigger{}, 0xB0, 64, 127).belongs);
    }

    void learningTakesPressesOnly()
    {
        QCOMPARE(learnable(0xB0, 64, 127), (MidiTrigger{MidiTrigger::ControlChange, 0, 64}));
        QVERIFY(!learnable(0xB0, 64, 0).isSet()); // pedal up
        QCOMPARE(learnable(0x99, 36, 90), (MidiTrigger{MidiTrigger::Note, 9, 36}));
        QVERIFY(!learnable(0x80, 36, 0).isSet());
        QCOMPARE(learnable(0xC2, 7, 0), (MidiTrigger{MidiTrigger::ProgramChange, 2, 7}));
        QVERIFY(!learnable(0xE0, 0, 64).isSet()); // pitch bend
    }

    void triggersPackForSavingAndTheAudioThread()
    {
        for (const MidiTrigger& t : {MidiTrigger{MidiTrigger::ControlChange, 15, 127}, MidiTrigger{MidiTrigger::Note, 0, 0},
                                     MidiTrigger{MidiTrigger::ProgramChange, 3, 99}}) {
            QCOMPARE(MidiTrigger::unpack(t.pack()), t);
        }
        QCOMPARE(MidiTrigger{}.pack(), 0U);
        QVERIFY(!MidiTrigger::unpack(0x00A01234).isSet()); // not a control kind: rejected
    }

    void triggersDescribeThemselves()
    {
        QCOMPARE((MidiTrigger{MidiTrigger::ControlChange, 0, 64}).describe(), u"Pedal/CC 64 (channel 1)"_s);
        QCOMPARE((MidiTrigger{MidiTrigger::Note, 9, 36}).describe(), u"Note C2 (channel 10)"_s);
        QCOMPARE((MidiTrigger{MidiTrigger::ProgramChange, 0, 4}).describe(), u"Program 5 (channel 1)"_s);
        QCOMPARE(MidiTrigger{}.describe(), u"Not set"_s);
    }
};

QTEST_GUILESS_MAIN(TestMidiControl)
#include "tst_midi_control.moc"
