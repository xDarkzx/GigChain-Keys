#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace gigchain::core {

// How a knob's travel (0-127) is shaped before it is spread over its range
// (ControlMapping::curve).
enum class KnobCurve : int {
    Straight = 0,    // even all the way
    GentleStart = 1, // fine control at the bottom, as an expression or volume pedal wants
    QuickStart = 2,  // most of the change early
};
inline constexpr int kKnobCurveCount = 3;

// The knob's position (0-127) as 0-1, shaped by the curve. Real-time safe.
[[nodiscard]] inline double shapeKnob(int value, int curve) noexcept
{
    const double x = std::clamp(value, 0, 127) / 127.0;
    switch (static_cast<KnobCurve>(curve)) {
    case KnobCurve::GentleStart: return x * x;
    case KnobCurve::QuickStart: return std::sqrt(x);
    case KnobCurve::Straight: break;
    }
    return x;
}

// Pickup (soft takeover) for one knob: a hardware knob that is not where
// the setting is takes it over only once it reaches it (or passes it), so
// nothing jumps when it is first touched on stage. Real-time safe; reset
// (a fresh one) when the sound changes.
struct KnobPickup
{
    static constexpr double kNear = 0.02; // close enough to take over (2% of the range)

    bool caught = false;
    double last = std::numeric_limits<double>::quiet_NaN(); // the knob's previous value, before it took over

    // Whether the knob, now asking for `wanted`, moves the setting at
    // `current` (0-1; below 0 = not known: it takes over at once).
    bool take(double wanted, double current) noexcept
    {
        if (caught || current < 0.0) return caught = true;
        const bool crossed = !std::isnan(last) && ((last - current) * (wanted - current) <= 0.0);
        last = wanted;
        caught = crossed || std::abs(wanted - current) <= kNear;
        return caught;
    }
};

} // namespace gigchain::core
