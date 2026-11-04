#include "SustainPads.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>

namespace gigchain::engine {

namespace {

// After fading out, a pad runs on (unheard) this long, so its notes and
// reverb have died away before it plays again.
constexpr double kTailSeconds = 15.0;
constexpr int64_t kIdle = std::numeric_limits<int64_t>::max() / 2;
constexpr float kMinFadeSeconds = 0.1F;
constexpr float kMaxFadeSeconds = 30.0F;
// The pads start at once: the fade is the swell (a pad's own bloom on top
// would dip the crossfade).
constexpr double kAttackSeconds = 0.05;
constexpr uint8_t kVelocity = 100;
// Two pads sound together through a crossfade (equal power: up to 3 dB
// over one): full volume leaves that room.
constexpr float kHeadroom = 0.7071F;
// Its top: clean up to the knee, then curving to full scale, never over it.
constexpr float kKnee = 0.8F;
float softTop(float x) noexcept
{
    const float size = std::abs(x);
    if (size <= kKnee) return x;
    const float over = (1.0F - kKnee) * std::tanh((size - kKnee) / (1.0F - kKnee));
    return std::copysign(kKnee + over, x);
}

// The VST watchdog: this quiet (-100 dB) this long while it should sound, it
// is pressed again; at most this often.
constexpr float kSilence = 1.0e-5F;
constexpr double kSilentSeconds = 0.75;
constexpr double kWatchEverySeconds = 3.0;

MidiEvent noteOn(int note) { return {.status = 0x90, .data1 = static_cast<uint8_t>(note), .data2 = kVelocity}; }
MidiEvent noteOff(int note) { return {.status = 0x80, .data1 = static_cast<uint8_t>(note), .data2 = 0}; }

// Adds `event` to a voicing's waiting notes (room for a chord let go and one pressed).
template <std::size_t N>
void queue(std::array<MidiEvent, N>& pending, std::size_t& count, const MidiEvent& event) noexcept
{
    if (count < pending.size()) pending.at(count++) = event;
}

} // namespace

std::array<int, 3> SustainPads::chord(int key, int octave) noexcept
{
    const int k = ((key % 12) + 12) % 12;
    const int root = 43 + ((k + 5) % 12) + (12 * std::clamp(octave, -1, 1)); // G2 .. F#3, C is C3
    return {root, root + 7, root + 12};
}

void SustainPads::prepare(double sampleRate, int maxBlock)
{
    m_prepared.store(false, std::memory_order_release);
    m_rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_maxBlock = std::max(maxBlock, 1);
    m_slotLeft.assign(static_cast<std::size_t>(m_maxBlock), 0.0F);
    m_slotRight.assign(static_cast<std::size_t>(m_maxBlock), 0.0F);
    m_mixLeft.assign(static_cast<std::size_t>(m_maxBlock), 0.0F);
    m_mixRight.assign(static_cast<std::size_t>(m_maxBlock), 0.0F);
    for (Slot& slot : m_slots) {
        slot.synth.prepare(m_rate, m_maxBlock);
        slot.notes = {-1, -1, -1};
        slot.preset = -1;
        slot.fade = 0.0F;
        slot.target = 0.0F;
        slot.held = false;
        slot.quiet = kIdle;
        slot.pendingCount = 0;
    }
    m_live = 0;
    m_sounding = false;
    m_instrumentSeen = nullptr; // (its instrument, re-prepared too, starts afresh)
    m_external = Held{};
    m_external.quiet = kIdle;
    m_soundSeen.fill(-1.0F);
    m_gain = GlidingGain{};
    m_masterGain = GlidingGain{};
    m_prepared.store(true, std::memory_order_release);
}

void SustainPads::set(const SustainPadsSettings& settings) noexcept
{
    m_key.store(settings.key, std::memory_order_relaxed);
    m_preset.store(std::clamp(settings.preset, 0, pads::padTypeCount() - 1), std::memory_order_relaxed);
    m_octave.store(std::clamp(settings.octave, -1, 1), std::memory_order_relaxed);
    const double fade = std::isfinite(settings.fadeSeconds) ? settings.fadeSeconds : 4.0;
    m_fadeSeconds.store(std::clamp(static_cast<float>(fade), kMinFadeSeconds, kMaxFadeSeconds), std::memory_order_relaxed);
    const auto unit = [](double v, double fallback) { return static_cast<float>(std::clamp(std::isfinite(v) ? v : fallback, 0.0, 1.0)); };
    m_tone.store(unit(settings.tone, 0.5), std::memory_order_relaxed);
    m_shimmer.store(unit(settings.shimmer, 0.0), std::memory_order_relaxed);
    m_motion.store(unit(settings.motion, 0.5), std::memory_order_relaxed);
    const double db = std::isfinite(settings.volumeDb) ? std::clamp(settings.volumeDb, -60.0, 0.0) : -6.0;
    const bool silent = settings.mute || db <= -60.0;
    m_volume.store(silent ? 0.0F : static_cast<float>(std::pow(10.0, db / 20.0)), std::memory_order_relaxed);
    m_pair.store(std::max(settings.outputPair, 0), std::memory_order_relaxed);
    // Last: the switch (with the key and pad it plays already in place).
    m_active.store(settings.active, std::memory_order_release);
}

LevelReading SustainPads::level() noexcept
{
    return LevelReading{.peak = m_peak.exchange(0.0F, std::memory_order_relaxed), .rms = m_rms.load(std::memory_order_relaxed)};
}

void SustainPads::follow() noexcept
{
    const bool active = m_active.load(std::memory_order_acquire);
    const auto notes = chord(m_key.load(std::memory_order_relaxed), m_octave.load(std::memory_order_relaxed));
    const int preset = m_preset.load(std::memory_order_relaxed);
    Slot& live = m_slots.at(static_cast<std::size_t>(m_live));
    if (m_instrumentSeen != nullptr) {
        // A VST instrument holds the chord: Pads Pro gives way (fading out).
        if (m_sounding) live.target = 0.0F;
        m_sounding = false;
        followInstrument(active, notes);
        return;
    }
    if (active) {
        if (!m_sounding || live.notes != notes || live.preset != preset) {
            if (m_sounding) live.target = 0.0F; // the old one fades out as the new one comes in
            m_live = 1 - m_live;
            start(m_slots.at(static_cast<std::size_t>(m_live)), notes, preset);
            m_sounding = true;
        }
    } else if (m_sounding) {
        live.target = 0.0F;
        m_sounding = false;
    }
}

void SustainPads::start(Slot& slot, const std::array<int, 3>& notes, int preset) noexcept
{
    if (slot.preset != preset) {
        slot.synth.setParam(pads::paramId(pads::Param::Type), pads::typeValue(preset));
        slot.preset = preset;
    }
    applySound(slot); // (a new pad brings its own knobs: the player's go back on)
    // (Added to what is waiting: the note-offs of a fade that ended last block are still to be sent.)
    if (slot.held) {
        for (const int note : slot.notes) queue(slot.pending, slot.pendingCount, noteOff(note));
    }
    for (const int note : notes) queue(slot.pending, slot.pendingCount, noteOn(note));
    slot.notes = notes;
    slot.held = true;
    slot.target = 1.0F;
    slot.quiet = 0;
}

void SustainPads::applySound(Slot& slot) noexcept
{
    using pads::Param;
    using pads::paramId;
    slot.synth.setParam(paramId(Param::Tone), m_tone.load(std::memory_order_relaxed));
    slot.synth.setParam(paramId(Param::Shimmer), m_shimmer.load(std::memory_order_relaxed));
    slot.synth.setParam(paramId(Param::Motion), m_motion.load(std::memory_order_relaxed));
    slot.synth.setParam(paramId(Param::Attack), pads::Ranges::fromSeconds(kAttackSeconds));
}

bool SustainPads::running(const Slot& slot) const noexcept
{
    return slot.fade > 0.0F || slot.target > 0.0F || slot.quiet < static_cast<int64_t>(kTailSeconds * m_rate);
}

void SustainPads::renderSlot(Slot& slot, int frames) noexcept
{
    const auto n = static_cast<std::size_t>(frames);
    const std::span<const MidiEvent> events(slot.pending.data(), slot.pendingCount);
    slot.synth.process(events, AudioBlock{.left = m_slotLeft.data(), .right = m_slotRight.data(), .frames = frames});
    slot.pendingCount = 0;
    mixFaded(slot.fade, slot.target, n);
    if (slot.fade <= 0.0F && slot.target <= 0.0F) {
        // Faded out: let go of its notes (unheard), and let it ring away.
        if (slot.held) {
            slot.pendingCount = 0;
            for (const int note : slot.notes) slot.pending.at(slot.pendingCount++) = noteOff(note);
            slot.held = false;
        }
        slot.quiet = std::min(slot.quiet + static_cast<int64_t>(n), kIdle);
    }
}

void SustainPads::mixFaded(float& fade, float target, std::size_t frames) noexcept
{
    const float step = 1.0F / (std::clamp(m_fadeSeconds.load(std::memory_order_relaxed), kMinFadeSeconds, kMaxFadeSeconds)
                               * static_cast<float>(m_rate));
    float now = fade;
    for (std::size_t i = 0; i < frames; ++i) {
        now = target > now ? std::min(now + step, target) : std::max(now - step, target);
        const float g = std::sin(now * std::numbers::pi_v<float> * 0.5F);
        m_mixLeft.at(i) += m_slotLeft.at(i) * g;
        m_mixRight.at(i) += m_slotRight.at(i) * g;
    }
    fade = now;
}

void SustainPads::followInstrument(bool active, const std::array<int, 3>& notes) noexcept
{
    Held& h = m_external;
    if (active && !h.sounding) {
        // Switched on (or on again while fading out): pressed afresh, never
        // back up on notes that may no longer sound (a new sound chosen in
        // the plugin meanwhile stops them in many synths).
        if (h.held) {
            for (const int note : h.notes) queue(h.pending, h.pendingCount, noteOff(note));
        }
        for (const int note : notes) queue(h.pending, h.pendingCount, noteOn(note));
        h.notes = notes;
        h.held = true;
        h.sounding = true;
        h.target = 1.0F;
        h.quiet = 0;
        h.silent = 0;
        return;
    }
    if (active) {
        if (h.held && h.notes == notes) {
            // The same chord, sounding.
        } else {
            // The new chord pressed first, then the old one let go: its own
            // release carries it over. A note in both stays held (pressed
            // again then let go, it would be gone).
            const auto in = [](const std::array<int, 3>& chordNotes, int note) { return std::ranges::find(chordNotes, note) != chordNotes.end(); };
            for (const int note : notes) {
                if (!h.held || !in(h.notes, note)) queue(h.pending, h.pendingCount, noteOn(note));
            }
            if (h.held) {
                for (const int note : h.notes) {
                    if (!in(notes, note)) queue(h.pending, h.pendingCount, noteOff(note));
                }
            }
            h.notes = notes;
            h.held = true;
        }
        h.sounding = true;
        h.target = 1.0F;
        h.quiet = 0;
    } else if (h.sounding) {
        h.target = 0.0F;
        h.sounding = false;
    }
}

void SustainPads::cutNow() noexcept
{
    // Pads Pro: both silent now (their notes let go as they run out, unheard).
    for (Slot& slot : m_slots) {
        slot.target = 0.0F;
        slot.fade = 0.0F;
    }
    m_sounding = false;
    // The VST: let go now.
    Held& h = m_external;
    if (h.held) {
        for (const int note : h.notes) queue(h.pending, h.pendingCount, noteOff(note));
        h.held = false;
    }
    h.sounding = false;
    h.fade = 0.0F;
    h.target = 0.0F;
    h.quiet = 0; // (it runs on, unheard, to take the note-offs)
    h.silent = 0;
    // (Its strip's effects are reset by the engine's panic, as every plugin.)
}

void SustainPads::pressAgain() noexcept
{
    Held& h = m_external;
    if (!h.held || !h.sounding) return; // (off, or fading out: pressed afresh when switched on)
    for (const int note : h.notes) queue(h.pending, h.pendingCount, noteOff(note));
    for (const int note : h.notes) queue(h.pending, h.pendingCount, noteOn(note));
    h.silent = 0;
}

void SustainPads::renderInstrument(INode& instrument, int frames, const TimeInfo& time) noexcept
{
    Held& h = m_external;
    const auto n = static_cast<std::size_t>(frames);
    std::fill_n(m_slotLeft.begin(), n, 0.0F);
    std::fill_n(m_slotRight.begin(), n, 0.0F);
    instrument.process(std::span<const MidiEvent>(h.pending.data(), h.pendingCount),
                       AudioBlock{.left = m_slotLeft.data(), .right = m_slotRight.data(), .frames = frames}, time);
    h.pendingCount = 0;

    // The watchdog: silent while it should sound (faded in, the chord held)
    // for a while, it is pressed again; at most every few seconds (a sound
    // that has died away is not pressed over and over).
    h.sinceWatch = std::min(h.sinceWatch + static_cast<int64_t>(n), kIdle);
    float loudest = 0.0F;
    for (std::size_t i = 0; i < n; ++i) loudest = std::max({loudest, std::abs(m_slotLeft.at(i)), std::abs(m_slotRight.at(i))});
    if (h.sounding && h.held && h.fade >= 1.0F && loudest < kSilence) {
        h.silent += static_cast<int64_t>(n);
        if (h.silent >= static_cast<int64_t>(kSilentSeconds * m_rate) && h.sinceWatch >= static_cast<int64_t>(kWatchEverySeconds * m_rate)) {
            pressAgain();
            h.sinceWatch = 0;
        }
    } else {
        h.silent = 0;
    }
    mixFaded(h.fade, h.target, n);
    if (h.fade <= 0.0F && h.target <= 0.0F) {
        // Faded out: let go of its notes now (unheard), and let it ring away.
        if (h.held) {
            for (const int note : h.notes) queue(h.pending, h.pendingCount, noteOff(note));
            h.held = false;
        }
        h.quiet = std::min(h.quiet + static_cast<int64_t>(n), kIdle);
    }
}

void SustainPads::process(const AudioBlock& to, float gain, const TimeInfo& time) noexcept
{
    if (!m_prepared.load(std::memory_order_acquire) || to.frames <= 0) return;
    INode* instrument = m_instrument.acquire();
    if (instrument != m_instrumentSeen) {
        // Another instrument (or none): it starts afresh, holding nothing.
        m_instrumentSeen = instrument;
        m_external = Held{};
        m_external.quiet = kIdle;
    }
    if (m_cut.exchange(false, std::memory_order_acq_rel)) cutNow(); // (a panic)
    follow();
    // A new sound chosen in the instrument: its chord pressed again.
    if (m_restrike.exchange(false, std::memory_order_acq_rel) && instrument != nullptr) pressAgain();
    // The player's filter, shimmer and motion, onto both pads when they move.
    const std::array<float, 3> sound{m_tone.load(std::memory_order_relaxed), m_shimmer.load(std::memory_order_relaxed),
                                     m_motion.load(std::memory_order_relaxed)};
    if (sound != m_soundSeen) {
        for (Slot& slot : m_slots) {
            if (slot.preset >= 0) applySound(slot);
        }
        m_soundSeen = sound;
    }
    // Its strip's effects (a reverb, a delay...), in order.
    EffectChain* const effects = m_effects.acquire();

    // Its meter is its own level (as a strip's): the master fader is not on it.
    const float own = m_volume.load(std::memory_order_relaxed) * kHeadroom;
    const float master = std::max(gain, 0.0F);
    const Held& h = m_external;
    const bool instrumentRunning = instrument != nullptr
                                   && (h.fade > 0.0F || h.target > 0.0F || h.pendingCount > 0
                                       || h.quiet < static_cast<int64_t>(kTailSeconds * m_rate));
    const bool anyRunning = instrumentRunning || std::ranges::any_of(m_slots, [this](const Slot& s) { return running(s); });
    if (!anyRunning) { // (silence: nothing to add, and the volume's glide has nothing to glide)
        m_effects.release();
        m_instrument.release();
        m_rms.store(0.0F, std::memory_order_relaxed);
        return;
    }
    float peak = 0.0F;
    double sum = 0.0;
    for (int done = 0; done < to.frames; done += m_maxBlock) {
        const int frames = std::min(m_maxBlock, to.frames - done);
        const auto n = static_cast<std::size_t>(frames);
        std::fill_n(m_mixLeft.begin(), n, 0.0F);
        std::fill_n(m_mixRight.begin(), n, 0.0F);
        for (Slot& slot : m_slots) {
            if (running(slot)) renderSlot(slot, frames);
        }
        if (instrumentRunning) renderInstrument(*instrument, frames, time);
        if (effects != nullptr) {
            const AudioBlock block{.left = m_mixLeft.data(), .right = m_mixRight.data(), .frames = frames};
            for (const auto& effect : effects->nodes) effect->process({}, block, time);
        }
        // Its volume (gliding), then the soft top: never over full scale, however hot its effects run.
        const std::span<float> mixLeft = std::span<float>(m_mixLeft).first(n);
        const std::span<float> mixRight = std::span<float>(m_mixRight).first(n);
        m_gain.scale(mixLeft, mixRight, own, m_rate);
        for (std::size_t i = 0; i < n; ++i) {
            const float l = softTop(m_mixLeft.at(i));
            const float r = softTop(m_mixRight.at(i));
            m_mixLeft.at(i) = l;
            m_mixRight.at(i) = r;
            peak = std::max({peak, std::abs(l), std::abs(r)});
            sum += 0.5 * ((static_cast<double>(l) * l) + (static_cast<double>(r) * r));
        }
        // Then the master fader (in the mix), gliding too.
        const auto at = static_cast<std::size_t>(done);
        const auto total = static_cast<std::size_t>(to.frames);
        m_masterGain.addInto(std::span<float>(to.left, total).subspan(at, n), std::span<float>(to.right, total).subspan(at, n),
                             mixLeft, mixRight, master, m_rate);
    }
    m_effects.release();
    m_instrument.release();
    float held = m_peak.load(std::memory_order_relaxed);
    while (peak > held && !m_peak.compare_exchange_weak(held, peak, std::memory_order_relaxed)) {
    }
    m_rms.store(static_cast<float>(std::sqrt(sum / static_cast<double>(to.frames))), std::memory_order_relaxed);
}

} // namespace gigchain::engine
