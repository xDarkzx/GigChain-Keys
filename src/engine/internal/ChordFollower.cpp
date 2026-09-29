#include "ChordFollower.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace gigchain::engine {
namespace {

constexpr uint16_t pitchBit(int pitchClass)
{
    return static_cast<uint16_t>(1U << pitchClass);
}

constexpr bool has(uint16_t notes, int pitchClass)
{
    return pitchClass >= 0 && (notes & pitchBit(pitchClass)) != 0;
}

// Rule 2, generous: the step's root plus any one other note of it, and a
// slash chord's bass at the bottom.
bool heardLoosely(const ChordFollowStep& step, uint16_t notes, int lowest)
{
    if (!has(notes, step.root)) return false;
    if ((notes & step.family & static_cast<uint16_t>(~pitchBit(step.root))) == 0) return false;
    return step.bass < 0 || lowest == step.bass;
}

// Rule 3, strict: the root and its third (a sus chord's sus note, a 5
// chord's fifth), not the other third, and at least three of its notes
// (both of a two-note chord): G-B-D holds Bm's B and D but is not Bm.
bool heardClearly(const ChordFollowStep& step, uint16_t notes)
{
    if (!has(notes, step.root)) return false;
    const bool third = step.third >= 0 ? has(notes, step.third) && !has(notes, step.otherThird) : has(notes, step.colour);
    const int needed = std::min(3, std::popcount(step.family));
    return third && std::popcount(static_cast<uint16_t>(notes & step.family)) >= needed;
}

bool isNoteOn(const MidiEvent& e)
{
    return (e.status & 0xF0) == 0x90 && e.data2 > 0;
}

bool isNoteOff(const MidiEvent& e)
{
    return (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0);
}

} // namespace

void ChordFollower::clear() noexcept
{
    m_velocity.fill(0);
    m_releasedAt.fill(-1);
    m_sustained.fill(false);
    m_sinceChord.reset();
    m_pedal = false;
    m_step = -1;
    m_candidate = -1;
}

int ChordFollower::sectionInForce(const ChordFollowMap& map) const noexcept
{
    const auto step = static_cast<std::size_t>(std::max(m_step, 0));
    return step < map.steps.size() ? map.steps.at(step).section : -1;
}

SectionGate ChordFollower::process(const ChordFollowMap* map, uint64_t generation, std::span<const MidiEvent> events,
                                   int frames, double sampleRate) noexcept
{
    m_handoverCount = 0;
    if (map == nullptr || map->steps.empty()) {
        m_generation = 0;
        m_now += frames;
        publish(nullptr);
        return {};
    }
    if (generation != m_generation) {
        clear();
        m_generation = generation;
        if (map->resumeAt >= 0 && std::cmp_less(map->resumeAt, map->steps.size())) m_step = map->resumeAt;
    }
    if (m_resetAsked.exchange(false, std::memory_order_acq_rel)) clear();
    SectionGate gate;
    gate.before = sectionInForce(*map);
    gate.after = gate.before;
    // A section chosen by hand (the pedal, a click): from the block's start.
    if (const int asked = m_jumpAsked.exchange(-1, std::memory_order_acq_rel);
        asked >= 0 && std::cmp_less(asked, map->sectionStarts.size()) &&
        map->sectionStarts.at(static_cast<std::size_t>(asked)) >= 0) {
        m_step = map->sectionStarts.at(static_cast<std::size_t>(asked));
        m_candidate = -1;
        m_sinceChord.reset();
        gate.before = sectionInForce(*map);
        gate.after = gate.before;
    }
    const auto memory = static_cast<int64_t>(kMemorySeconds * sampleRate);
    for (const MidiEvent& e : events) {
        const auto key = static_cast<std::size_t>(e.data1 & 0x7F);
        const int64_t at = m_now + e.sampleOffset;
        if (isNoteOn(e)) {
            m_velocity.at(key) = e.data2;
            m_status.at(key) = e.status;
            m_pressedAt.at(key) = at;
            m_releasedAt.at(key) = -1;
            m_sustained.at(key) = false;
            m_sinceChord.set(key);
            if (hear(*map, static_cast<int>(key), at, memory)) {
                const int section = sectionInForce(*map);
                if (section != gate.after) {
                    gate.after = section;
                    gate.switchAt = e.sampleOffset;
                    fillHandover(at, e.sampleOffset);
                }
                m_sinceChord.reset();
            }
        } else if (isNoteOff(e)) {
            if (m_velocity.at(key) != 0) {
                m_velocity.at(key) = 0;
                m_releasedAt.at(key) = at;
                m_sustained.at(key) = m_pedal;
            }
        } else if ((e.status & 0xF0) == 0xB0 && e.data1 == 64) {
            m_pedal = e.data2 >= 64;
            if (!m_pedal) m_sustained.fill(false);
        } else if ((e.status & 0xF0) == 0xB0 && (e.data1 == 120 || e.data1 == 123)) {
            // All sound / all notes off: nothing is held any more.
            m_velocity.fill(0);
            m_sustained.fill(false);
            m_releasedAt.fill(-1);
            m_sinceChord.reset();
        }
    }
    m_now += frames;
    publish(map);
    return gate;
}

