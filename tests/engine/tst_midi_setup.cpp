// Which MIDI inputs play: from what is plugged in and what the user chose.
#include "gigchain/engine/MidiSetup.h"

#include <QtTest>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// What Windows lists when an Impact GXP61 is plugged in: the keys on the
// first port, DAW control on the second.
QStringList gxp61()
{
    return {u"Impact GXP61 0"_s, u"MIDIIN2 (Impact GXP61) 1"_s};
}

} // namespace

class TestMidiSetup : public QObject
{
    Q_OBJECT

private slots:
    void firstTimeOnlyTheFirstPortPlays()
    {
        const auto ports = resolveMidiInputs(gxp61(), MidiSetup{});
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
        const auto ports = resolveMidiInputs(gxp61(), setup);
        QVERIFY(!ports[0].enabled);
        QVERIFY(ports[1].enabled);
        QCOMPARE(ports[1].channel, 10);
    }

    void aNewPortStaysOff()
    {
        MidiSetup setup;
        setup.configured = true;
        setup.enabled = {u"Impact GXP61 0"_s};
        const auto ports = resolveMidiInputs(gxp61() << u"Some Pad Controller 2"_s, setup);
        QVERIFY(ports[0].enabled);
        QVERIFY(!ports[2].enabled);
    }

    void whenNoSavedInputIsPluggedInTheFirstPortPlays()
    {
        // A different keyboard at the gig: never leave the player without sound.
        MidiSetup setup;
        setup.configured = true;
        setup.enabled = {u"Studio Keyboard 0"_s};
        const auto ports = resolveMidiInputs(gxp61(), setup);
        QVERIFY(ports[0].enabled);
        QVERIFY(!ports[1].enabled);
    }

    void nothingPluggedInIsEmpty()
    {
        QVERIFY(resolveMidiInputs({}, MidiSetup{}).empty());
    }

    // RtMidi numbers each port by its place in Windows' list; plugged in in
    // another order, the same keyboard gets another number. Names leave it out.
    void portNamesLeaveOutThePlaceInTheList()
    {
        QCOMPARE(portNames({u"Impact GXP61 0"_s, u"MIDIIN2 (Impact GXP61) 1"_s}),
                 (QStringList{u"Impact GXP61"_s, u"MIDIIN2 (Impact GXP61)"_s}));
        // Plugged in after another device: the same names.
        QCOMPARE(portNames({u"Pad Controller 0"_s, u"Impact GXP61 1"_s, u"MIDIIN2 (Impact GXP61) 2"_s}),
                 (QStringList{u"Pad Controller"_s, u"Impact GXP61"_s, u"MIDIIN2 (Impact GXP61)"_s}));
        // A name that ends in a number keeps it; only the place goes.
        QCOMPARE(portNames({u"Keystation 49 0"_s}), QStringList{u"Keystation 49"_s});
        // Not numbered by place: kept as it is.
        QCOMPARE(portNames({u"Keystation 49"_s}), QStringList{u"Keystation 49"_s});
        QCOMPARE(portNames({}), QStringList{});
    }

    void twoOfTheSameKeyboardAreToldApart()
    {
        QCOMPARE(portNames({u"Keystation 49 0"_s, u"Keystation 49 1"_s, u"Keystation 49 2"_s}),
                 (QStringList{u"Keystation 49"_s, u"Keystation 49 (2)"_s, u"Keystation 49 (3)"_s}));
    }

    // Settings saved by an earlier version name the ports with their place.
    void earlierSavedNamesLoseTheirPlace()
    {
        MidiSetup saved;
        saved.configured = true;
        saved.enabled = {u"Impact GXP61 0"_s, u"Keystation 49 1"_s, u"Odd Name x"_s};
        saved.channels = {{u"Impact GXP61 0"_s, 2}, {u"MIDIIN2 (Impact GXP61) 1"_s, 10}};
        saved.clockOutput = u"MIDIOUT2 (Impact GXP61) 1"_s;
        saved.followClock = true;
        const MidiSetup now = withoutPortPlaces(saved);
        QCOMPARE(now.enabled, (QStringList{u"Impact GXP61"_s, u"Keystation 49"_s, u"Odd Name x"_s}));
        QCOMPARE(now.channels, (std::map<QString, int>{{u"Impact GXP61"_s, 2}, {u"MIDIIN2 (Impact GXP61)"_s, 10}}));
        QCOMPARE(now.clockOutput, u"MIDIOUT2 (Impact GXP61)"_s);
        QVERIFY(now.configured);
        QVERIFY(now.followClock);
        QCOMPARE(withoutPortPlaces(MidiSetup{}), MidiSetup{});
        saved.off = {u"MIDIIN2 (Impact GXP61) 1"_s};
        saved.controls = {u"Pad Controller 2"_s};
        QCOMPARE(withoutPortPlaces(saved).off, QStringList{u"MIDIIN2 (Impact GXP61)"_s});
        QCOMPARE(withoutPortPlaces(saved).controls, QStringList{u"Pad Controller"_s});
    }

