// What a keyboard sends (or a faulty cable, or another app). Whatever bytes:
// parsing, routing to a layer, matching learned buttons and reading knobs
// never crash, and what comes out is valid MIDI (data bytes 0..127, a
// channel message) that stays inside the layer's key and velocity range.
#include "MidiInput.h"
#include "MidiRouter.h"

#include "gigchain/engine/MidiControl.h"
#include "gigchain/engine/MidiSetup.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

using namespace gigchain::engine;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::span<const uint8_t> bytes(data, size);
    if (bytes.size() < 8) return 0;
    // The first bytes pick a layer's settings, as the setlist could hold them.
    const auto setting = [&](std::size_t i, int low, int high) { return low + (bytes[i] % (high - low + 1)); };
    RouteSettings route;
    route.keyLow = setting(0, 0, 127);
    route.keyHigh = setting(1, 0, 127);
    route.transpose = setting(2, -48, 48);
    route.midiChannel = setting(3, 0, 16);
    route.velocityLow = setting(4, 1, 127);
    route.velocityHigh = setting(5, 1, 127);
    const MidiTrigger learned = MidiTrigger::unpack(static_cast<uint32_t>(bytes[6]) << 16 | bytes[7]);
    (void)learned.describe();

    const auto message = bytes.subspan(8);
    for (std::size_t at = 0; at < message.size(); ++at) {
        const auto event = parseMidi(message.subspan(at, std::min<std::size_t>(3, message.size() - at)));
        if (!event) continue;
        if (event->status < 0x80 || event->status >= 0xF0 || event->data1 > 127 || event->data2 > 127) std::abort();
        (void)passesChannelFilter(event->status, route.midiChannel);
        (void)matchTrigger(learned, event->status, event->data1, event->data2);
        (void)learnable(event->status, event->data1, event->data2);
        (void)programOf(event->status, event->data1);
        (void)encoderSteps(SelectorKnob::Relative, event->data2);
        (void)encoderSteps(SelectorKnob::RelativeOffset, event->data2);
        const auto routed = routeEvent(*event, route);
        if (!routed) continue;
        if (routed->data1 > 127 || routed->data2 > 127) std::abort();
        // A key a layer takes is inside its key range (before transposing), and
        // a note it starts inside its velocity range.
        const int type = event->status & 0xF0;
        const bool keyed = type == 0x80 || type == 0x90 || type == 0xA0;
        if (keyed && (event->data1 < route.keyLow || event->data1 > route.keyHigh)) std::abort();
        if (type == 0x90 && event->data2 > 0 && (event->data2 < route.velocityLow || event->data2 > route.velocityHigh)) std::abort();
        if (keyed && routed->data1 != event->data1 + route.transpose) std::abort();
    }
    return 0;
}