bool ChordFollower::hear(const ChordFollowMap& map, int key, int64_t now, int64_t memory) noexcept
{
    const auto count = static_cast<int>(map.steps.size());
    // A remembered section opening is forgotten when a key is played that
    // belongs to neither of its first two chords: the player went elsewhere.
    if (m_candidate >= 0) {
        const auto first = static_cast<std::size_t>(map.sectionStarts.at(static_cast<std::size_t>(m_candidate)));
        uint16_t both = map.steps.at(first).family;
        if (first + 1 < map.steps.size()) both |= map.steps.at(first + 1).family;
        if (!has(both, key % 12)) m_candidate = -1;
    }
    // What is played: keys down, kept by the pedal, or let go a moment ago.
    uint16_t notes = 0;
    int lowest = -1;
    for (std::size_t k = 0; k < m_velocity.size(); ++k) {
        const bool held = m_velocity.at(k) != 0 || m_sustained.at(k) ||
                          (m_releasedAt.at(k) >= 0 && now - m_releasedAt.at(k) <= memory);
        if (!held) continue;
        notes |= pitchBit(static_cast<int>(k % 12));
        if (lowest < 0) lowest = static_cast<int>(k % 12);
    }
    // Rules 1 and 2: the next chord (the first when not started).
    const int next = m_step + 1;
    if (next < count && heardLoosely(map.steps.at(static_cast<std::size_t>(next)), notes, lowest)) {
        m_step = next;
        m_candidate = -1;
        return true;
    }
    // Rule 3: a section's first two chords, clearly.
    const auto sections = static_cast<int>(map.sectionStarts.size());
    if (m_candidate >= 0) {
        const int second = map.sectionStarts.at(static_cast<std::size_t>(m_candidate)) + 1;
        if (second < count && second != next && heardClearly(map.steps.at(static_cast<std::size_t>(second)), notes)) {
            m_step = second;
            m_candidate = -1;
            return true;
        }
    }
    // A section's first chord, clearly: remembered (the sections after the
    // current one first, then from the top). Nothing clear: it stays as it was.
    const int current = m_step >= 0 ? map.steps.at(static_cast<std::size_t>(m_step)).section : -1;
    for (int i = 0; i < sections; ++i) {
        const int s = (current + 1 + i) % sections;
        const int first = map.sectionStarts.at(static_cast<std::size_t>(s));
        if (first < 0 || first == next) continue;
        if (heardClearly(map.steps.at(static_cast<std::size_t>(first)), notes)) {
            m_candidate = s;
            break;
        }
    }
    return false;
}

void ChordFollower::fillHandover(int64_t switchTime, int offset) noexcept
{
    m_handoverCount = 0;
    for (std::size_t key = 0; key < m_velocity.size() && m_handoverCount + 2 <= m_handover.size(); ++key) {
        // Only keys down (one let go under the pedal would never get its
        // note-off there), pressed before the switch (those at it already
        // reach the strips coming in).
        if (m_velocity.at(key) == 0 || m_pressedAt.at(key) >= switchTime) continue;
        const auto channel = static_cast<uint8_t>(m_status.at(key) & 0x0F);
        const auto note = static_cast<uint8_t>(key);
        m_handover.at(m_handoverCount++) = MidiEvent{.status = static_cast<uint8_t>(0x90 | channel),
                                                     .data1 = note,
                                                     .data2 = m_velocity.at(key),
                                                     .sampleOffset = offset};
        if (m_sinceChord.test(key)) {
            m_handover.at(m_handoverCount++) =
                MidiEvent{.status = static_cast<uint8_t>(0x80 | channel), .data1 = note, .data2 = 0, .sampleOffset = offset};
        }
    }
}

void ChordFollower::publish(const ChordFollowMap* map) noexcept
{
    const bool active = map != nullptr;
    m_outActive.store(active, std::memory_order_relaxed);
    m_outStarted.store(active && m_step >= 0, std::memory_order_relaxed);
    m_outStep.store(active ? m_step : -1, std::memory_order_relaxed);
    m_outSection.store(active ? sectionInForce(*map) : -1, std::memory_order_relaxed);
}

ChordFollowPosition ChordFollower::position() const noexcept
{
    return ChordFollowPosition{.active = m_outActive.load(std::memory_order_relaxed),
                               .started = m_outStarted.load(std::memory_order_relaxed),
                               .step = m_outStep.load(std::memory_order_relaxed),
                               .section = m_outSection.load(std::memory_order_relaxed)};
}

} // namespace gigchain::engine
