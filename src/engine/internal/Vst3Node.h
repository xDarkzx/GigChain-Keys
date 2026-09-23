#pragma once

#include "INode.h"

#include "openstage/core/Error.h"

#include <QString>

#include <memory>

namespace openstage::engine {

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
    static core::Result<std::shared_ptr<Vst3Node>> load(const QString& bundlePath, double sampleRate, int maxBlock);

    Vst3Node(Token, std::unique_ptr<Impl> impl);
    ~Vst3Node() override; // main thread

    void prepare(double sampleRate, int maxBlock) override;                    // main thread
    void process(std::span<const MidiEvent> events, AudioBlock io) override;   // audio thread

    [[nodiscard]] QString name() const;
    [[nodiscard]] bool isInstrument() const;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace openstage::engine
