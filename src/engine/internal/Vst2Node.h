#pragma once

#include "PluginNode.h"

#include <QString>

#include <cstdint>
#include <memory>

namespace gigchain::engine {

// One VST2 plugin instance as a graph node: the only unit that talks to a
// VST2 plugin (through our own definitions, Vst2Abi.h). The order follows
// Audacity 4's VSTWrapper (studied, not copied): load the library, find its
// entry point, give it the sample rate and block size, open it, give them
// again (some plugins ignore them before opening), resume and start
// processing; teardown in reverse, on the main thread, after the audio
// thread stopped using the node.
class Vst2Node final : public PluginNode
{
    struct Token
    {
        explicit Token() = default;
    };

public:
    struct Impl;

    // What a scan needs to know of a plugin (it is opened, read and closed).
    struct Info
    {
        QString name;
        QString vendor;
        QString version;
        bool instrument = false;
        int32_t uniqueId = 0;
    };
    // Main thread (or the scanner). Every failure with its cause, not logged.
    static core::Result<Info> readInfo(const QString& path);

    // Main thread. Loads the plugin at `path` ready to process at
    // `sampleRate` / `maxBlock`. Every failure is returned with its cause and logged.
    // (PluginNode::load opens a VST2 plugin with it.)
    static core::Result<std::shared_ptr<Vst2Node>> open(const QString& path, double sampleRate, int maxBlock);

    Vst2Node(Token, std::unique_ptr<Impl> impl);
    ~Vst2Node() override; // main thread
    Vst2Node(const Vst2Node&) = delete;
    Vst2Node& operator=(const Vst2Node&) = delete;
    Vst2Node(Vst2Node&&) = delete;
    Vst2Node& operator=(Vst2Node&&) = delete;

    core::Result<void> prepare(double sampleRate, int maxBlock) override;                            // main thread
    void process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time) override; // audio thread
    void queueParameter(uint32_t id, double value, int32_t sampleOffset) noexcept override;         // audio thread
    [[nodiscard]] bool holdsNotes() const noexcept override;                                         // audio thread
    [[nodiscard]] double parameterValue(uint32_t id) const override;                                 // main thread

    [[nodiscard]] std::vector<Parameter> parameters() const override;
    [[nodiscard]] std::optional<uint32_t> takeTouchedParameter() override;
    Problems takeProblems() override;
    void releaseAllNotes() override;
    // Its settings: the plugin's own "chunk" when it gives one, else every
    // parameter's value; compressed, tagged as VST2.
    [[nodiscard]] core::Result<QByteArray> saveEncodedState() const override;
    core::Result<void> restoreEncodedState(const QByteArray& bytes) override;
    bool takeEdited() override;

    [[nodiscard]] QString bundlePath() const override;
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isInstrument() const override;

protected:
    core::Result<std::unique_ptr<IPluginEditor>> makeEditor(const std::shared_ptr<PluginNode>& self) override;

private:
    static core::Result<std::shared_ptr<Vst2Node>> loadUnlogged(const QString& path, double sampleRate, int maxBlock);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gigchain::engine
