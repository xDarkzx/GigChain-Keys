#include "LoopStation.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace gigchain::engine {
namespace {

// Heard in the mix.
bool sounds(LoopState s)
{
    return s == LoopState::Playing || s == LoopState::OverdubArmed || s == LoopState::Overdubbing ||
           s == LoopState::StopArmed;
}

// Recording the base (the loop's first pass).
bool recordsBase(LoopState s)
{
    return s == LoopState::Recording || s == LoopState::Closing;
}

} // namespace

int64_t LoopGrid::next(int64_t sample) const
{
    if (!valid()) return sample;
    auto k = static_cast<int64_t>(std::ceil((static_cast<double>(sample - origin) / unit) - 1e-9));
    int64_t line = origin + std::llround(static_cast<double>(k) * unit);
    while (line < sample) line = origin + std::llround(static_cast<double>(++k) * unit);
    return line;
}

int64_t LoopGrid::previous(int64_t sample) const
{
    if (!valid()) return sample;
    auto k = static_cast<int64_t>(std::floor((static_cast<double>(sample - origin) / unit) + 1e-9));
    int64_t line = origin + std::llround(static_cast<double>(k) * unit);
    while (line > sample) line = origin + std::llround(static_cast<double>(--k) * unit);
    return line;
}

// ---------------------------------------------------------------- main thread

void LoopStation::setData(int slot, std::shared_ptr<LoopData> buffers)
{
    m_slots.at(static_cast<std::size_t>(slot)).data.publish(std::move(buffers));
}

const LoopData* LoopStation::data(int slot) const
{
    return m_slots.at(static_cast<std::size_t>(slot)).data.current();
}

bool LoopStation::post(int slot, LoopCommand command)
{
    Commands& queue = m_slots.at(static_cast<std::size_t>(slot)).commands;
    const uint32_t head = queue.head.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1) % Commands::kSize;
    if (next == queue.tail.load(std::memory_order_acquire)) return false;
    queue.items.at(head) = command;
    queue.head.store(next, std::memory_order_release);
    return true;
}

bool LoopStation::pending(int slot) const
{
    const Commands& queue = m_slots.at(static_cast<std::size_t>(slot)).commands;
    return queue.head.load(std::memory_order_acquire) != queue.tail.load(std::memory_order_acquire);
}

LoopReading LoopStation::read(int slot) const
{
    const Slot& s = m_slots.at(static_cast<std::size_t>(slot));
    return LoopReading{.state = s.outState.load(std::memory_order_acquire),
                       .length = s.outLength.load(std::memory_order_relaxed),
                       .position = s.outPosition.load(std::memory_order_relaxed),
                       .layers = s.outLayers.load(std::memory_order_relaxed),
                       .wait = s.outWait.load(std::memory_order_relaxed),
                       .take = s.outTake.load(std::memory_order_relaxed),
                       .tail = s.outTail.load(std::memory_order_relaxed)};
}

uint32_t LoopStation::dirtyLayers(int slot) const
{
    return m_slots.at(static_cast<std::size_t>(slot)).outDirty.load(std::memory_order_acquire);
}

bool LoopStation::takeNeedsLayers(int slot)
{
    return m_slots.at(static_cast<std::size_t>(slot)).needsLayers.exchange(false, std::memory_order_acq_rel);
}

bool LoopStation::takeFull(int slot)
{
    return m_slots.at(static_cast<std::size_t>(slot)).full.exchange(false, std::memory_order_acq_rel);
}

bool LoopStation::takeNoLayerLeft(int slot)
{
    return m_slots.at(static_cast<std::size_t>(slot)).noLayerLeft.exchange(false, std::memory_order_acq_rel);
}

bool LoopStation::takeFault(int slot)
{
    return m_slots.at(static_cast<std::size_t>(slot)).fault.exchange(false, std::memory_order_acq_rel);
}

int64_t LoopStation::tailFadeFrames(const LoopGrid& lines, int64_t take) noexcept
{
    if (take <= 1) return 0;
    const int64_t fade = lines.valid() ? std::llround(lines.unit / 8.0) : take / 16;
    return std::clamp<int64_t>(fade, 1, take / 2);
}

