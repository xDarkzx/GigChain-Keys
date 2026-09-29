#include "ChordFollower.h"

#include "gigchain/core/Limits.h"

#include <algorithm>
#include <bit>
#include <climits>
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

tl::unexpected<core::Error> broken(const QString& what)
{
    return core::fail(core::ErrorCode::InvalidData, QStringLiteral("Chord follow refused: %1").arg(what));
}

// A pitch class (0-11), or -1 where "none" is allowed.
bool pitchOk(int pitchClass, bool noneAllowed)
{
    return (noneAllowed && pitchClass == -1) || (pitchClass >= 0 && pitchClass < 12);
}

} // namespace

core::Result<void> ChordFollower::check(const ChordFollowMap& map)
{
    using core::limits::kMaxFollowSteps;
    using core::limits::kMaxSectionsPerSong;
    const auto chords = static_cast<int>(std::min<std::size_t>(map.steps.size(), INT_MAX));
    const auto sections = static_cast<int>(std::min<std::size_t>(map.sectionStarts.size(), INT_MAX));
    if (chords > kMaxFollowSteps) return broken(QStringLiteral("%1 chords, at most %2").arg(chords).arg(kMaxFollowSteps));
    if (sections > kMaxSectionsPerSong) {
        return broken(QStringLiteral("%1 sections, at most %2").arg(sections).arg(kMaxSectionsPerSong));
    }
    for (int i = 0; i < chords; ++i) {
        const ChordFollowStep& step = map.steps.at(static_cast<std::size_t>(i));
        if (step.family == 0 || (step.family & ~0xFFFU) != 0 || !pitchOk(step.root, false) || !pitchOk(step.bass, true) ||
            !pitchOk(step.third, true) || !pitchOk(step.otherThird, true) || !pitchOk(step.colour, true)) {
            return broken(QStringLiteral("chord %1 has no notes or a note out of the octave").arg(i + 1));
        }
        if (step.section < -1 || step.section >= sections) {
            return broken(
                QStringLiteral("chord %1 is in section %2, but the song has %3").arg(i + 1).arg(step.section + 1).arg(sections));
        }
    }
    for (int s = 0; s < sections; ++s) {
        const int start = map.sectionStarts.at(static_cast<std::size_t>(s));
        if (start == -1) continue;
        if (start < 0 || start >= chords) {
            return broken(
                QStringLiteral("section %1 starts at chord %2, but the song has %3").arg(s + 1).arg(start + 1).arg(chords));
        }
        if (const int owner = map.steps.at(static_cast<std::size_t>(start)).section; owner != s) {
            return broken(QStringLiteral("section %1 starts at chord %2, which is in section %3")
                              .arg(s + 1)
                              .arg(start + 1)
                              .arg(owner + 1));
        }
    }
    if (map.resumeAt < -1 || map.resumeAt >= chords) {
        return broken(QStringLiteral("resume at chord %1, but the song has %2").arg(map.resumeAt + 1).arg(chords));
    }
    return {};
}

void ChordFollower::clear() noexcept
{
    m_velocity.fill(0);
    m_releasedAt.fill(-1);
    m_sustained.fill(false);
    m_sinceChord.reset();
    m_pedal = false;
    m_step = -1;
    m_candidates = 0;
    m_heardAt = kLongAgo;
}

int ChordFollower::sectionInForce(const ChordFollowMap& map) const noexcept
{
    const auto step = static_cast<std::size_t>(std::max(m_step, 0));
    const int section = step < map.steps.size() ? map.steps.at(step).section : -1;
    if (section >= 0) return section;
    // Chords above the first section title belong to the first section (a
    // song without sections has none: every instrument plays).
    const auto first = std::ranges::find_if(map.sectionStarts, [](int start) { return start >= 0; });
    return first != map.sectionStarts.end() ? static_cast<int>(first - map.sectionStarts.begin()) : -1;
}

