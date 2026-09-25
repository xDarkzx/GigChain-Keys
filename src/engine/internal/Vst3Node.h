#pragma once

#include "INode.h"

#include "gigchain/core/Error.h"
#include "gigchain/engine/IPluginEditor.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <memory>

namespace gigchain::engine {

// One VST3 plugin instance as a graph node. The only unit that touches the
// Steinberg SDK; its types stay behind the Impl pointer.
//
// Lifecycle follows Muse's proven order (studied, not copied): load and
// initialise on the main thread, sync controller to component state,
// setupProcessing -> prepare buffers -> activate main buses -> setActive ->
// setProcessing; teardown is the reverse, on the main thread, after the audio
// thread has stopped using the node.
class Vst3Node final : public INode
{
    struct Token
    {
        explicit Token() = default;
    };

public:
    struct Impl;

    // Main thread. Loads the first audio-processor class in the bundle and
    // leaves it active and ready to process at `sampleRate` / `maxBlock`.
    // Every failure is returned with its precise cause and logged.
    static core::Result<std::shared_ptr<Vst3Node>> load(const QString& bundlePath, double sampleRate, int maxBlock);

    Vst3Node(Token, std::unique_ptr<Impl> impl);
    ~Vst3Node() override; // main thread

    core::Result<void> prepare(double sampleRate, int maxBlock) override;      // main thread; failure is logged
    void process(std::span<const MidiEvent> events, AudioBlock io) override;   // audio thread

    // Problems the audio thread counted since the last call (it cannot log).
    // The main thread polls this and logs anything non-zero.
    struct Problems
    {
        uint64_t processFailures = 0;
        uint64_t droppedEvents = 0;
        uint64_t oversizedBlocks = 0;
        [[nodiscard]] bool any() const { return processFailures || droppedEvents || oversizedBlocks; }
    };
    Problems takeProblems();

    // Main thread: the next processed block starts with note-offs for every
    // note this plugin is still holding. Used when the node leaves the graph
    // (patch change) so its notes do not hang when it returns.
    void releaseAllNotes();

    // Main thread. The plugin's own editor, or nullptr when it has none. The
    // editor keeps `node` alive until it is destroyed.
    static core::Result<std::unique_ptr<IPluginEditor>> createEditor(const std::shared_ptr<Vst3Node>& node);

    // A plugin's full state (its sound and settings), as VST3 hosts save it:
    // the component's state and the controller's.
    struct State
    {
        QByteArray component;
        QByteArray controller;

        // How the setlist stores it (core::PluginSlot::state): compressed,
        // with a format tag. decode() rejects anything damaged.
        [[nodiscard]] QByteArray encode() const;
        [[nodiscard]] static core::Result<State> decode(const QByteArray& bytes);
    };
    // Main thread.
    [[nodiscard]] core::Result<State> saveState() const;
    // Main thread, before the node is published to the audio graph.
    core::Result<void> restoreState(const State& state);
    // Main thread. True once after the plugin reported a change of its
    // settings (see ComponentHandler); loading and restoring can report
    // changes too, so call it once after those to start clean.
    bool takeEdited();

    [[nodiscard]] QString bundlePath() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] bool isInstrument() const;

private:
    [[nodiscard]] core::Result<State> saveStateUnguarded() const;
    core::Result<void> restoreStateUnguarded(const State& state);
    // load() = this + logging of the outcome (failures as warnings).
    static core::Result<std::shared_ptr<Vst3Node>> loadUnlogged(const QString& bundlePath, double sampleRate,
                                                                int maxBlock);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gigchain::engine