LoopGrid LoopStation::freeGrid() const
{
    return LoopGrid{.origin = m_freeOrigin.load(std::memory_order_relaxed), .unit = m_freeUnit.load(std::memory_order_relaxed)};
}

void LoopStation::collectGarbage()
{
    for (Slot& slot : m_slots) slot.data.collectGarbage();
}

// ---------------------------------------------------------------- audio thread

LoopGrid LoopStation::gridFor(const LoopGrid& bars) const noexcept
{
    return m_sync.load(std::memory_order_relaxed) ? bars : freeGrid();
}

bool LoopStation::layersReady(const Slot& slot) noexcept
{
    return slot.current != nullptr && !slot.current->layers.empty() &&
           std::ranges::all_of(slot.current->layers, [&slot](const auto& layer) {
               return layer && layer->frames() >= slot.length;
           });
}

bool LoopStation::canLayer(const Slot& slot) noexcept
{
    return layersReady(slot) && std::cmp_less(slot.layers, slot.current->layers.size()) &&
           (slot.dirty & (1U << static_cast<unsigned>(slot.layers))) == 0;
}

int64_t LoopStation::capacity(const Slot& slot) noexcept
{
    return slot.current != nullptr && slot.current->base ? slot.current->base->frames() : 0;
}

void LoopStation::closeTake(Slot& slot, int64_t take, int64_t overlap, int repeats, const LoopGrid& lines) noexcept
{
    overlap = take > 0 ? std::clamp<int64_t>(overlap, 0, take - 1) : 0;
    slot.take = take;
    slot.length = take * std::max(repeats, 1);
    slot.written = take;
    slot.repeats = 1;
    // What rang on past the end: what was played after it (a press a
    // little late; a quarter of the take at most: past that it is another
    // part, not a spill) and a little more to fade out over. Never more
    // than half the take, nor than the room left.
    const int64_t fade = tailFadeFrames(lines, take);
    const int64_t room = std::max<int64_t>(capacity(slot) - take, 0);
    slot.tailTarget = std::min({std::min(overlap, take / 4) + fade, take / 2, room});
    slot.tailWritten = std::min(overlap, slot.tailTarget); // (already recorded)
    slot.tailFade = std::min(fade, slot.tailTarget);
    // It plays on from where the recording is now: the take's second time
    // through (in a loop of one take, its start again).
    slot.position = slot.length > 0 ? (take + overlap) % slot.length : 0;
    slot.played = overlap;
    slot.layersPending = slot.length > 0;
}

bool LoopStation::fits(Slot& slot) noexcept
{
    const LoopData* data = slot.current;
    const auto bad = [&slot, data] {
        if (slot.faultData != data) slot.fault.store(true, std::memory_order_release); // (once for these buffers)
        slot.faultData = data;
        return false;
    };
    if (data == nullptr || !data->base) return slot.length <= 0;
    if (slot.length <= 0) return true; // recording: the plan keeps within the base
    if (slot.take <= 0 || slot.length % slot.take != 0 || data->base->frames() < slot.take + slot.tailTarget) return bad();
    const auto used = std::min<std::size_t>(data->layers.size(), static_cast<std::size_t>(slot.layers) + 1);
    for (std::size_t i = 0; i < used; ++i) {
        if (!data->layers.at(i) || data->layers.at(i)->frames() < slot.length) return bad();
    }
    return true;
}

