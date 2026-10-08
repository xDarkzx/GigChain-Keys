#include "MidiClockOut.h"
#include "MidiInput.h"

#include <QtTest>

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// Replaces one function this program imports from a DLL (its import table
// entry) until destroyed. RtMidi is linked in statically, so its WinMM calls
// go through this program's own import table.
// The import table is Windows' raw PE layout (offsets from the module base,
// unions, a C array): reading it takes casts and pointer arithmetic.
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-union-access, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-non-const-global-variables)
class ImportSwap
{
public:
    ImportSwap(const char* dll, const char* function, void* replacement)
    {
        auto* base = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); d->Name != 0; ++d) {
            if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0) continue;
            const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
            auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
            for (; names->u1.AddressOfData != 0; ++names, ++addresses) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (std::strcmp(byName->Name, function) != 0) continue;
                m_slot = reinterpret_cast<void**>(&addresses->u1.Function);
                m_original = write(replacement);
                return;
            }
        }
    }
    ~ImportSwap()
    {
        if (m_slot != nullptr) write(m_original);
    }
    ImportSwap(const ImportSwap&) = delete;
    ImportSwap& operator=(const ImportSwap&) = delete;
    ImportSwap(ImportSwap&&) = delete;
    ImportSwap& operator=(ImportSwap&&) = delete;

    [[nodiscard]] bool found() const { return m_slot != nullptr; }
    [[nodiscard]] void* original() const { return m_original; }

private:
    void* write(void* value)
    {
        DWORD protection = 0;
        VirtualProtect(static_cast<void*>(m_slot), sizeof(void*), PAGE_READWRITE, &protection);
        void* previous = *m_slot;
        *m_slot = value;
        VirtualProtect(static_cast<void*>(m_slot), sizeof(void*), protection, &protection);
        return previous;
    }

    void** m_slot = nullptr;
    void* m_original = nullptr;
};

using UnprepareFn = MMRESULT(WINAPI*)(HMIDIIN, LPMIDIHDR, UINT);
UnprepareFn realUnprepare = nullptr;

// midiInUnprepareHeader once the keyboard is unplugged: the buffer is let go
// of, and the driver is gone.
MMRESULT WINAPI unpluggedUnprepare(HMIDIIN in, LPMIDIHDR header, UINT size)
{
    (void)realUnprepare(in, header, size);
    return MMSYSERR_NODRIVER;
}

// While alive, WinMM answers as it does once the keyboard is unplugged.
class UnpluggedMidi
{
public:
    UnpluggedMidi() { realUnprepare = reinterpret_cast<UnprepareFn>(m_swap.original()); }
    [[nodiscard]] bool found() const { return m_swap.found(); }

private:
    ImportSwap m_swap{"winmm.dll", "midiInUnprepareHeader", reinterpret_cast<void*>(&unpluggedUnprepare)};
};
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-union-access, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

