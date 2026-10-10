#pragma once

#include "PluginNode.h"

#include "gigchain/core/Error.h"
#include "gigchain/engine/IPluginEditor.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gigchain::engine {

// One VST3 plugin instance as a graph node. The only unit that touches the
// Steinberg SDK; its types stay behind the Impl pointer.
//
// Lifecycle follows Muse's proven order (studied, not copied): load and
// initialise on the main thread, sync controller to component state,
// setupProcessing -> prepare buffers -> activate main buses -> setActive ->
// setProcessing; teardown is the reverse, on the main thread, after the audio
// thread has stopped using the node.
class Vst3Node final : public PluginNode
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

    core::Result<void> prepare(double sampleRate, int maxBlock) override; // main thread; failure is logged
    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override; // audio thread
    void queueParameter(uint32_t id, double value, int32_t sampleOffset) noexcept override;         // audio thread
    [[nodiscard]] bool holdsNotes() const noexcept override;                                         // audio thread
    // Main thread, regularly: parameters set by mapped knobs, shown in the
    // plugin's own window (the sound changed already, on the audio thread).
    void showParameterChanges() override;
    [[nodiscard]] double parameterValue(uint32_t id) const override; // main thread

    // Main thread: the plugin's parameters a knob can be mapped to (those it
    // marks automatable), as {id, name}.
    [[nodiscard]] std::vector<Parameter> parameters() const override;
    // Main thread: the plugin parameter a MIDI controller (0-127, or VST3's
    // pitch bend / aftertouch numbers) on `midiChannel` (0-15) moves, as the
    // plugin assigned it; nothing when the plugin does not take it.
    [[nodiscard]] std::optional<uint32_t> controllerParameter(int midiChannel, int controller) const override;
    // Main thread: the last parameter the user moved in the plugin's own
    // window since the previous call (for "learn"), or nothing.
    [[nodiscard]] std::optional<uint32_t> takeTouchedParameter() override;

    // Problems the audio thread counted since the last call (it cannot log).
    // The main thread polls this and logs anything non-zero.
    Problems takeProblems() override;

    // Main thread: the next processed block starts with note-offs for every
    // note this plugin is still holding. Used when the node leaves the graph
    // (patch change) so its notes do not hang when it returns.
    void releaseAllNotes() override;

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
    // The same, as the setlist stores it (State::encode / decode).
    [[nodiscard]] core::Result<QByteArray> saveEncodedState() const override;
    core::Result<void> restoreEncodedState(const QByteArray& bytes) override;
    // Main thread. True once after the plugin reported a change of its
    // settings (see ComponentHandler); loading and restoring can report
    // changes too, so call it once after those to start clean.
    bool takeEdited() override;

    [[nodiscard]] QString bundlePath() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isInstrument() const override;

protected:
    core::Result<std::unique_ptr<IPluginEditor>> makeEditor(const std::shared_ptr<PluginNode>& self) override;

private:
    [[nodiscard]] core::Result<State> saveStateUnguarded() const;
    core::Result<void> restoreStateUnguarded(const State& state);
    // load() = this + logging of the outcome (failures as warnings).
    static core::Result<std::shared_ptr<Vst3Node>> loadUnlogged(const QString& bundlePath, double sampleRate,
                                                                int maxBlock);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gigchain::engine