void LoopStation::apply(Slot& slot, LoopCommand command, int64_t blockStart, const LoopGrid& lines, int target) noexcept
{
    // The next grid line (at once without a grid: a free first loop).
    const int64_t line = lines.valid() ? lines.next(blockStart) : blockStart;
    const auto schedule = [&slot](LoopState now, LoopState then, int64_t at) {
        slot.state = now;
        slot.target = then;
        slot.switchAt = at;
    };
    using S = LoopState;
    // A layer being recorded is dropped: its buffer holds some of it now.
    const auto dropLayer = [&slot] {
        if (slot.state == S::Overdubbing) slot.dirty |= 1U << static_cast<unsigned>(slot.layers);
        slot.overdubDone = 0;
        slot.commitOnSwitch = false;
    };
    // Stopped: what rang on past the take is what has been recorded of it.
    const auto endTail = [&slot] {
        slot.tailTarget = std::min(slot.tailTarget, slot.tailWritten);
        slot.tailFade = std::min(slot.tailFade, slot.tailTarget);
        slot.played = 0; // (it starts again after silence: no spill over its start)
    };
    // Ends a recording on the nearest bar: a little late, on the bar just
    // gone (what was played after it rings on over the start, and the loop
    // goes on in time from there); a little early, on the coming one.
    const auto close = [&slot, &lines, &schedule, blockStart, line, target] {
        // A set length stopped early: the take is the most bars (to the
        // nearest) that fill it evenly, and plays over and over to fill it.
        if (lines.valid() && target > 0) {
            const auto recorded = static_cast<int>(std::lround(static_cast<double>(blockStart - slot.recordStart) / lines.unit));
            int bars = std::clamp(recorded, 1, target);
            while (target % bars != 0) --bars;
            const int64_t end = slot.recordStart + std::llround(bars * lines.unit);
            if (end > blockStart) {
                slot.repeats = target / bars;
                schedule(S::Closing, S::Playing, end); // just ahead: it closes there
                return;
            }
            const int64_t take = end - slot.recordStart;
            closeTake(slot, take, slot.written - take, target / bars, lines);
            schedule(S::Playing, S::Playing, -1);
            return;
        }
        if (lines.valid()) {
            const int64_t gone = lines.previous(blockStart);
            if (gone > slot.recordStart && blockStart - gone < line - blockStart) {
                const int64_t take = gone - slot.recordStart;
                closeTake(slot, take, slot.written - take, 1, lines);
                schedule(S::Playing, S::Playing, -1);
                return;
            }
        }
        slot.repeats = 1;
        schedule(S::Closing, S::Playing, line);
    };
    switch (command) {
    case LoopCommand::Record:
        switch (slot.state) {
        case S::Empty:
            if (capacity(slot) == 0) return; // no buffer given: nothing to record into
            slot.written = 0;
            slot.layers = 0;
            slot.take = 0;
            slot.repeats = 1;
            slot.tailTarget = slot.tailWritten = slot.tailFade = slot.played = 0;
            slot.layersPending = false;
            schedule(S::Armed, S::Recording, line);
            return;
        case S::Armed: schedule(S::Empty, S::Empty, -1); return; // changed my mind
        case S::Recording: close(); return;
        case S::Playing:
            // Every layer used (8, or fewer for a long loop: they take memory).
            if (slot.layers >= kMaxLayers || (layersReady(slot) && std::cmp_greater_equal(slot.layers, slot.current->layers.size()))) {
                slot.noLayerLeft.store(true, std::memory_order_release);
                return;
            }
            // (Waits for its buffer when it is not there or not silent yet.)
            schedule(S::OverdubArmed, S::Overdubbing, canLayer(slot) ? line : -1);
            return;
        case S::OverdubArmed: schedule(S::Playing, S::Playing, -1); return;
        case S::Overdubbing:
            schedule(S::Overdubbing, S::Playing, line);
            slot.commitOnSwitch = true;
            return;
        case S::Stopped: schedule(S::StartArmed, S::Playing, line); return;
        case S::Closing:
        case S::StartArmed:
        case S::StopArmed: return;
        }
        return;
    case LoopCommand::PlayStop:
        switch (slot.state) {
        case S::Empty:
        case S::Closing: return;
        case S::Armed: schedule(S::Empty, S::Empty, -1); return;
        case S::Recording: close(); return;
        // Stopping is at once; starting waits for the bar (to stay in time).
        case S::Playing:
        case S::StopArmed:
        case S::OverdubArmed:
            dropLayer();
            endTail();
            slot.position = 0;
            schedule(S::Stopped, S::Stopped, -1);
            return;
        case S::Overdubbing:
            // The layer so far is kept (the rest of its pass stays silent).
            ++slot.layers;
            slot.overdubDone = 0;
            slot.commitOnSwitch = false;
            endTail();
            slot.position = 0;
            schedule(S::Stopped, S::Stopped, -1);
            return;
        case S::Stopped: schedule(S::StartArmed, S::Playing, line); return;
        case S::StartArmed: schedule(S::Stopped, S::Stopped, -1); return;
        }
        return;
    case LoopCommand::Undo:
        if (slot.state == S::Overdubbing || slot.state == S::OverdubArmed) {
            dropLayer();
            schedule(S::Playing, S::Playing, -1);
        } else if (slot.layers > 0 && slot.state != S::Empty && slot.state != S::Armed && !recordsBase(slot.state)) {
            --slot.layers;
            slot.dirty |= 1U << static_cast<unsigned>(slot.layers); // its sound is still in the buffer
        }
        return;
    case LoopCommand::Stop:
        dropLayer();
        if (slot.state == S::Empty || slot.state == S::Armed || recordsBase(slot.state)) {
            slot.written = 0;
            schedule(S::Empty, S::Empty, -1);
        } else {
            endTail();
            slot.position = 0;
            schedule(S::Stopped, S::Stopped, -1);
        }
        return;
    case LoopCommand::Clear:
        dropLayer();
        slot.written = 0;
        slot.length = 0;
        slot.take = 0;
        slot.repeats = 1;
        slot.tailTarget = slot.tailWritten = slot.tailFade = slot.played = 0;
        slot.layersPending = false;
        slot.position = 0;
        slot.layers = 0;
        slot.dirty = 0; // (new buffers come with the next recording)
        schedule(S::Empty, S::Empty, -1);
        return;
    }
}

