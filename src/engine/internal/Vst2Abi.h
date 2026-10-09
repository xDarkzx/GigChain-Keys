#pragma once

// The VST 2 plugin interface, as a host sees it: the binary layout and the
// numbers a VST2 plugin library uses, written for this app from the
// interface's public description. Steinberg's own VST2 SDK is not used (it
// is no longer licensed, and never could be shipped in GPL software); the
// layout was checked against VeSTige (LMMS's independent header, used by
// Audacity and Ardour). Only what this host needs is here.

#include <array>
#include <cstddef>
#include <cstdint>

namespace gigchain::engine::vst2 {

struct Effect;

// The host's function the plugin calls (the "audioMaster").
using HostCallback = intptr_t (*)(Effect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
// The library's entry point: "VSTPluginMain" (or "main" in older plugins).
using EntryPoint = Effect* (*)(HostCallback host);

// One plugin instance (the plugin owns it). 64-bit layout.
struct Effect
{
    int32_t magic; // kMagic
    intptr_t (*dispatcher)(Effect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    void (*processAccumulating)(Effect* effect, float** inputs, float** outputs, int32_t frames); // old plugins
    void (*setParameter)(Effect* effect, int32_t index, float value);
    float (*getParameter)(Effect* effect, int32_t index);
    int32_t numPrograms;
    int32_t numParams;
    int32_t numInputs;
    int32_t numOutputs;
    int32_t flags;
    intptr_t reserved1;
    intptr_t reserved2;
    int32_t initialDelay;
    int32_t realQualities;
    int32_t offQualities;
    float ioRatio;
    void* object;
    void* user; // the host's own pointer (this host: its Vst2Node)
    int32_t uniqueId;
    int32_t version;
    void (*processReplacing)(Effect* effect, float** inputs, float** outputs, int32_t frames);
    void (*processDoubleReplacing)(Effect* effect, double** inputs, double** outputs, int32_t frames);
    std::array<char, 56> future; // (std::array: the same bytes as a C array)
};

constexpr int32_t fourCc(char a, char b, char c, char d)
{
    return static_cast<int32_t>((static_cast<uint32_t>(static_cast<unsigned char>(a)) << 24U)
                                | (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 16U)
                                | (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 8U)
                                | static_cast<uint32_t>(static_cast<unsigned char>(d)));
}
inline constexpr int32_t kMagic = fourCc('V', 's', 't', 'P');

// Effect::flags.
namespace flag {
inline constexpr int32_t kHasEditor = 1 << 0;
inline constexpr int32_t kCanReplacing = 1 << 4;
inline constexpr int32_t kProgramChunks = 1 << 5;
inline constexpr int32_t kIsSynth = 1 << 8;
} // namespace flag

// What the host asks the plugin (Effect::dispatcher).
namespace op {
inline constexpr int32_t kOpen = 0;
inline constexpr int32_t kClose = 1;
inline constexpr int32_t kGetParamName = 8;
inline constexpr int32_t kSetSampleRate = 10;
inline constexpr int32_t kSetBlockSize = 11;
inline constexpr int32_t kMainsChanged = 12; // value 1: resume, 0: suspend
inline constexpr int32_t kEditGetRect = 13;  // ptr: Rect**
inline constexpr int32_t kEditOpen = 14;     // ptr: the native parent window
inline constexpr int32_t kEditClose = 15;
inline constexpr int32_t kEditIdle = 19;
inline constexpr int32_t kGetChunk = 23;     // index 0: the bank, 1: the program; ptr: void**; returns the size
inline constexpr int32_t kSetChunk = 24;     // index as kGetChunk; value: the size; ptr: the data
inline constexpr int32_t kProcessEvents = 25; // ptr: Events*
inline constexpr int32_t kCanBeAutomated = 26;
inline constexpr int32_t kGetPlugCategory = 35;
inline constexpr int32_t kGetEffectName = 45;
inline constexpr int32_t kGetVendorString = 47;
inline constexpr int32_t kGetProductString = 48;
inline constexpr int32_t kGetVendorVersion = 49;
inline constexpr int32_t kGetVstVersion = 58;
inline constexpr int32_t kShellGetNextPlugin = 70;
inline constexpr int32_t kStartProcess = 71;
inline constexpr int32_t kStopProcess = 72;
} // namespace op

// What the plugin asks the host (HostCallback).
namespace host {
inline constexpr int32_t kAutomate = 0; // a parameter was changed in the plugin's window
inline constexpr int32_t kVersion = 1;
inline constexpr int32_t kCurrentId = 2;
inline constexpr int32_t kIdle = 3;
inline constexpr int32_t kWantMidi = 6;
inline constexpr int32_t kGetTime = 7;
inline constexpr int32_t kProcessEvents = 8;
inline constexpr int32_t kIoChanged = 13;
inline constexpr int32_t kSizeWindow = 15;
inline constexpr int32_t kGetSampleRate = 16;
inline constexpr int32_t kGetBlockSize = 17;
inline constexpr int32_t kGetCurrentProcessLevel = 23;
inline constexpr int32_t kGetVendorString = 32;
inline constexpr int32_t kGetProductString = 33;
inline constexpr int32_t kGetVendorVersion = 34;
inline constexpr int32_t kCanDo = 37;
inline constexpr int32_t kGetLanguage = 38;
inline constexpr int32_t kUpdateDisplay = 42;
inline constexpr int32_t kBeginEdit = 43;
inline constexpr int32_t kEndEdit = 44;
} // namespace host

// The plugin's category (op::kGetPlugCategory).
inline constexpr intptr_t kCategorySynth = 2;
inline constexpr intptr_t kCategoryShell = 10;

// A rectangle in the plugin's window (op::kEditGetRect).
struct Rect
{
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
};

// One MIDI message for the plugin (op::kProcessEvents).
struct MidiEvent
{
    int32_t type;     // kMidiType
    int32_t byteSize; // sizeof(MidiEvent)
    int32_t deltaFrames;
    int32_t flags;
    int32_t noteLength;
    int32_t noteOffset;
    std::array<char, 4> midiData;
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
};
inline constexpr int32_t kMidiType = 1;

// A list of events: `numEvents` pointers follow `reserved` (variable length).
struct Events
{
    int32_t numEvents;
    intptr_t reserved;
    std::array<MidiEvent*, 2> events;
};

// Where the music is (host::kGetTime).
struct TimeInfo
{
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    int32_t timeSigNumerator;
    int32_t timeSigDenominator;
    int32_t smpteOffset;
    int32_t smpteFrameRate;
    int32_t samplesToNextClock;
    int32_t flags;
};
namespace timeFlag {
inline constexpr int32_t kTransportPlaying = 1 << 1;
inline constexpr int32_t kPpqPosValid = 1 << 9;
inline constexpr int32_t kTempoValid = 1 << 10;
inline constexpr int32_t kBarsValid = 1 << 11;
inline constexpr int32_t kTimeSigValid = 1 << 13;
} // namespace timeFlag

// The layout every VST2 plugin was built against (64-bit).
static_assert(sizeof(MidiEvent) == 32 && offsetof(MidiEvent, midiData) == 0x18);
static_assert(sizeof(void*) != 8 || sizeof(Effect) == 0x88 + 56);
static_assert(sizeof(Rect) == 8);
static_assert(sizeof(void*) != 8 || sizeof(TimeInfo) == 88);
static_assert(sizeof(void*) != 8 || offsetof(Effect, user) == 0x68);
static_assert(sizeof(void*) != 8 || offsetof(Effect, processReplacing) == 0x78);
static_assert(sizeof(void*) != 8 || offsetof(Events, events) == 0x10);

} // namespace gigchain::engine::vst2
