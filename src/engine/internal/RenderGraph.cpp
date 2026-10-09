#include "RenderGraph.h"

#include "LoopStation.h"

#include "gigchain/core/Limits.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <numeric>
#include <span>

namespace gigchain::engine {
namespace {

void atomicMax(std::atomic<float>& target, float value) noexcept
{
    float current = target.load(std::memory_order_relaxed);
    while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

// Below this a tail counts as silent (-80 dB).
constexpr float kQuietLevel = 1.0e-4F;
// A tail stops after this long quiet with nothing held...
constexpr double kQuietSeconds = 1.0;
// ... or this long with nothing held at all (a drone that never fades).
constexpr double kLongestTailSeconds = 30.0;

bool isNoteOff(const MidiEvent& e)
{
    const int type = e.status & 0xF0;
    return type == 0x80 || (type == 0x90 && e.data2 == 0);
}

bool isSustain(const MidiEvent& e)
{
    return (e.status & 0xF0) == 0xB0 && e.data1 == 64;
}

} // namespace

float dbToGain(double volumeDb)
{
    if (!std::isfinite(volumeDb) || volumeDb <= core::limits::kMinVolumeDb) return 0.0F;
    return static_cast<float>(std::pow(10.0, std::min(volumeDb, core::limits::kMaxVolumeDb) / 20.0));
}

ChannelStrip::ChannelStrip(StripSpec spec, int maxBlock)
    : m_id(std::move(spec.id)),
      m_route(spec.route),
      m_instrument(std::move(spec.instrument)),
      m_effects(std::move(spec.effects)),
      m_mappings(std::move(spec.mappings)),
      m_mappedNow(m_mappings.size()),
      m_pickups(m_mappings.size()),
      m_inputLeft(spec.inputLeft),
      m_inputRight(spec.inputRight),
      m_left(static_cast<std::size_t>(maxBlock), 0.0F),
      m_right(static_cast<std::size_t>(maxBlock), 0.0F),
      m_routed(static_cast<std::size_t>(kMaxStripEventsPerBlock)),
      m_midiEffects(spec.midiEffects),
      m_effected(spec.midiEffects.any() ? static_cast<std::size_t>(kMaxStripEventsPerBlock) : 0)
{
    m_outputPair = std::max(spec.outputPair, 0);
    for (auto& now : m_mappedNow) now.store(-1.0F, std::memory_order_relaxed); // not known until refreshed
    refreshMappedValues();
    setVolumeDb(spec.volumeDb);
    setPan(spec.pan);
    m_mute.store(spec.mute, std::memory_order_relaxed);
    m_solo.store(spec.solo, std::memory_order_relaxed);
}

void ChannelStrip::setVolumeDb(double volumeDb)
{
    if (!std::isfinite(volumeDb)) return;
    m_gain.store(dbToGain(volumeDb), std::memory_order_relaxed);
}

void ChannelStrip::setPan(double pan)
{
    if (!std::isfinite(pan)) return;
    m_pan.store(static_cast<float>(std::clamp(pan, -1.0, 1.0)), std::memory_order_relaxed);
}

LevelReading ChannelStrip::takeLevel()
{
    return LevelReading{.peak = m_peak.exchange(0.0F, std::memory_order_relaxed), .rms = m_rms.load(std::memory_order_relaxed)};
}

std::vector<const INode*> ChannelStrip::nodes() const
{
    std::vector<const INode*> all;
    if (m_instrument) all.push_back(m_instrument.get());
    std::ranges::transform(m_effects, std::back_inserter(all), [](const auto& effect) { return effect.get(); });
    return all;
}

std::span<const MidiEvent> ChannelStrip::effected(std::size_t count, int frames, const TimeInfo& time) noexcept
{
    const std::span<const MidiEvent> routed(m_routed.data(), count);
    if (m_effected.empty()) return routed; // no MIDI effects
    const std::size_t made = m_midiEffects.process(routed, m_effected, frames, time);
    return {m_effected.data(), made};
}

INode* ChannelStrip::mappingTarget(const ParameterMapping& m) const noexcept
{
    if (m.target < 0) return m_instrument.get();
    return std::cmp_less(m.target, m_effects.size()) ? m_effects.at(static_cast<std::size_t>(m.target)).get() : nullptr;
}

void ChannelStrip::refreshMappedValues()
{
    for (std::size_t i = 0; i < m_mappings.size(); ++i) {
        const ParameterMapping& m = m_mappings.at(i);
        const INode* target = mappingTarget(m);
        const double now = target != nullptr ? target->parameterValue(m.parameter) : -1.0;
        m_mappedNow.at(i).store(static_cast<float>(now), std::memory_order_relaxed);
    }
}

void ChannelStrip::produce(std::span<const MidiEvent> routed, int frames, const TimeInfo& time,
                           const AudioInputs& inputs) noexcept
{
    AudioBlock block{.left = m_left.data(), .right = m_right.data(), .frames = frames};
    const auto count = static_cast<std::size_t>(frames);
    // An input channel's samples for this block, or nullptr when there is
    // no such input (unplugged, or the device has fewer inputs).
    const auto input = [&inputs, frames](int channel) -> const float* {
        return channel >= 0 && std::cmp_less(channel, inputs.channels.size()) && inputs.frames >= frames
                   ? inputs.channels.subspan(static_cast<std::size_t>(channel)).front()
                   : nullptr;
    };
    if (const float* left = input(m_inputLeft); left != nullptr) {
        const float* right = input(m_inputRight);
        std::copy_n(left, count, block.left);
        std::copy_n(right != nullptr ? right : left, count, block.right); // mono: on both sides
    } else if (m_instrument) {
        m_instrument->process(routed, block, time);
    } else {
        std::fill_n(block.left, count, 0.0F);
        std::fill_n(block.right, count, 0.0F);
    }
    for (const auto& effect : m_effects) {
        effect->process({}, block, time);
    }
}

float ChannelStrip::mixInto(const AudioBlock& mix, float gain, LoopStation* loops) noexcept
{
    const auto frames = static_cast<std::size_t>(mix.frames);
    // Constant-power pan, normalised so the centre is unity on both sides.
    const double angle = (static_cast<double>(m_pan.load(std::memory_order_relaxed)) + 1.0) * std::numbers::pi / 4.0;
    const float leftGain = gain * static_cast<float>(std::cos(angle) * std::numbers::sqrt2);
    const float rightGain = gain * static_cast<float>(std::sin(angle) * std::numbers::sqrt2);

    float peak = 0.0F;
    double sumSquares = 0.0;
    // The strip's own buffers (the fader and pan go on them: what is heard
    // of the strip, for its loop too) and the mix, each `frames` long
    // (checked by the graph).
    const std::span<float> fromLeft(m_left.data(), frames);
    const std::span<float> fromRight(m_right.data(), frames);
    const std::span<float> toLeft(mix.left, frames);
    const std::span<float> toRight(mix.right, frames);
    auto inRight = fromRight.begin();
    auto outLeft = toLeft.begin();
    auto outRight = toRight.begin();
    for (auto inLeft = fromLeft.begin(); inLeft != fromLeft.end(); ++inLeft, ++inRight, ++outLeft, ++outRight) {
        const float left = *inLeft * leftGain;
        const float right = *inRight * rightGain;
        *inLeft = left;
        *inRight = right;
        *outLeft += left;
        *outRight += right;
        peak = std::max({peak, std::abs(left), std::abs(right)});
        sumSquares += 0.5 * (static_cast<double>(left) * left + static_cast<double>(right) * right);
    }
    m_rms.store(frames > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(frames))) : 0.0F,
                std::memory_order_relaxed);
    if (const int slot = m_loopSlot.load(std::memory_order_relaxed); loops != nullptr && slot >= 0) {
        loops->record(slot, m_left.data(), m_right.data(), mix.frames);
    }
    return peak;
}