void LoopStation::plan(Slot& slot, int64_t blockStart, int frames, const LoopGrid& lines) noexcept
{
    using S = LoopState;
    // A layer asked for before its buffers were there: on the next line now.
    if (slot.state == S::OverdubArmed && slot.switchAt < 0 && canLayer(slot)) {
        slot.switchAt = lines.valid() ? lines.next(blockStart) : blockStart;
    }

    int split = frames;
    LoopState after = slot.state;
    bool full = false;
    if (slot.switchAt >= 0 && slot.switchAt < blockStart + frames) {
        split = static_cast<int>(std::max<int64_t>(slot.switchAt - blockStart, 0));
        after = slot.target;
    }
    // Out of room while recording: the loop closes where the room ends.
    if (recordsBase(slot.state)) {
        const int64_t room = capacity(slot) - slot.written;
        if (room < split) {
            split = static_cast<int>(std::max<int64_t>(room, 0));
            after = S::Playing;
            full = true;
        }
    }

    Segment& first = slot.segments.at(0);
    first = Segment{.state = slot.state,
                    .from = 0,
                    .to = split,
                    .index = recordsBase(slot.state) ? slot.written : slot.position,
                    .done = slot.overdubDone,
                    .layers = slot.layers,
                    .played = slot.played};
    slot.segmentCount = 1;
    if (split >= frames) return;

    // The switch, `split` samples into the block: first the block so far...
    const int64_t written = slot.written + (recordsBase(slot.state) ? split : 0);
    const int64_t position = slot.length > 0 ? (slot.position + split) % slot.length : 0;
    slot.played = sounds(slot.state) ? std::min(slot.take, slot.played + split) : slot.played;
    // ... then what the new state starts from.
    int64_t index = 0;
    switch (after) {
    case S::Recording:
        slot.recordStart = blockStart + split;
        slot.written = 0;
        index = 0;
        break;
    case S::Playing:
    case S::Stopped:
        if (recordsBase(slot.state)) { // the loop closes
            if (written == 0) {
                slot.written = 0;
                after = S::Empty;
                break;
            }
            closeTake(slot, written, 0, full ? 1 : slot.repeats, lines); // (told in endBlock, after its length is published)
            if (full) slot.full.store(true, std::memory_order_release);
            // Free: the first loop is the grid for the others (quarters of it).
            if (!m_sync.load(std::memory_order_relaxed) && !freeGrid().valid()) {
                m_freeOrigin.store(slot.recordStart, std::memory_order_relaxed);
                m_freeUnit.store(static_cast<double>(slot.length) / 4.0, std::memory_order_relaxed);
            }
        } else if (slot.state == S::Overdubbing) { // a layer ends
            if (slot.commitOnSwitch) ++slot.layers;
            slot.commitOnSwitch = false;
            slot.overdubDone = 0;
            slot.position = position;
        } else if (slot.state == S::StartArmed) {
            slot.position = 0; // from the top, on the line (after silence: no spill over its start)
            slot.played = 0;
        } else {
            slot.position = position;
        }
        if (after == S::Stopped) {
            slot.position = 0;
            slot.played = 0;
        }
        index = slot.position;
        break;
    case S::Overdubbing:
        slot.position = position;
        slot.overdubDone = 0;
        index = position;
        break;
    default: index = position; break;
    }
    slot.state = after;
    slot.switchAt = -1;
    // A recording of a set length: it knows where it ends, and closes there.
    if (after == S::Recording && lines.valid() && m_sync.load(std::memory_order_relaxed) && targetLines() > 0) {
        slot.switchAt = slot.recordStart + std::llround(targetLines() * lines.unit);
        slot.target = S::Playing;
    }
    slot.segments.at(1) = Segment{.state = after,
                                  .from = split,
                                  .to = frames,
                                  .index = index,
                                  .done = slot.overdubDone,
                                  .layers = slot.layers,
                                  .played = slot.played};
    slot.segmentCount = 2;
}

