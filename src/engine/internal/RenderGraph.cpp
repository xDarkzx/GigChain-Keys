#include "RenderGraph.h"

#include "gigchain/core/Limits.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <span>

namespace gigchain::engine {
namespace {

void atomicMax(std::atomic<float>& target, float value) noexcept
{
    float current = target.load(std::memory_order_relaxed);
    while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
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
      m_left(static_cast<std::size_t>(maxBlock), 0.0F),
      m_right(static_cast<std::size_t>(maxBlock), 0.0F),
      m_routed(static_cast<std::size_t>(kMaxEventsPerBlock))
{
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

void ChannelStrip::render(std::span<const MidiEvent> events, const AudioBlock& mix, bool anySolo) noexcept
{
    std::size_t routedCount = 0;
    for (const MidiEvent& event : events) {
        if (routedCount == m_routed.size()) break;
        if (const auto routed = routeEvent(event, m_route)) m_routed.at(routedCount++) = *routed; // room checked above
    }

    AudioBlock block{.left = m_left.data(), .right = m_right.data(), .frames = mix.frames};
    const auto frames = static_cast<std::size_t>(mix.frames);
    if (m_instrument) {
        m_instrument->process(std::span<const MidiEvent>(m_routed.data(), routedCount), block);
    } else {
        std::fill_n(block.left, frames, 0.0F);
        std::fill_n(block.right, frames, 0.0F);
    }
    for (const auto& effect : m_effects) {
        effect->process({}, block);
    }

    // Silenced strips still process so instruments and effect tails keep state.
    const bool audible = !m_mute.load(std::memory_order_relaxed) && (!anySolo || m_solo.load(std::memory_order_relaxed));
    const float gain = audible ? m_gain.load(std::memory_order_relaxed) : 0.0F;
    // Constant-power pan, normalised so the centre is unity on both sides.
    const double angle = (static_cast<double>(m_pan.load(std::memory_order_relaxed)) + 1.0) * std::numbers::pi / 4.0;
    const float leftGain = gain * static_cast<float>(std::cos(angle) * std::numbers::sqrt2);
    const float rightGain = gain * static_cast<float>(std::sin(angle) * std::numbers::sqrt2);

    float peak = 0.0F;
    double sumSquares = 0.0;
    // The block's own buffers and the mix, each `frames` long (checked by the graph).
    const std::span<const float> fromLeft(block.left, frames);
    const std::span<const float> fromRight(block.right, frames);
    const std::span<float> toLeft(mix.left, frames);
    const std::span<float> toRight(mix.right, frames);
    auto inRight = fromRight.begin();
    auto outLeft = toLeft.begin();
    auto outRight = toRight.begin();
    for (auto inLeft = fromLeft.begin(); inLeft != fromLeft.end(); ++inLeft, ++inRight, ++outLeft, ++outRight) {
        const float left = *inLeft * leftGain;
        const float right = *inRight * rightGain;
        *outLeft += left;
        *outRight += right;
        peak = std::max({peak, std::abs(left), std::abs(right)});
        sumSquares += 0.5 * (static_cast<double>(left) * left + static_cast<double>(right) * right);
    }
    atomicMax(m_peak, peak);
    m_rms.store(frames > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(frames))) : 0.0F,
                std::memory_order_relaxed);
}

RenderGraph::RenderGraph(std::vector<StripSpec> specs, double sampleRate, int maxBlock,
                         std::vector<std::shared_ptr<INode>> masterEffects)
    : m_masterEffects(std::move(masterEffects)), m_sampleRate(sampleRate), m_maxBlock(maxBlock)
{
    m_strips.reserve(specs.size());
    std::ranges::transform(specs, std::back_inserter(m_strips), [maxBlock](StripSpec& spec) {
        return std::make_unique<ChannelStrip>(std::move(spec), maxBlock);
    });
}

void RenderGraph::render(std::span<const MidiEvent> events, AudioBlock out, float masterGain) noexcept
{
    const auto frames = static_cast<std::size_t>(std::max(out.frames, 0));
    std::fill_n(out.left, frames, 0.0F);
    std::fill_n(out.right, frames, 0.0F);
    if (out.frames > m_maxBlock) {
        m_oversizedBlocks.fetch_add(1, std::memory_order_relaxed); // never overrun strip buffers
        return;
    }
    if (out.frames <= 0) return;

    const bool anySolo = std::ranges::any_of(m_strips, [](const auto& s) { return s->solo(); });
    for (const auto& channel : m_strips) {
        channel->render(events, out, anySolo);
    }
    for (const auto& effect : m_masterEffects) {
        effect->process({}, out);
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

ChannelStrip* RenderGraph::findStrip(const core::ChannelId& id)
{
    const auto it = std::ranges::find_if(m_strips, [&id](const auto& s) { return s->id() == id; });
    return it == m_strips.end() ? nullptr : it->get();
}

} // namespace gigchain::engine
