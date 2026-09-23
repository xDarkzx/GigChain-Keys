#pragma once

#include "openstage/core/Ids.h"
#include "openstage/core/Model.h"
#include "openstage/engine/EngineTypes.h"

#include <vector>

namespace openstage::engine {

// Everything the UI may ask of the audio engine. Speaks only in core types
// and plain values: no SDK types ever appear here. The UI thread calls all of
// these; implementations own any thread hand-off to the audio callback.
class IEngine
{
public:
    virtual ~IEngine() = default;
    IEngine(const IEngine&) = delete;
    IEngine& operator=(const IEngine&) = delete;
    IEngine(IEngine&&) = delete;
    IEngine& operator=(IEngine&&) = delete;

    // Makes `patch` the sounding patch. The engine copies what it needs;
    // `patch` does not have to outlive the call.
    virtual void applyPatch(const core::Patch& patch) = 0;

    [[nodiscard]] virtual std::vector<PluginInfo> availablePlugins() const = 0;
    [[nodiscard]] virtual LevelReading channelLevel(const core::ChannelId& id) const = 0;
    [[nodiscard]] virtual float cpuLoad() const = 0;
    [[nodiscard]] virtual bool midiActivity() const = 0;

    // Fast paths for mixer moves. Unknown ids and non-finite values are ignored;
    // volumes are clamped to core::limits.
    virtual void setChannelVolume(const core::ChannelId& id, double volumeDb) = 0;
    virtual void setChannelMute(const core::ChannelId& id, bool mute) = 0;
    virtual void setChannelSolo(const core::ChannelId& id, bool solo) = 0;
    virtual void setMasterVolume(double volumeDb) = 0;
    [[nodiscard]] virtual double masterVolume() const = 0;

protected:
    IEngine() = default;
};

} // namespace openstage::engine
