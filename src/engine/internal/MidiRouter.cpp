#include "MidiRouter.h"

#include <QHash>

#include <limits>
#include <mutex>
#include <utility>

namespace gigchain::engine {

uint16_t keyboardSource(const QString& name)
{
    static std::mutex lock;
    static QHash<QString, uint16_t> numbers; // (never forgotten: a number stays its keyboard's)
    const std::scoped_lock held(lock);
    if (const auto found = numbers.constFind(name); found != numbers.cend()) return found.value();
    // (65535 keyboards' names in one run: the last number is shared, never 0, "every keyboard")
    const auto number = static_cast<uint16_t>(std::min<qsizetype>(numbers.size() + 1, std::numeric_limits<uint16_t>::max()));
    numbers.insert(name, number);
    return number;
}

uint16_t routeSourceFor(const QString& keyboard, const QStringList& playing)
{
    return keyboard.isEmpty() || !playing.contains(keyboard) ? uint16_t{0} : keyboardSource(keyboard);
}

std::optional<MidiEvent> routeEvent(const MidiEvent& event, const RouteSettings& route) noexcept
{
    const int type = event.status & 0xF0;
    if (type < 0x80 || type > 0xE0) return std::nullopt; // not a channel voice message
    // Another keyboard than the one it listens to (the app's own notes reach every channel).
    if (route.source != 0 && event.source != 0 && event.source != route.source) return std::nullopt;

    const int channel = (event.status & 0x0F) + 1;
    if (route.midiChannel != 0 && channel != route.midiChannel) return std::nullopt;

    if (route.ignores != 0) {
        using namespace midi_filter;
        const bool ignored = (type == 0xB0 && event.data1 == 64 && (route.ignores & kSustain) != 0)
                             || (type == 0xB0 && event.data1 == 11 && (route.ignores & kExpression) != 0)
                             || (type == 0xB0 && event.data1 == 1 && (route.ignores & kModWheel) != 0)
                             || (type == 0xE0 && (route.ignores & kPitchBend) != 0)
                             || ((type == 0xD0 || type == 0xA0) && (route.ignores & kAftertouch) != 0);
        if (ignored) return std::nullopt;
    }

    const bool isKeyed = type == 0x80 || type == 0x90 || type == 0xA0;
    if (!isKeyed) return event;

    if (std::cmp_less(event.data1, route.keyLow) || std::cmp_greater(event.data1, route.keyHigh)) return std::nullopt;
    // A velocity layer plays only the note-ons in its range; note-offs always
    // pass (one for a note it never started is ignored).
    if (type == 0x90 && event.data2 > 0 &&
        (std::cmp_less(event.data2, route.velocityLow) || std::cmp_greater(event.data2, route.velocityHigh))) {
        return std::nullopt;
    }
    const int note = event.data1 + route.transpose + event.transpose; // (the keys' range is the keys as played)
    if (note < 0 || note > 127) return std::nullopt;

    MidiEvent routed = event;
    routed.data1 = static_cast<uint8_t>(note);
    return routed;
}

} // namespace gigchain::engine
