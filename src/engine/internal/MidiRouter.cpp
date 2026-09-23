#include "MidiRouter.h"

namespace openstage::engine {

std::optional<MidiEvent> routeEvent(const MidiEvent& event, const RouteSettings& route) noexcept
{
    const int type = event.status & 0xF0;
    if (type < 0x80 || type > 0xE0) return std::nullopt; // not a channel voice message

    const int channel = (event.status & 0x0F) + 1;
    if (route.midiChannel != 0 && channel != route.midiChannel) return std::nullopt;

    const bool isKeyed = type == 0x80 || type == 0x90 || type == 0xA0;
    if (!isKeyed) return event;

    if (event.data1 < route.keyLow || event.data1 > route.keyHigh) return std::nullopt;
    const int note = event.data1 + route.transpose;
    if (note < 0 || note > 127) return std::nullopt;

    MidiEvent routed = event;
    routed.data1 = static_cast<uint8_t>(note);
    return routed;
}

} // namespace openstage::engine
