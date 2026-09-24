#pragma once

#include "openstage/core/Ids.h"
#include "openstage/core/Model.h"
#include "openstage/engine/EngineTypes.h"
#include "openstage/engine/IPluginEditor.h"

#include <memory>

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
    // Peak since the previous call for this channel (then reset) and current RMS.
    [[nodiscard]] virtual LevelReading channelLevel(const core::ChannelId& id) = 0;
    [[nodiscard]] virtual float cpuLoad() const = 0;
    [[nodiscard]] virtual bool midiActivity() const = 0;

    // Fast paths for mixer moves. Unknown ids and non-finite values are ignored;
    // volumes are clamped to core::limits.
    virtual void setChannelVolume(const core::ChannelId& id, double volumeDb) = 0;
    virtual void setChannelPan(const core::ChannelId& id, double pan) = 0;
    virtual void setChannelMute(const core::ChannelId& id, bool mute) = 0;
    virtual void setChannelSolo(const core::ChannelId& id, bool solo) = 0;
    virtual void setMasterVolume(double volumeDb) = 0;
    [[nodiscard]] virtual double masterVolume() const = 0;

    // Plays a note as if it came from the keyboard (on-screen keyboard,
    // auditioning). velocity 0 = note off. Out-of-range values are ignored.
    virtual void injectNote(int midiChannel, int note, int velocity) = 0;

    // Main thread, regularly (the UI polls ~30 Hz): housekeeping, logging of
    // anything the audio thread counted, device recovery. Returns messages the
    // user should see (each is also logged).
    virtual std::vector<QString> poll() = 0;

    // The instrument plugin's own editor for a channel of the current patch.
    // nullptr when the channel has no instrument or the plugin has no editor;
    // an error when opening it failed (also logged).
    virtual core::Result<std::unique_ptr<IPluginEditor>> createEditor(const core::ChannelId& id) = 0;

    // The editor of any installed plugin, loaded on its own (not playing).
    // Used to take pictures of plugins. nullptr when it has no editor.
    virtual core::Result<std::unique_ptr<IPluginEditor>> createEditorForPlugin(const QString& pluginId) = 0;

    // One line describing the audio setup, e.g. "Scarlett Solo · WASAPI · 5.3 ms".
    [[nodiscard]] virtual QString statusText() const = 0;

protected:
    IEngine() = default;
};

} // namespace openstage::engine
