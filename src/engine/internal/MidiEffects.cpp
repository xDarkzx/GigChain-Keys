#include "MidiEffects.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gigchain::engine {

namespace {

constexpr double kGate = 0.5; // an arpeggiated note lasts half its step

bool isNoteOn(const MidiEvent& e) noexcept { return (e.status & 0xF0) == 0x90 && e.data2 > 0; }
bool isNoteOff(const MidiEvent& e) noexcept { return (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0); }

} // namespace

void MidiEffects::hold(int note, int velocity) noexcept
{
    m_velocity = velocity;
    const auto held = std::span(m_held).first(m_heldCount);
    if (std::ranges::find(held, note) != held.end() || m_heldCount == m_held.size()) return;
    m_held.at(m_heldCount++) = note;
}

void MidiEffects::release(int note) noexcept
{
    const auto held = std::span(m_held).first(m_heldCount);
    const auto it = std::ranges::find(held, note);
    if (it == held.end()) return;
    std::copy(it + 1, held.end(), it); // the order played is kept
    --m_heldCount;
}

int MidiEffects::arpNote(long long step) const noexcept
{
    if (m_heldCount == 0) return -1;
    std::array<int, kMaxHeld> keys = m_held;
    const auto held = std::span(keys).first(m_heldCount);
    if (m_settings.arpeggio != static_cast<int>(core::ArpPattern::AsPlayed)) std::ranges::sort(held);
    const auto count = static_cast<long long>(m_heldCount);
    const long long length = count * std::clamp(m_settings.arpOctaves, 1, core::kMaxArpOctaves);
    long long i = step % length;
    switch (static_cast<core::ArpPattern>(m_settings.arpeggio)) {
    case core::ArpPattern::Down: i = length - 1 - i; break;
    case core::ArpPattern::UpDown:
        if (length > 1) {
            const long long period = (2 * length) - 2; // up, then down without playing the ends twice
            const long long at = step % period;
            i = at < length ? at : period - at;
        }
        break;
    case core::ArpPattern::Up:
    case core::ArpPattern::AsPlayed:
    case core::ArpPattern::Off: break;
    }
    const int note = keys.at(static_cast<std::size_t>(i % count)) + (12 * static_cast<int>(i / count)); // (held: keys' first ones)
    return note <= 127 ? note : -1;
}

std::size_t MidiEffects::process(std::span<const MidiEvent> in, std::span<MidiEvent> out, int frames, const TimeInfo& time) noexcept
{
    std::size_t written = 0;
    const auto send = [&out, &written](const MidiEvent& e) {
        if (written < out.size()) out.subspan(written++, 1).front() = e;
    };
    const core::ChordIntervals chord = core::chordIntervals(m_settings.chord);
    const double perSample = time.sampleRate > 0.0 ? time.tempo / 60.0 / time.sampleRate : 0.0;
    // No tempo to step on (no device yet): the chord alone.
    const bool arp = m_settings.arpeggio != 0 && perSample > 0.0;
    const double start = time.ppqPosition;
    const double end = start + (frames * perSample);
    const double step = core::arpStepQuarters(m_settings.arpRate);
    const auto quarterAt = [start, perSample](int32_t offset) { return start + (offset * perSample); };
    const auto offsetOf = [start, perSample, frames](double quarter) {
        const double at = perSample > 0.0 ? (quarter - start) / perSample : 0.0;
        return static_cast<int32_t>(std::lround(std::clamp(at, 0.0, static_cast<double>(std::max(frames - 1, 0)))));
    };
    const auto note = [this](int key, int velocity, int32_t offset) {
        return MidiEvent{.status = static_cast<uint8_t>(velocity > 0 ? m_status : (m_status & 0x0F) | 0x80),
                         .data1 = static_cast<uint8_t>(key),
                         .data2 = static_cast<uint8_t>(velocity),
                         .sampleOffset = offset};
    };
    // The song's clock jumped (Play from the top, a part chosen): the
    // arpeggiator carries on from here rather than waiting for its old time.
    if (arp && m_nextStep >= 0.0 && (m_nextStep > end + step || m_nextStep < start - (4.0 * step))) m_nextStep = start;
    if (arp && m_playing >= 0 && (m_offAt > end + step || m_offAt < start - (4.0 * step))) m_offAt = start;
    // The arpeggiator's notes and their ends due before `until` (quarters).
    const auto runSteps = [&](double until) {
        constexpr double kNever = std::numeric_limits<double>::infinity();
        while (true) {
            const double next = m_heldCount > 0 && m_nextStep >= 0.0 ? m_nextStep : kNever;
            const double off = m_playing >= 0 ? m_offAt : kNever;
            const double at = std::min(next, off);
            if (!(at < until)) return;
            if (m_playing >= 0) {
                send(note(m_playing, 0, offsetOf(off < next ? off : next)));
                m_playing = -1;
                if (off < next) continue;
            }
            if (const int key = arpNote(m_step++); key >= 0) {
                send(note(key, m_velocity, offsetOf(next)));
                m_playing = key;
                m_offAt = next + (step * kGate);
            }
            // On the song's grid from here (the first note played at once, on the key).
            m_nextStep = (std::floor((next / step) + 1e-9) + 1.0) * step;
        }
    };

    for (const MidiEvent& e : in) {
        if (arp) runSteps(quarterAt(e.sampleOffset));
        const bool on = isNoteOn(e);
        if (!on && !isNoteOff(e)) {
            send(e); // controllers, pitch bend...: as they came
            continue;
        }
        m_status = static_cast<uint8_t>(0x90 | (e.status & 0x0F));
        for (std::size_t k = 0; k < chord.count; ++k) {
            const int key = e.data1 + chord.steps.at(k);
            if (key < 0 || key > 127) continue;
            if (!arp) {
                MidiEvent played = e;
                played.data1 = static_cast<uint8_t>(key);
                send(played);
            } else if (on) {
                hold(key, e.data2);
            } else {
                release(key);
            }
        }
        if (!arp) continue;
        if (on && m_nextStep < 0.0) { // the first key: it starts now
            m_step = 0;
            m_nextStep = quarterAt(e.sampleOffset);
        }
        if (m_heldCount == 0) { // every key up: it stops
            if (m_playing >= 0) send(note(m_playing, 0, e.sampleOffset));
            m_playing = -1;
            m_nextStep = -1.0;
        }
    }
    if (arp) runSteps(end);
    return written;
}

} // namespace gigchain::engine