void LoopStation::beginBlock(int64_t blockStart, int frames, const LoopGrid& bars) noexcept
{
    m_blockStart = blockStart;
    m_frames = std::max(frames, 0);
    const LoopGrid now = gridFor(bars);
    bool anyLoop = false;
    for (Slot& slot : m_slots) {
        slot.current = slot.data.acquire();
        slot.recordedThisBlock = false;
        if (slot.current != slot.seen) { // new buffers: the layers made fresh are clean again
            if (slot.current != nullptr) slot.dirty &= ~slot.current->freshMask;
            slot.seen = slot.current;
        }
        Commands& queue = slot.commands;
        uint32_t tail = queue.tail.load(std::memory_order_relaxed);
        while (tail != queue.head.load(std::memory_order_acquire)) {
            apply(slot, queue.items.at(tail), blockStart, now, m_sync.load(std::memory_order_relaxed) ? targetLines() : 0);
            tail = (tail + 1) % Commands::kSize;
            queue.tail.store(tail, std::memory_order_release);
        }
        plan(slot, blockStart, m_frames, gridFor(bars)); // (a free first loop may have just set the grid)
        slot.usable = fits(slot);
        anyLoop = anyLoop || slot.state != LoopState::Empty;
    }
    // Every loop cleared: the next free loop sets a new grid.
    if (!anyLoop) m_freeUnit.store(0.0, std::memory_order_relaxed);
}

