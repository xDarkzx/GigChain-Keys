// Chord follow reads charts pasted from web pages and imported from files,
// and follows whatever is played on them. Whatever text: every chord name
// reads as notes (its root among them), the song map stays within the most
// chords a song can be followed through, the map the engine is given passes
// its check, and following it with the same bytes played as keys never
// crashes and stays in range (steps, sections, and the notes handed over).
#include "ChordFollower.h"
#include "MidiEvent.h"

#include "gigchain/core/Chart.h"
#include "gigchain/core/Chords.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/SongMap.h"
#include "gigchain/engine/EngineTypes.h"

#include <QString>
#include <QStringList>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

using namespace gigchain;
using namespace gigchain::engine;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::span<const uint8_t> bytes(data, size);
    const QString text = QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): libFuzzer's bytes

    // Every word as a chord name.
    for (const QString& word : text.split(u' ', Qt::SkipEmptyParts)) {
        const auto shape = core::parseChordName(word);
        if (!shape) continue;
        if (shape->root < 0 || shape->root > 11 || shape->bass < -1 || shape->bass > 11) std::abort();
        if ((shape->family & (1U << shape->root)) == 0 || (shape->family & ~0xFFFU) != 0) std::abort();
    }

    // The chart as a song map, and the map the engine is given.
    const core::Chart chart = core::parseChordPro(text);
    const core::SongMap song = core::buildSongMap(chart);
    if (song.steps.size() > static_cast<std::size_t>(core::limits::kMaxFollowSteps)) std::abort();
    if (song.tooLong && !song.steps.empty()) std::abort();
    if (song.sectionStarts.size() != core::chartSections(chart).size()) std::abort();
    const ChordFollowMap map = followMapOf(song);
    if (!ChordFollower::check(map)) std::abort(); // the app's own maps are never refused
    if (map.steps.empty()) return 0;

    // The same bytes played: each a key down or up (or the pedal), 16 to a
    // 1 ms block at 48 kHz.
    ChordFollower follower;
    std::array<MidiEvent, 16> block{};
    const auto sections = static_cast<int>(map.sectionStarts.size());
    for (std::size_t at = 0; at < bytes.size(); at += block.size()) {
        const auto part = bytes.subspan(at, std::min(block.size(), bytes.size() - at));
        for (std::size_t i = 0; i < part.size(); ++i) {
            const uint8_t b = part[i];
            const auto key = static_cast<uint8_t>(36 + (b & 0x3F));
            block.at(i) = (b & 0xC0) == 0xC0 ? MidiEvent{.status = 0xB0, .data1 = 64, .data2 = static_cast<uint8_t>((b & 1) != 0 ? 127 : 0), .sampleOffset = static_cast<int32_t>(i)}
                          : MidiEvent{.status = static_cast<uint8_t>((b & 0x80) != 0 ? 0x90 : 0x80), .data1 = key, .data2 = 100, .sampleOffset = static_cast<int32_t>(i)};
        }
        if (at % 64 == 48) follower.jumpToSection(part.front() % std::max(sections, 1));
        const SectionGate gate = follower.process(&map, 1, std::span<const MidiEvent>(block.data(), part.size()), 48, 48000.0);
        const ChordFollowPosition where = follower.position();
        if (where.step < -1 || where.step >= static_cast<int>(map.steps.size())) std::abort();
        if (gate.before < -1 || gate.before >= sections || gate.after < -1 || gate.after >= sections) std::abort();
        const auto handover = follower.handover();
        if (handover.size() > static_cast<std::size_t>(kMaxEventsPerBlock)) std::abort();
        for (const MidiEvent& e : handover) {
            if (((e.status & 0xF0) != 0x80 && (e.status & 0xF0) != 0x90) || e.data1 > 127 || e.data2 > 127) std::abort();
        }
    }
    return 0;
}
