#pragma once

#include "GlidingGain.h"
#include "GraphExchange.h"
#include "INode.h"
#include "PadsPro.h"
#include "gigchain/engine/EngineTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace gigchain::engine {

// Sustain Pads (Sunday Keys' Tonic): a Pads Pro pad holding root, fifth and
// octave of one key under the song. Two pads take turns: a new key or pad
// starts on the other one while this one fades out (equal power, no gap or
// bump), over the fade time; switched off, it fades out. Settings come from
// the main thread at any time; process() runs on the audio thread and never
// allocates, locks or logs.
class SustainPads
{
public:
    // Root, fifth and octave of `key` (0 C .. 11 B), the root between G2 and
    // F#3, moved by `octave` (-1..+1). Out of range values are kept in range.
    [[nodiscard]] static std::array<int, 3> chord(int key, int octave) noexcept;

    // Main thread, while the audio thread does not run process() (before
    // the first prepare, process() plays nothing).
    void prepare(double sampleRate, int maxBlock);

    // Any thread.
    void set(const SustainPadsSettings& settings) noexcept;
    [[nodiscard]] int outputPair() const noexcept { return m_pair.load(std::memory_order_relaxed); }
    // Its own level since the last call (peak held, RMS of the last block),
    // at its volume (not the master fader).
    [[nodiscard]] LevelReading level() noexcept;

    // Main thread. A VST instrument holds the chord instead of Pads Pro
    // (prepared for this rate and block); nullptr: Pads Pro again. One
    // instance: a new key is pressed before the old one is let go, so its
    // own release carries it over. The pad and the filter, shimmer and
    // motion are then the instrument's own (set in its window).
    void setInstrument(std::shared_ptr<INode> instrument) { m_instrument.publish(std::move(instrument)); }
    // Its strip's effects (a reverb, a delay, an EQ...), in order, after the
    // pad (Pads Pro or a VST) and before its fader; each prepared for this
    // rate and block. Main thread.
    struct EffectChain
    {
        std::vector<std::shared_ptr<INode>> nodes;
    };
    void setEffects(std::vector<std::shared_ptr<INode>> effects)
    {
        m_effects.publish(effects.empty() ? nullptr : std::make_shared<EffectChain>(EffectChain{.nodes = std::move(effects)}));
    }
    // Main thread, now and then: frees an instrument or effects taken out.
    void collectGarbage()
    {
        m_instrument.collectGarbage();
        m_effects.collectGarbage();
    }
    // Any thread: a new sound was chosen in the instrument. The chord it
    // holds is pressed again (let go, then pressed) on the next block: a
    // synth plays a new sound on new notes only (or stops the old ones).
    void restrike() noexcept { m_restrike.store(true, std::memory_order_release); }
    // Any thread (a panic): off, and silent at once (no fade, no echo or
    // reverb left ringing); its notes let go. Set on again to play.
    void cut() noexcept
    {
        m_active.store(false, std::memory_order_release);
        m_cut.store(true, std::memory_order_release);
    }

    // Audio thread: adds the pads into `to`, at `gain` on top of its volume
    // (the master fader, when it plays in the mix). `time`: for the instrument.
    void process(const AudioBlock& to, float gain, const TimeInfo& time = {}) noexcept;

private:
    // The VST instrument's chord (audio thread).
    struct Held
    {
        std::array<int, 3> notes{-1, -1, -1};
        bool held = false;
        bool sounding = false; // switched on (else fading out, or quiet)
        float fade = 0.0F;
        float target = 0.0F;
        int64_t quiet = 0;
        std::array<MidiEvent, 12> pending{}; // (room for a new sound and a new key in one block)
        int64_t silent = 0;                   // samples it has been silent while it should sound
        int64_t sinceWatch = std::numeric_limits<int64_t>::max() / 2; // since the watchdog last pressed it again
        std::size_t pendingCount = 0;
    };
    void followInstrument(bool active, const std::array<int, 3>& notes) noexcept;
    // The held chord let go and pressed again (a new sound in the instrument).
    void pressAgain() noexcept;
    // Silent at once, everything let go (cut()).
    void cutNow() noexcept;
    void renderInstrument(INode& instrument, int frames, const TimeInfo& time) noexcept;
    // m_slotLeft/m_slotRight into the mix, fading `fade` toward `target` (equal power).
    void mixFaded(float& fade, float target, std::size_t frames) noexcept;

    struct Slot
    {
        pads::Synth synth;
        std::array<int, 3> notes{-1, -1, -1}; // what it holds (-1: nothing)
        int preset = -1;
        float fade = 0.0F;   // 0 silent .. 1 in full (its gain is equal power on this)
        float target = 0.0F; // where the fade is going
        bool held = false;   // its notes are down
        int64_t quiet = 0;   // samples since it faded out (it runs on a while to let its tail go)
        std::array<MidiEvent, 6> pending{}; // notes to let go and press, at its next block
        std::size_t pendingCount = 0;
    };

    // Audio thread.
    void follow() noexcept;
    void start(Slot& slot, const std::array<int, 3>& notes, int preset) noexcept;
    void applySound(Slot& slot) noexcept;
    [[nodiscard]] bool running(const Slot& slot) const noexcept;
    void renderSlot(Slot& slot, int frames) noexcept;

    std::atomic<bool> m_active{false};
    std::atomic<int> m_key{0};
    std::atomic<int> m_preset{0};
    std::atomic<int> m_octave{0};
    std::atomic<float> m_fadeSeconds{4.0F};
    std::atomic<float> m_tone{0.5F};
    std::atomic<float> m_shimmer{0.0F};
    std::atomic<float> m_motion{0.5F};
    std::atomic<float> m_volume{0.5F}; // linear
    std::atomic<int> m_pair{0};
    std::atomic<float> m_peak{0.0F};
    std::atomic<float> m_rms{0.0F};
    std::atomic<bool> m_prepared{false};
    std::atomic<bool> m_restrike{false};
    std::atomic<bool> m_cut{false};

    // Audio thread.
    std::array<Slot, 2> m_slots;
    int m_live = 0;         // the slot playing (or last played)
    bool m_sounding = false; // switched on and started
    std::array<float, 3> m_soundSeen{-1.0F, -1.0F, -1.0F}; // tone, shimmer, motion as last given the pads
    HazardExchange<INode> m_instrument; // a VST instrument in Pads Pro's place (null: none)
    INode* m_instrumentSeen = nullptr;  // audio thread: the one it last played (a new one starts afresh)
    Held m_external;
    HazardExchange<EffectChain> m_effects; // its strip's effects (null: none)
    GlidingGain m_gain;       // its volume
    GlidingGain m_masterGain; // the master fader (in the mix)
    double m_rate = 48000.0;
    int m_maxBlock = 0;
    std::vector<float> m_slotLeft, m_slotRight; // one slot's block
    std::vector<float> m_mixLeft, m_mixRight;   // both slots, before the volume
};

} // namespace gigchain::engine