SectionGate ChordFollower::process(const ChordFollowMap* map, uint64_t generation, std::span<const MidiEvent> events,
                                   int frames, double sampleRate) noexcept
{
    m_handoverCount = 0;
    if (map == nullptr || map->steps.empty()) {
        // Nothing followed: a section or reset asked now is not for a later map.
        m_jumpAsked.store(-1, std::memory_order_relaxed);
        m_resetAsked.store(false, std::memory_order_relaxed);
        m_generation = 0;
        m_now += frames;
        publish(nullptr);
        return {};
    }
    if (generation != m_generation) {
        // A new map starts fresh: a section asked before it (the new song's
        // top, chosen with it) is not a start.
        clear();
        m_jumpAsked.store(-1, std::memory_order_relaxed);
        m_resetAsked.store(false, std::memory_order_relaxed);
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
        m_candidates = 0;
        m_sinceChord.reset();
        m_heardAt = m_now;
        gate.before = sectionInForce(*map);
        gate.after = gate.before;
    }
    const auto memory = static_cast<int64_t>(kMemorySeconds * sampleRate);
    const auto spread = static_cast<int64_t>(kChordSpreadSeconds * sampleRate);
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
            if (hear(*map, static_cast<int>(key), at, memory, spread)) {
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

bool ChordFollower::hear(const ChordFollowMap& map, int key, int64_t now, int64_t memory, int64_t spread) noexcept
{
    const auto count = static_cast<int>(map.steps.size());
    // Sections past 64 cannot be remembered (a song has at most 64).
    const int sections = std::min(static_cast<int>(map.sectionStarts.size()), 64);
    const auto startOf = [&map](int s) { return map.sectionStarts.at(static_cast<std::size_t>(s)); };
    const auto stepAt = [&map](int i) -> const ChordFollowStep& { return map.steps.at(static_cast<std::size_t>(i)); };
    // A remembered section opening is forgotten when a key is played that
    // belongs to neither of its first two chords: the player went elsewhere.
    for (int s = 0; s < sections; ++s) {
        if ((m_candidates >> s & 1U) == 0) continue;
        const int first = startOf(s);
        uint16_t both = stepAt(first).family;
        if (first + 1 < count) both |= stepAt(first + 1).family;
        if (!has(both, key % 12)) m_candidates &= ~(uint64_t{1} << s);
    }
    // What is played: keys down, kept by the pedal, or let go a moment ago;
    // and of those, the ones struck since the last chord was heard.
    uint16_t notes = 0;
    uint16_t struck = 0;
    int lowest = -1;
    for (std::size_t k = 0; k < m_velocity.size(); ++k) {
        const bool held = m_velocity.at(k) != 0 || m_sustained.at(k) ||
                          (m_releasedAt.at(k) >= 0 && now - m_releasedAt.at(k) <= memory);
        if (!held) continue;
        const int pitchClass = static_cast<int>(k % 12);
        notes |= pitchBit(pitchClass);
        // (The rest of the chord just heard, landing a moment after it, is
        // that chord, not a new strike.)
        if (m_sinceChord.test(k) && m_pressedAt.at(k) - m_heardAt >= spread) struck |= pitchBit(pitchClass);
        if (lowest < 0) lowest = pitchClass;
    }
    // Rules 1 and 2: the next chord (the first when not started), its root
    // struck (a melody over a held chord is not the next one). The same
    // chord again (a section ending and the next starting on it) has to be
    // played again, clearly.
    const int next = m_step + 1;
    if (next < count) {
        const ChordFollowStep& step = stepAt(next);
        const bool again = m_step >= 0 && stepAt(m_step).root == step.root && stepAt(m_step).family == step.family &&
                           stepAt(m_step).bass == step.bass;
        if (heardLoosely(step, notes, lowest) && has(struck, step.root) && (!again || heardClearly(step, struck))) {
            m_step = next;
            m_candidates = 0;
            m_heardAt = now;
            return true;
        }
    }
    // Rule 3: a section's first two chords, clearly (the sections after the
    // current one first, then from the top).
    const int current = m_step >= 0 ? stepAt(m_step).section : -1;
    const auto order = [current, sections](int i) { return (current + 1 + i + sections) % sections; };
    for (int i = 0; i < sections; ++i) {
        const int s = order(i);
        if ((m_candidates >> s & 1U) == 0) continue;
        const int second = startOf(s) + 1;
        if (second < count && second != next && stepAt(second).section == s && heardClearly(stepAt(second), notes)) {
            m_step = second;
            m_candidates = 0;
            m_heardAt = now;
            return true;
        }
    }
    // A section's first chord, clearly: remembered, every section it opens.
    // Nothing clear: it stays as it was.
    for (int s = 0; s < sections; ++s) {
        const int first = startOf(s);
        if (first >= 0 && first != next && heardClearly(stepAt(first), notes)) m_candidates |= uint64_t{1} << s;
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
