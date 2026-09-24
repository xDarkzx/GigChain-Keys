// Which MIDI inputs play: from what is plugged in and what the user chose.
#include "gigchain/engine/MidiSetup.h"

#include <QtTest>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// What Windows lists when an Impact GXP61 is plugged in: the keys on the
// first port, DAW control on the second.
const QStringList kGxp61{u"Impact GXP61 0"_s, u"MIDIIN2 (Impact GXP61) 1"_s};

} // namespace

class TestMidiSetup : public QObject
{
    Q_OBJECT

private slots:
    void firstTimeOnlyTheFirstPortPlays()
    {
        const auto ports = resolveMidiInputs(kGxp61, MidiSetup{});
        QCOMPARE(ports.size(), std::size_t{2});
        QVERIFY(ports[0].enabled);
        QVERIFY(!ports[1].enabled); // two enabled ports would double every note
        QCOMPARE(ports[0].channel, 0);
    }

    void theSavedChoiceIsKept()
    {
        MidiSetup setup;
        setup.configured = true;
        setup.enabled = {u"MIDIIN2 (Impact GXP61) 1"_s};
        setup.channels[u"MIDIIN2 (Impact GXP61) 1"_s] = 10;
        const auto ports = resolveMidiInputs(kGxp61, setup);
        QVERIFY(!ports[0].enabled);
        QVERIFY(ports[1].enabled);
        QCOMPARE(ports[1].channel, 10);
    }

    void aNewPortStaysOff()
    {
        MidiSetup setup;
        setup.configured = true;
        setup.enabled = {u"Impact GXP61 0"_s};
        const auto ports = resolveMidiInputs(QStringList{kGxp61} << u"Some Pad Controller 2"_s, setup);
        QVERIFY(ports[0].enabled);
        QVERIFY(!ports[2].enabled);
    }

    void whenNoSavedInputIsPluggedInTheFirstPortPlays()
    {
        // A different keyboard at the gig: never leave the player without sound.
        MidiSetup setup;
        setup.configured = true;
        setup.enabled = {u"Studio Keyboard 0"_s};
        const auto ports = resolveMidiInputs(kGxp61, setup);
        QVERIFY(ports[0].enabled);
        QVERIFY(!ports[1].enabled);
    }

    void nothingPluggedInIsEmpty()
    {
        QVERIFY(resolveMidiInputs({}, MidiSetup{}).empty());
    }

    void channelFilter()
    {
        const uint8_t noteOnChannel3 = 0x92;
        QVERIFY(passesChannelFilter(noteOnChannel3, 0)); // all channels
        QVERIFY(passesChannelFilter(noteOnChannel3, 3));
        QVERIFY(!passesChannelFilter(noteOnChannel3, 1));
    }
};

QTEST_GUILESS_MAIN(TestMidiSetup)
#include "tst_midi_setup.moc"