void LoopStation::record(int slot, const float* left, const float* right, int frames) noexcept
{
    if (slot < 0 || slot >= kSlots || left == nullptr || right == nullptr || frames != m_frames) return;
    Slot& s = m_slots.at(static_cast<std::size_t>(slot));
    if (s.current == nullptr || !s.usable) return;
    s.recordedThisBlock = true;
    const auto count = static_cast<std::size_t>(frames);
    const std::span<const float> inLeft(left, count);
    const std::span<const float> inRight(right, count);
    for (int n = 0; n < s.segmentCount; ++n) {
        const Segment& seg = s.segments.at(static_cast<std::size_t>(n));
        const auto from = static_cast<std::size_t>(seg.from);
        const auto size = static_cast<std::size_t>(seg.to - seg.from);
        if (recordsBase(seg.state) && s.current->base) {
            const auto at = static_cast<std::size_t>(seg.index);
            const std::size_t room = s.current->base->left.size() - std::min(at, s.current->base->left.size());
            const std::size_t fits = std::min(size, room); // (the plan keeps within it)
            std::ranges::copy(inLeft.subspan(from, fits), s.current->base->left.begin() + static_cast<std::ptrdiff_t>(at));
            std::ranges::copy(inRight.subspan(from, fits), s.current->base->right.begin() + static_cast<std::ptrdiff_t>(at));
        } else if (seg.state == LoopState::Overdubbing && s.length > 0 && std::cmp_less(seg.layers, s.current->layers.size())) {
            LoopTake& layer = *s.current->layers.at(static_cast<std::size_t>(seg.layers));
            int64_t p = seg.index;
            int64_t done = seg.done;
            const std::span<const float> segRight = inRight.subspan(from, size);
            auto r = segRight.begin();
            for (const float l : inLeft.subspan(from, size)) {
                const auto at = static_cast<std::size_t>(p);
                if (done < s.length) { // its first pass: written over what was there
                    layer.left.at(at) = l;
                    layer.right.at(at) = *r;
                } else {
                    layer.left.at(at) += l;
                    layer.right.at(at) += *r;
                }
                ++r;
                ++done;
                if (++p == s.length) p = 0;
            }
        }
        // After the take: what rings on past its end, kept to play over its start.
        if (sounds(seg.state) && s.take > 0 && s.tailWritten < s.tailTarget && s.current->base) {
            LoopTake& base = *s.current->base;
            const auto at = static_cast<std::size_t>(s.take + s.tailWritten);
            const std::size_t room = base.left.size() - std::min(at, base.left.size());
            const std::size_t taken = std::min({size, static_cast<std::size_t>(s.tailTarget - s.tailWritten), room});
            std::ranges::copy(inLeft.subspan(from, taken), base.left.begin() + static_cast<std::ptrdiff_t>(at));
            std::ranges::copy(inRight.subspan(from, taken), base.right.begin() + static_cast<std::ptrdiff_t>(at));
            s.tailWritten += static_cast<int64_t>(taken);
        }
    }
}

void LoopStation::play(float* left, float* right, int frames) noexcept
{
    if (left == nullptr || right == nullptr || frames != m_frames) return;
    const auto count = static_cast<std::size_t>(frames);
    const std::span<float> outLeft(left, count);
    const std::span<float> outRight(right, count);
    for (Slot& s : m_slots) {
        if (s.current == nullptr || !s.current->base || s.length <= 0 || s.take <= 0 || !s.usable) continue;
        const LoopTake& base = *s.current->base;
        // The seam: faded in from silence; faded out too when nothing rang on past it.
        const int64_t declick = std::min<int64_t>(kDeclickFrames, std::max<int64_t>(s.take / 4, 1));
        const bool fadeEnd = s.tailTarget == 0;
        const float tailFade = static_cast<float>(std::max<int64_t>(s.tailFade, 1));
        for (int n = 0; n < s.segmentCount; ++n) {
            const Segment& seg = s.segments.at(static_cast<std::size_t>(n));
            if (!sounds(seg.state)) continue;
            const int committed = std::min<int>(seg.layers, static_cast<int>(s.current->layers.size()));
            int64_t p = seg.index;
            int64_t q = seg.index % s.take; // in the take
            int64_t played = seg.played;
            int64_t done = seg.done;
            const auto from = static_cast<std::size_t>(seg.from);
            const auto size = static_cast<std::size_t>(seg.to - seg.from);
            const std::span<float> segRight = outRight.subspan(from, size);
            auto out = segRight.begin();
            for (float& outL : outLeft.subspan(from, size)) {
                const auto at = static_cast<std::size_t>(p);
                const auto inTake = static_cast<std::size_t>(q);
                float gain = 1.0F;
                if (q < declick) gain = static_cast<float>(q) / static_cast<float>(declick);
                if (fadeEnd && q >= s.take - declick) gain = std::min(gain, static_cast<float>(s.take - q) / static_cast<float>(declick));
                float l = base.left.at(inTake) * gain;
                float r = base.right.at(inTake) * gain;
                // What rang on past the end, over the start, once it has come round.
                if (played >= s.take && q < s.tailWritten) {
                    const float ring = std::min(1.0F, static_cast<float>(s.tailTarget - q) / tailFade);
                    l += base.left.at(static_cast<std::size_t>(s.take + q)) * ring;
                    r += base.right.at(static_cast<std::size_t>(s.take + q)) * ring;
                }
                if (played < s.take) ++played;
                if (++q == s.take) q = 0;
                for (int k = 0; k < committed; ++k) {
                    const LoopTake& layer = *s.current->layers.at(static_cast<std::size_t>(k));
                    l += layer.left.at(at);
                    r += layer.right.at(at);
                }
                // The layer being recorded, where a pass of it is already there.
                if (seg.state == LoopState::Overdubbing && done >= s.length &&
                    std::cmp_less(seg.layers, s.current->layers.size())) {
                    const LoopTake& layer = *s.current->layers.at(static_cast<std::size_t>(seg.layers));
                    l += layer.left.at(at);
                    r += layer.right.at(at);
                }
                outL += l;
                *out++ += r;
                ++done;
                if (++p == s.length) p = 0;
            }
        }
    }
}