class TestMidiInput : public QObject
{
    Q_OBJECT

private slots:
    // Unplugged while open: Windows fails to let go of the input's buffers.
    // Closing must not crash (RtMidi 6.0.0 freed them twice: thestk/rtmidi#376),
    // and the port opens again once plugged back in.
    void anUnpluggedKeyboardClosesCleanlyAndOpensAgain()
    {
        const QStringList ports = MidiInput::listPorts();
        if (ports.isEmpty()) QSKIP("No MIDI inputs on this machine");
        std::vector<MidiPort> all;
        std::ranges::transform(ports, std::back_inserter(all), [](const QString& name) {
            return MidiPort{.name = name, .enabled = true, .controlsOnly = false, .channel = 0};
        });
        MidiInput input;
        (void)input.openAll(all);
        const qsizetype opened = input.openPortNames().size();
        if (opened == 0) QSKIP("No MIDI input could be opened (another app may hold them)");
        {
            const UnpluggedMidi unplugged;
            QVERIFY2(unplugged.found(), "midiInUnprepareHeader is not imported from winmm.dll");
            for (qsizetype i = 0; i < opened; ++i) {
                QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"reported: MidiInWinMM::\\w+: error closing"_s));
            }
            input.close();
        }
        QVERIFY(input.openPortNames().isEmpty());

        (void)input.openAll(all); // plugged in again
        QCOMPARE(input.openPortNames().size(), opened);
        input.close();
    }

    void parsesChannelMessages()
    {
        const std::vector<unsigned char> noteOn{0x91, 60, 100};
        const auto on = parseMidi(noteOn);
        if (!on) QFAIL("a note-on was not parsed");
        QCOMPARE(int(on->status), 0x91);
        QCOMPARE(int(on->data1), 60);
        QCOMPARE(int(on->data2), 100);

        const std::vector<unsigned char> program{0xC0, 5}; // two-byte message
        const auto pc = parseMidi(program);
        if (!pc) QFAIL("a program change was not parsed");
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

    // The keyboard's transport buttons: MIDI Start / Continue / Stop and
    // MMC Play / Stop (any device id); other sysex is not one.
    void transportButtonsAreRecognised()
    {
        using V = std::vector<unsigned char>;
        QCOMPARE(MidiInput::transportRequestOf(V{0xFA}), transport::kStart);
        QCOMPARE(MidiInput::transportRequestOf(V{0xFB}), transport::kContinue);
        QCOMPARE(MidiInput::transportRequestOf(V{0xFC}), transport::kStop);
        QCOMPARE(MidiInput::transportRequestOf(V{0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7}), transport::kStart); // MMC Play
        QCOMPARE(MidiInput::transportRequestOf(V{0xF0, 0x7F, 0x10, 0x06, 0x03, 0xF7}), transport::kStart); // deferred play
        QCOMPARE(MidiInput::transportRequestOf(V{0xF0, 0x7F, 0x00, 0x06, 0x01, 0xF7}), transport::kStop);
        QCOMPARE(MidiInput::transportRequestOf(V{0xF0, 0x7F, 0x7F, 0x06, 0x04, 0xF7}), transport::kNextPart);     // fast forward
        QCOMPARE(MidiInput::transportRequestOf(V{0xF0, 0x7F, 0x7F, 0x06, 0x05, 0xF7}), transport::kPreviousPart); // rewind
        QCOMPARE(MidiInput::transportRequestOf(V{0xF0, 0x7F, 0x7F, 0x06, 0x06, 0xF7}), 0U); // record: not ours
        QCOMPARE(MidiInput::transportRequestOf(V{0xF0, 0x43, 0x10, 0x4C, 0x00, 0xF7}), 0U); // a synth's own sysex
        QCOMPARE(MidiInput::transportRequestOf(V{0xF8}), 0U);                             // clock
        QCOMPARE(MidiInput::transportRequestOf(V{0x90, 60, 100}), 0U);
        QCOMPARE(MidiInput::transportRequestOf(V{}), 0U);
    }

    // Mackie Control, the DAW mode of most controller keyboards (Korg
    // nanoKONTROL, M-Audio, Arturia, Novation, Akai...): its transport and
    // navigation buttons, from a keyboard's DAW port. Shift turns ◀◀ ▶▶ into
    // the previous / next song. A release, or any other note, asks nothing.
    void mackieControlButtons()
    {
        const auto press = [](uint8_t note, bool shift = false) { return MidiInput::mackieRequestOf(0x90, note, 0x7F, shift); };
        QCOMPARE(press(0x5E), transport::kContinue);     // Play: from where the song is
        QCOMPARE(press(0x5D), transport::kStop);
        QCOMPARE(press(0x5B), transport::kPreviousPart); // ◀◀
        QCOMPARE(press(0x5C), transport::kNextPart);     // ▶▶
        QCOMPARE(press(0x5B, true), transport::kPreviousSong);
        QCOMPARE(press(0x5C, true), transport::kNextSong);
        QCOMPARE(press(0x56), transport::kLoopPart);     // Cycle
        QCOMPARE(press(0x59), transport::kClick);
        QCOMPARE(press(0x2E), transport::kPreviousSong); // Bank ◀ ▶
        QCOMPARE(press(0x2F), transport::kNextSong);
        QCOMPARE(press(0x30), transport::kPreviousSound); // Channel ◀ ▶
        QCOMPARE(press(0x31), transport::kNextSound);
        QCOMPARE(MidiInput::mackieRequestOf(0x90, 0x5E, 0x00, false), 0U); // released
        QCOMPARE(MidiInput::mackieRequestOf(0x80, 0x5E, 0x40, false), 0U); // a note-off
        QCOMPARE(press(60), 0U);
        QCOMPARE(MidiInput::mackieRequestOf(0xB0, 0x5E, 0x7F, false), 0U); // a controller, not a button
    }

    // The sustain pedal pressed twice within 0.4 s is a double press; slower
    // is not; a held pedal's repeated values are not presses.
    void aQuickDoubleSustainPressIsSeen()
    {
        constexpr int64_t ms = 1'000'000;
        SustainTaps taps;
        QVERIFY(!taps.change(true, 0));
        QVERIFY(!taps.change(true, 50 * ms)); // still down (a half-pedal value): not a press
        QVERIFY(!taps.change(false, 100 * ms));
        QVERIFY(taps.change(true, 300 * ms)); // the second press, 0.3 s after the first
        QVERIFY(!taps.change(false, 350 * ms));
        QVERIFY(!taps.change(true, 500 * ms)); // a third quick press starts afresh
        QVERIFY(!taps.change(false, 600 * ms));
        QVERIFY(!taps.change(true, 1200 * ms)); // 0.7 s later: too slow
        QVERIFY(!taps.change(false, 1300 * ms));
        QVERIFY(taps.change(true, 1500 * ms));
    }

    void listingAndOpeningNeverThrow()
    {
        const QStringList ports = MidiInput::listPorts();
        MidiInput input;
        std::vector<MidiPort> all;
        std::ranges::transform(ports, std::back_inserter(all), [](const QString& name) {
            return MidiPort{.name = name, .enabled = true, .controlsOnly = false, .channel = 0};
        });
        const auto notices = input.openAll(all);
        QCOMPARE(input.openPortNames().size() + static_cast<qsizetype>(notices.size()) >= ports.size(), true);
        std::array<MidiEvent, 16> events{};
        QCOMPARE(input.drain(events), std::size_t{0});
        QVERIFY(!input.takeActivity());
        QCOMPARE(input.takeDropped(), uint64_t{0});
        input.close();
        QVERIFY(input.openPortNames().isEmpty());
    }

    // The engine lists the ports every 2 s for as long as it runs (to notice
    // a keyboard plugged in): listing must not leak (the soak saw handles
    // creep up).
    void listingPortsLeaksNoHandles()
    {
        (void)MidiInput::listPorts(); // first use: the MIDI system loads
        (void)MidiClockOut::listPorts();
        DWORD before = 0;
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &before));
        for (int i = 0; i < 200; ++i) {
            (void)MidiInput::listPorts();
            (void)MidiClockOut::listPorts();
        }
        DWORD after = 0;
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &after));
        QVERIFY2(after <= before + 5, qPrintable(u"handles went from %1 to %2"_s.arg(before).arg(after)));
    }

    void switchedOffPortsStayClosed()
    {
        const QStringList ports = MidiInput::listPorts();
        if (ports.isEmpty()) QSKIP("No MIDI inputs on this machine");
        MidiInput input;
        (void)input.openAll({MidiPort{.name = ports.first(), .enabled = false, .controlsOnly = false, .channel = 0}}); // switched off in Settings
        QVERIFY(!input.openPortNames().contains(ports.first()));
        input.close();
    }
};

QTEST_GUILESS_MAIN(TestMidiInput)
#include "tst_midi_input.moc"
