#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

// The app's own instruments' tuning (MainStage's Tuning preference): A4 at
// 440 Hz, moved up to 100 cents either way to play with a piano or a
// recording that is not at 440. Every built-in instrument (Piano Pro, Saw
// Stack Pro, Pads Pro, the SoundFont Player) reads it as it starts a note;
// VST plugins keep their own tuning. Any thread: one lock-free value.
namespace gigchain::engine::tuning {

inline constexpr double kMaxCents = 100.0;

inline std::atomic<double>& centsValue() noexcept
{
    static std::atomic<double> cents{0.0};
    return cents;
}

// -100..100 cents from A4 = 440 Hz (anything else is clamped; not a number: 0).
inline void setCents(double cents) noexcept
{
    centsValue().store(std::isfinite(cents) ? std::clamp(cents, -kMaxCents, kMaxCents) : 0.0, std::memory_order_relaxed);
}

[[nodiscard]] inline double cents() noexcept
{
    return centsValue().load(std::memory_order_relaxed);
}

// What every pitch is multiplied by.
[[nodiscard]] inline double ratio() noexcept
{
    return std::exp2(cents() / 1200.0);
}

// A4 in Hz <-> cents from 440.
[[nodiscard]] inline double centsFor(double a4Hz) noexcept
{
    return a4Hz > 0.0 && std::isfinite(a4Hz) ? 1200.0 * std::log2(a4Hz / 440.0) : 0.0;
}

} // namespace gigchain::engine::tuning