void LoopStation::endBlock() noexcept
{
    for (Slot& s : m_slots) {
        if (s.segmentCount > 0) {
            const Segment& last = s.segments.at(static_cast<std::size_t>(s.segmentCount - 1));
            const int64_t n = last.to - last.from;
            // A layer's first pass with no sound from its channel (it is not
            // in this patch): silence over what an undone layer left there.
            if (!s.recordedThisBlock && s.current != nullptr && s.length > 0) {
                for (int k = 0; k < s.segmentCount; ++k) {
                    const Segment& seg = s.segments.at(static_cast<std::size_t>(k));
                    if (seg.state != LoopState::Overdubbing || std::cmp_greater_equal(seg.layers, s.current->layers.size())) {
                        continue;
                    }
                    LoopTake& layer = *s.current->layers.at(static_cast<std::size_t>(seg.layers));
                    int64_t p = seg.index;
                    for (int64_t d = seg.done; d < seg.done + (seg.to - seg.from); ++d) {
                        if (d < s.length) {
                            layer.left.at(static_cast<std::size_t>(p)) = 0.0F;
                            layer.right.at(static_cast<std::size_t>(p)) = 0.0F;
                        }
                        if (++p == s.length) p = 0;
                    }
                }
            }
            if (recordsBase(last.state)) s.written = last.index + n;
            if (s.length > 0 && (sounds(last.state))) {
                s.position = (last.index + n) % s.length;
                s.played = std::min(s.take, last.played + n);
            }
            if (last.state == LoopState::Overdubbing) s.overdubDone = last.done + n;
            // What rings on past the take, with no sound from its channel
            // (it is not in this patch): silence, and it is in.
            if (!s.recordedThisBlock && s.tailWritten < s.tailTarget) {
                for (int k = 0; k < s.segmentCount; ++k) {
                    const Segment& seg = s.segments.at(static_cast<std::size_t>(k));
                    if (sounds(seg.state)) s.tailWritten = std::min(s.tailTarget, s.tailWritten + (seg.to - seg.from));
                }
            }
        }
        s.outState.store(s.state, std::memory_order_release);
        s.outLength.store(s.length, std::memory_order_relaxed);
        s.outPosition.store(recordsBase(s.state) ? s.written : s.position, std::memory_order_relaxed);
        s.outLayers.store(s.layers, std::memory_order_relaxed);
        s.outWait.store(s.switchAt >= 0 ? std::max<int64_t>(s.switchAt - (m_blockStart + m_frames), 0) : 0,
                        std::memory_order_relaxed);
        s.outDirty.store(s.dirty, std::memory_order_release);
        s.outTake.store(s.take, std::memory_order_relaxed);
        s.outTail.store(s.tailWritten, std::memory_order_relaxed);
        // Closed, and what rang on past it is in: its layers (and the base
        // cut to the take and that) are asked for (what is above goes with it).
        if (s.layersPending && s.tailWritten >= s.tailTarget) {
            s.layersPending = false;
            s.needsLayers.store(true, std::memory_order_release);
        }
        if (s.current != nullptr) s.data.release();
        s.current = nullptr;
        s.segmentCount = 0;
    }
}

} // namespace gigchain::engine
