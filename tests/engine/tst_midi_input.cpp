#include "MidiInput.h"

#include <QtTest>

#include <array>
#include <vector>

using namespace openstage::engine;

class TestMidiInput : public QObject
{
    Q_OBJECT

private slots:
    void parsesChannelMessages()
    {
        const std::vector<unsigned char> noteOn{0x91, 60, 100};
        const auto on = parseMidi(noteOn);
        QVERIFY(on.has_value());
        QCOMPARE(int(on->status), 0x91);
        QCOMPARE(int(on->data1), 60);
        QCOMPARE(int(on->data2), 100);

        const std::vector<unsigned char> program{0xC0, 5}; // two-byte message
        const auto pc = parseMidi(program);
        QVERIFY(pc.has_value());
        QCOMPARE(int(pc->data1), 5);
        QCOMPARE(int(pc->data2), 0);

        const std::vector<unsigned char> sustain{0xB0, 64, 127};
        QVERIFY(parseMidi(sustain).has_value());
    }

    void rejectsIncompleteAndSystemMessages()
    {
        QVERIFY(!parseMidi(std::vector<unsigned char>{}).has_value());
        QVERIFY(!parseMidi(std::vector<unsigned char>{0x90, 60}).has_value());     // truncated
        QVERIFY(!parseMidi(std::vector<unsigned char>{0xF8}).has_value());         // clock
        QVERIFY(!parseMidi(std::vector<unsigned char>{0xF0, 1, 2, 0xF7}).has_value()); // sysex
        QVERIFY(!parseMidi(std::vector<unsigned char>{60, 100, 0}).has_value());   // no status byte
        QVERIFY(!parseMidi(std::vector<unsigned char>{0x90, 200, 100}).has_value()); // data byte > 127
    }

    void listingAndOpeningNeverThrow()
    {
        const QStringList ports = MidiInput::listPorts();
        MidiInput input;
        std::vector<MidiPort> all;
        for (const QString& name : ports) all.push_back(MidiPort{name, true, 0});
        const auto notices = input.openAll(all);
        QCOMPARE(input.openPortNames().size() + static_cast<qsizetype>(notices.size()) >= ports.size(), true);
        std::array<MidiEvent, 16> events{};
        QCOMPARE(input.drain(events), std::size_t{0});
        QVERIFY(!input.takeActivity());
        QCOMPARE(input.takeDropped(), uint64_t{0});
        input.close();
        QVERIFY(input.openPortNames().isEmpty());
    }

    void switchedOffPortsStayClosed()
    {
        const QStringList ports = MidiInput::listPorts();
        if (ports.isEmpty()) QSKIP("No MIDI inputs on this machine");
        MidiInput input;
        (void)input.openAll({MidiPort{ports.first(), false, 0}}); // switched off in Settings
        QVERIFY(!input.openPortNames().contains(ports.first()));
        input.close();
    }
};

QTEST_GUILESS_MAIN(TestMidiInput)
#include "tst_midi_input.moc"