void ChannelStrip::render(std::span<const MidiEvent> events, const AudioBlock& mix, bool anySolo, const TimeInfo& time,
                          const AudioInputs& inputs, const SectionGate& gate, LoopStation* loops) noexcept
{
    const uint64_t mask = m_sections.load(std::memory_order_relaxed);
    const bool outside = m_unsectioned.load(std::memory_order_relaxed);
    // Whether this strip plays in `section` (none: as set for outside any
    // section; out of range: every strip does).
    const auto inSection = [mask, outside](int section) {
        if (section < 0) return outside;
        return section >= 64 || ((mask >> section) & 1U) != 0;
    };
    // Whether a new note at `offset` is for this strip: its section is in force.
    const auto plays = [&gate, &inSection](int offset) {
        return inSection(offset < gate.switchAt ? gate.before : gate.after);
    };
    std::size_t routedCount = 0;
    uint64_t dropped = 0; // no room left in the block (counted, never unseen)
    for (const MidiEvent& event : events) {
        const bool newNote = (event.status & 0xF0) == 0x90 && event.data2 > 0;
        if (newNote && !plays(event.sampleOffset)) continue;
        // A controller mapped to a parameter moves it, and nothing else hears it.
        bool mapped = false;
        if ((event.status & 0xF0) == 0xB0) {
            const int channel = (event.status & 0x0F) + 1;
            for (std::size_t i = 0; i < m_mappings.size(); ++i) {
                const ParameterMapping& m = m_mappings.at(i);
                if (std::cmp_not_equal(m.controller, event.data1) || (m.midiChannel != 0 && m.midiChannel != channel)) continue;
                INode* target = mappingTarget(m);
                if (target == nullptr) continue;
                mapped = true;
                const double value = m.minimum + ((m.maximum - m.minimum) * core::shapeKnob(event.data2, m.curve));
                // With pickup, a knob that is not where the parameter is moves nothing until it gets there.
                if (m.pickup && !m_pickups.at(i).take(value, m_mappedNow.at(i).load(std::memory_order_relaxed))) continue;
                target->queueParameter(m.parameter, value, event.sampleOffset);
            }
        }
        if (mapped) continue;
        const auto routed = routeEvent(event, m_route);
        if (!routed) continue;
        if (routedCount == m_routed.size()) {
            ++dropped;
            continue;
        }
        m_routed.at(routedCount++) = *routed; // room checked above
    }
    if (dropped > 0) m_droppedEvents.fetch_add(dropped, std::memory_order_relaxed);
    produce(effected(routedCount, mix.frames, time), mix.frames, time, inputs);

    // Silenced strips still process so instruments and effect tails keep state.
    const bool audible = !m_mute.load(std::memory_order_relaxed) && (!anySolo || m_solo.load(std::memory_order_relaxed));
    atomicMax(m_peak, mixInto(mix, audible ? m_gain.load(std::memory_order_relaxed) : 0.0F, loops));
}

