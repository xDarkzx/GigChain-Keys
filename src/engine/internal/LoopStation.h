#pragma once

#include "GraphExchange.h"

#include "gigchain/engine/EngineTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace gigchain::engine {

// A stereo recording of a fixed length, allocated (zeroed) on the main thread.
struct LoopTake
{
    explicit LoopTake(int64_t frames) : left(static_cast<std::size_t>(frames)), right(static_cast<std::size_t>(frames)) {}
    [[nodiscard]] int64_t frames() const { return static_cast<int64_t>(left.size()); }
    std::vector<float> left;
    std::vector<float> right;
};

// One loop's recordings: the base (sized for the longest loop while it
// records) and, once its length is known, room for the layers recorded on
// top (up to LoopStation::kMaxLayers; fewer for a long loop).
struct LoopData
{
    std::shared_ptr<LoopTake> base;
    std::vector<std::shared_ptr<LoopTake>> layers; // empty until the length is known
    // Layers (bit n = layer n) replaced here by fresh, silent buffers after
    // an undo (LoopStation::dirtyLayers).
    uint32_t freshMask = 0;
};

// Where loops may start and stop: grid lines at origin + k * unit (samples).
struct LoopGrid
{
    int64_t origin = 0;
    double unit = 0.0;

    [[nodiscard]] bool valid() const { return unit >= 1.0; }
    // The first grid line at or after `sample`.
    [[nodiscard]] int64_t next(int64_t sample) const;
    // The last grid line at or before `sample`.
    [[nodiscard]] int64_t previous(int64_t sample) const;
};

// One loop's state for the main thread.
struct LoopReading
{
    LoopState state = LoopState::Empty;
    int64_t length = 0;   // frames, 0 until the loop is closed
    int64_t position = 0; // where it plays (or how much is recorded, while recording)
    int layers = 0;
    int64_t wait = 0; // frames until what it waits for (a start, a close) happens; 0 = nothing
};

// The loop pedal: up to kSlots loops, one per channel, each recording a
// channel's sound and playing it back in a loop, with layers on top.
//
// Main thread: gives each slot its buffers (LoopData, through a hazard
// exchange: never freed while the audio thread uses them), posts commands,
// reads the state. Audio thread, once per block: beginBlock() applies the
// commands (on the next grid line: a bar, or a quarter of the first loop
// when free), record() takes each strip's sound, play() adds every loop to
// the mix, endBlock() moves on. Never allocates, locks or logs.
class LoopStation
{
public:
    static constexpr int kSlots = 32;
    static constexpr int kMaxLayers = 8;

    // ---- Main thread
    // A slot's buffers (null: none). Its state must be Empty when the base changes.
    void setData(int slot, std::shared_ptr<LoopData> buffers);
    [[nodiscard]] const LoopData* data(int slot) const;
    // Queued for the audio thread (dropped, and false, when 16 are waiting).
    bool post(int slot, LoopCommand command);
    // Commands posted and not yet taken by the audio thread.
    [[nodiscard]] bool pending(int slot) const;
    // Synced: loops start and stop on bar lines. Free: the first loop is
    // recorded press to press and sets the grid for the others.
    void setSync(bool on) { m_sync.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool sync() const { return m_sync.load(std::memory_order_relaxed); }
    [[nodiscard]] LoopReading read(int slot) const;
    // Once each: the loop closed and wants its layers; it ran out of room;
    // a layer was asked for with all layers used.
    bool takeNeedsLayers(int slot);
    bool takeFull(int slot);
    bool takeNoLayerLeft(int slot);
    // Layers undone (bit n = layer n) whose buffers still hold their sound:
    // the main thread gives fresh ones (LoopData::freshMask) before they are
    // recorded on again.
    [[nodiscard]] uint32_t dirtyLayers(int slot) const;
    // The grid free loops keep to (for the tempo taken from the first loop).
    [[nodiscard]] LoopGrid freeGrid() const;
    void collectGarbage();

    // ---- Audio thread
    // `bars`: the bar grid (synced); invalid when there is no tempo.
    void beginBlock(int64_t blockStart, int frames, const LoopGrid& bars) noexcept;
    // A slot's channel sound for this block (post-fader), `frames` long.
    void record(int slot, const float* left, const float* right, int frames) noexcept;
    // Adds every sounding loop to `left`/`right`.
    void play(float* left, float* right, int frames) noexcept;
    void endBlock() noexcept;

private:
    struct Segment
    {
        LoopState state = LoopState::Empty;
        int from = 0; // block samples [from, to)
        int to = 0;
        int64_t index = 0; // the base (recording) or loop position at `from`
        int64_t done = 0;  // frames of the layer being recorded, at `from`
        int layers = 0;    // committed layers during it (the one being recorded is the next)
    };

    // Commands from the main thread: a small single-producer queue.
    struct Commands
    {
        static constexpr uint32_t kSize = 17;
        std::array<LoopCommand, kSize> items{};
        std::atomic<uint32_t> head{0};
        std::atomic<uint32_t> tail{0};
    };

    struct Slot
    {
        HazardExchange<LoopData> data;
        Commands commands;
        // Audio thread.
        LoopState state = LoopState::Empty;
        LoopState target = LoopState::Empty; // what the scheduled switch leads to
        int64_t switchAt = -1;               // absolute sample of the scheduled switch, -1 = none
        bool commitOnSwitch = false;         // the layer being recorded is kept at the switch
        int64_t written = 0;                 // base frames recorded
        int64_t recordStart = 0;             // sample the base recording started at
        int64_t length = 0;
        int64_t position = 0;                // loop position at the start of the block
        int layers = 0;                      // committed layers
        int64_t overdubDone = 0;             // frames of the layer being recorded
        bool recordedThisBlock = false;
        bool closed = false; // the loop closed in this block
        uint32_t dirty = 0;  // undone layers still holding their sound
        const LoopData* seen = nullptr; // the buffers last acquired
        LoopData* current = nullptr;         // acquired for the block
        std::array<Segment, 2> segments{};
        int segmentCount = 0;
        // To the main thread.
        std::atomic<LoopState> outState{LoopState::Empty};
        std::atomic<int64_t> outLength{0};
        std::atomic<int64_t> outPosition{0};
        std::atomic<int> outLayers{0};
        std::atomic<bool> needsLayers{false};
        std::atomic<bool> full{false};
        std::atomic<bool> noLayerLeft{false};
        std::atomic<int64_t> outWait{0};
        std::atomic<uint32_t> outDirty{0};
    };

    // Audio thread.
    static void apply(Slot& slot, LoopCommand command, int64_t blockStart, const LoopGrid& lines) noexcept;
    void plan(Slot& slot, int64_t blockStart, int frames, const LoopGrid& lines) noexcept;
    // The grid in force: the bars (synced) or the first free loop's.
    [[nodiscard]] LoopGrid gridFor(const LoopGrid& bars) const noexcept;
    [[nodiscard]] static bool layersReady(const Slot& slot) noexcept;
    // A layer can be recorded now: its buffer is there and silent.
    [[nodiscard]] static bool canLayer(const Slot& slot) noexcept;
    [[nodiscard]] static int64_t capacity(const Slot& slot) noexcept;

    std::array<Slot, kSlots> m_slots;
    std::atomic<bool> m_sync{true};
    int64_t m_blockStart = 0; // audio thread
    int m_frames = 0;
    // Free mode: the first loop's grid (audio thread; read by freeGrid()).
    std::atomic<int64_t> m_freeOrigin{0};
    std::atomic<double> m_freeUnit{0.0};
};

} // namespace gigchain::engine
