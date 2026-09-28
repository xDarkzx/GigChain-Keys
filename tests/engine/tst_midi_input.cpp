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
            return MidiPort{.name = name, .enabled = true, .channel = 0};
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

    void listingAndOpeningNeverThrow()
    {
        const QStringList ports = MidiInput::listPorts();
        MidiInput input;
        std::vector<MidiPort> all;
        std::ranges::transform(ports, std::back_inserter(all), [](const QString& name) {
            return MidiPort{.name = name, .enabled = true, .channel = 0};
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
        (void)input.openAll({MidiPort{ports.first(), false, 0}}); // switched off in Settings
        QVERIFY(!input.openPortNames().contains(ports.first()));
        input.close();
    }
};

QTEST_GUILESS_MAIN(TestMidiInput)
#include "tst_midi_input.moc"