    // The keyboard chosen before, plugged in after another device this time:
    // it still plays, with its channel, and the other device stays off.
    void theChosenKeyboardPlaysWhateverOrderItIsPluggedIn()
    {
        MidiSetup setup;
        setup.configured = true;
        setup.enabled = {u"Impact GXP61"_s};
        setup.channels = {{u"Impact GXP61"_s, 2}};
        const QStringList first = portNames({u"Impact GXP61 0"_s, u"MIDIIN2 (Impact GXP61) 1"_s});
        const QStringList after = portNames({u"Pad Controller 0"_s, u"Impact GXP61 1"_s, u"MIDIIN2 (Impact GXP61) 2"_s});
        const auto alone = resolveMidiInputs(first, setup);
        QVERIFY(alone.at(0).enabled);
        QCOMPARE(alone.at(0).channel, 2);
        const auto ports = resolveMidiInputs(after, setup);
        QVERIFY(!ports.at(0).enabled);
        QVERIFY(ports.at(1).enabled);
        QCOMPARE(ports.at(1).channel, 2);
        QVERIFY(!ports.at(2).enabled);
    }

    // A keyboard's second port (its DAW port: transport buttons, faders on
    // many keyboards) is opened for its buttons and knobs only: it never
    // plays (two playing ports would double notes). The first time, and for
    // a keyboard chosen before this existed.
    void aKeyboardsDawPortIsOpenForItsControls()
    {
        const auto fresh = resolveMidiInputs(portNames(gxp61()), MidiSetup{});
        QVERIFY(fresh.at(0).enabled && !fresh.at(0).controlsOnly);
        QVERIFY(!fresh.at(1).enabled);
        QVERIFY(fresh.at(1).controlsOnly);

        MidiSetup chosen;
        chosen.configured = true;
        chosen.enabled = {u"Impact GXP61"_s};
        const auto ports = resolveMidiInputs(portNames(gxp61()) << u"Pad Controller"_s, chosen);
        QVERIFY(ports.at(1).controlsOnly);
        QVERIFY(!ports.at(2).enabled && !ports.at(2).controlsOnly); // another device: off, as before

        // Switched off in Settings: off.
        chosen.off = {u"MIDIIN2 (Impact GXP61)"_s};
        QVERIFY(!resolveMidiInputs(portNames(gxp61()), chosen).at(1).controlsOnly);
        // A controller chosen for its buttons and knobs only (a nanoKONTROL in DAW mode).
        chosen.controls = {u"Pad Controller"_s};
        const auto pads = resolveMidiInputs(QStringList{u"Impact GXP61"_s, u"Pad Controller"_s}, chosen);
        QVERIFY(!pads.at(1).enabled && pads.at(1).controlsOnly);
    }

    // The same keyboard's ports as each system names them.
    void aKeyboardsPortsAreToldBySystem()
    {
        QCOMPARE(midiDeviceOf(u"MIDIIN2 (Impact GXP61)"_s), u"impact gxp61"_s);               // Windows
        QCOMPARE(midiDeviceOf(u"Impact GXP61"_s), u"impact gxp61"_s);
        QCOMPARE(midiDeviceOf(u"Launchkey MK3 61 LKMK3 DAW In"_s), u"launchkey mk3 61 lkmk3"_s); // Mac
        QCOMPARE(midiDeviceOf(u"Launchkey MK3 61 LKMK3 MIDI In"_s), u"launchkey mk3 61 lkmk3"_s);
        QCOMPARE(midiDeviceOf(u"Impact GXP61:Impact GXP61 MIDI 2 24:1"_s), u"impact gxp61"_s); // Linux (ALSA)
        QCOMPARE(midiDeviceOf(u"Impact GXP61:Impact GXP61 MIDI 1 24:0"_s), u"impact gxp61"_s);
        QVERIFY(midiDeviceOf(u"nanoKONTROL2"_s) != midiDeviceOf(u"nanoKEY2"_s));
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