void ChannelStrip::renderTail(std::span<const MidiEvent> events, const AudioBlock& mix, const TimeInfo& time,
                              LoopStation* loops) noexcept
{
    if (m_tailDone.load(std::memory_order_relaxed)) return;
    std::size_t routedCount = 0;
    for (const MidiEvent& event : events) {
        if (!isNoteOff(event) && !isSustain(event)) continue; // new notes belong to the new patch
        const auto routed = routeEvent(event, m_route);
        if (!routed) continue;
        if (routedCount == m_routed.size()) {
            m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        m_routed.at(routedCount++) = *routed; // room checked above
    }
    // (Through its MIDI effects too: a chord's other notes and an arpeggio end with the keys.)
    produce(effected(routedCount, mix.frames, time), mix.frames, time, {});
    const float gain = m_mute.load(std::memory_order_relaxed) ? 0.0F : m_gain.load(std::memory_order_relaxed);
    const float peak = mixInto(mix, gain, loops);

    // Done after a second of silence with nothing held, or when it has
    // droned on long after every key was let go.
    if (m_instrument && m_instrument->holdsNotes()) {
        m_quietSamples = 0;
        m_releasedSamples = 0;
        return;
    }
    m_releasedSamples += mix.frames;
    m_quietSamples = peak < kQuietLevel ? m_quietSamples + mix.frames : 0;
    const auto quietEnough = static_cast<int64_t>(kQuietSeconds * time.sampleRate);
    const auto longest = static_cast<int64_t>(kLongestTailSeconds * time.sampleRate);
    if (m_quietSamples >= quietEnough || m_releasedSamples >= longest) m_tailDone.store(true, std::memory_order_relaxed);
}

RenderGraph::RenderGraph(std::vector<StripSpec> specs, double sampleRate, int maxBlock,
                         std::vector<std::shared_ptr<INode>> masterEffects, std::vector<std::shared_ptr<ChannelStrip>> tails)
    : m_tails(std::move(tails)), m_masterEffects(std::move(masterEffects)), m_sampleRate(sampleRate), m_maxBlock(maxBlock)
{
    m_strips.reserve(specs.size());
    std::ranges::transform(specs, std::back_inserter(m_strips), [maxBlock](StripSpec& spec) {
        return std::make_shared<ChannelStrip>(std::move(spec), maxBlock);
    });
}

void RenderGraph::render(std::span<const MidiEvent> events, AudioBlock out, float masterGain, const TimeInfo& time,
                         const AudioInputs& inputs, const SectionGate& gate, LoopStation* loops,
                         std::span<float* const> sends) noexcept
{
    const auto frames = static_cast<std::size_t>(std::max(out.frames, 0));
    std::fill_n(out.left, frames, 0.0F);
    std::fill_n(out.right, frames, 0.0F);
    if (out.frames > m_maxBlock) {
        m_oversizedBlocks.fetch_add(1, std::memory_order_relaxed); // never overrun strip buffers
        return;
    }
    if (out.frames <= 0) return;

    // A strip sent to outputs of its own plays there directly (its fader, not
    // the master's); a pair the device does not have: the mix.
    const auto blockFor = [&out, &sends](int pair) { return sendPair(sends, pair, out); };
    const bool anySolo = std::ranges::any_of(m_strips, [](const auto& s) { return s->solo(); });
    for (const auto& channel : m_strips) {
        channel->render(events, blockFor(channel->outputPair()), anySolo, time, inputs, gate, loops);
    }
    for (const auto& tail : m_tails) {
        if (out.frames <= tail->maxBlock()) tail->renderTail(events, blockFor(tail->outputPair()), time, loops);
    }
    // The loops, whatever patch is playing, through the master effects.
    if (loops != nullptr) loops->play(out.left, out.right, out.frames);
    for (const auto& effect : m_masterEffects) {
        effect->process({}, out, time);
    }
    const auto gain = [masterGain](float sample) { return sample * masterGain; };
    const std::span<float> left(out.left, frames);
    const std::span<float> right(out.right, frames);
    std::ranges::transform(left, left.begin(), gain);
    std::ranges::transform(right, right.begin(), gain);
}

ChannelStrip* RenderGraph::strip(std::size_t index)
{
    return index < m_strips.size() ? m_strips.at(index).get() : nullptr;
}

uint64_t RenderGraph::takeDroppedEvents()
{
    const auto take = [](uint64_t sum, const std::shared_ptr<ChannelStrip>& one) { return sum + one->takeDroppedEvents(); };
    const uint64_t live = std::accumulate(m_strips.begin(), m_strips.end(), uint64_t{0}, take);
    return std::accumulate(m_tails.begin(), m_tails.end(), live, take);
}

ChannelStrip* RenderGraph::findStrip(const core::ChannelId& id)
{
    const auto it = std::ranges::find_if(m_strips, [&id](const auto& s) { return s->id() == id; });
    return it == m_strips.end() ? nullptr : it->get();
}

} // namespace gigchain::engine
