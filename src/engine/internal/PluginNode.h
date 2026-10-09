#pragma once

#include "INode.h"

#include "gigchain/core/Error.h"
#include "gigchain/engine/IPluginEditor.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gigchain::engine {

// One plugin instance as a graph node, whatever its format (Vst3Node,
// Vst2Node): what the engine needs of a plugin besides playing it. A
// plugin's id is its file (a .vst3 bundle, or a VST2 library), which says
// its format.
class PluginNode : public INode
{
public:
    // Main thread. Loads the plugin `pluginId` names, ready to process at
    // `sampleRate` / `maxBlock`. Every failure is returned with its cause and logged.
    static core::Result<std::shared_ptr<PluginNode>> load(const QString& pluginId, double sampleRate, int maxBlock);
    // Main thread. The plugin's own editor, or nullptr when it has none. The
    // editor keeps `node` alive until it is destroyed.
    static core::Result<std::unique_ptr<IPluginEditor>> createEditor(const std::shared_ptr<PluginNode>& node);
    // Whether `pluginId` is a VST2 plugin (else VST3).
    [[nodiscard]] static bool isVst2(const QString& pluginId);

    // Main thread: the parameters a knob can be mapped to, as {id, name}.
    struct Parameter
    {
        uint32_t id = 0;
        QString name;
    };
    [[nodiscard]] virtual std::vector<Parameter> parameters() const = 0;
    // Main thread: the parameter a MIDI controller on `midiChannel` (0-15)
    // moves, as the plugin assigned it; nothing when it does not say.
    [[nodiscard]] virtual std::optional<uint32_t> controllerParameter(int /*midiChannel*/, int /*controller*/) const
    {
        return std::nullopt;
    }
    // Main thread: the last parameter moved in the plugin's own window since
    // the previous call (for "learn").
    [[nodiscard]] virtual std::optional<uint32_t> takeTouchedParameter() = 0;
    // Main thread, regularly: knob moves shown in the plugin's window.
    virtual void showParameterChanges() {}

    // Problems the audio thread counted since the last call (it cannot log).
    struct Problems
    {
        uint64_t processFailures = 0;
        uint64_t droppedEvents = 0;
        uint64_t oversizedBlocks = 0;
        [[nodiscard]] bool any() const { return processFailures || droppedEvents || oversizedBlocks; }
    };
    virtual Problems takeProblems() = 0;
    // Main thread: the next block starts with every held note let go.
    virtual void releaseAllNotes() = 0;

    // Main thread. The plugin's sound and settings as the setlist stores them
    // (core::PluginSlot::state: compressed, tagged with the format), and back.
    [[nodiscard]] virtual core::Result<QByteArray> saveEncodedState() const = 0;
    virtual core::Result<void> restoreEncodedState(const QByteArray& bytes) = 0;
    // Main thread: true once after the plugin said its settings changed.
    virtual bool takeEdited() = 0;

    [[nodiscard]] virtual QString bundlePath() const = 0;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual bool isInstrument() const = 0;

protected:
    PluginNode() = default;
    // createEditor's work, for this node (`self` is this node, kept alive by the editor).
    virtual core::Result<std::unique_ptr<IPluginEditor>> makeEditor(const std::shared_ptr<PluginNode>& self) = 0;
};

} // namespace gigchain::engine
