#pragma once

#include "gigchain/core/Ids.h"
#include "gigchain/core/Model.h"
#include "gigchain/engine/EngineTypes.h"
#include "gigchain/engine/IPluginEditor.h"
#include "gigchain/engine/MidiSetup.h"

#include <QSize>
#include <QStringList>

#include <memory>

#include <vector>

namespace gigchain::engine {

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

    // For an editor that refuses host zoom and resize (every Arturia plugin):
    // if the plugin's own window size setting has a step that fits `area`
    // better than `editorSize` (both in physical pixels), the channel's
    // instrument is reloaded at that size with its sound and settings kept,
    // and true is returned (open the editor again). false = nothing to do.
    // An error means the instrument kept playing as it was.
    virtual core::Result<bool> fitEditorToArea(const core::ChannelId& id, QSize editorSize, QSize area) = 0;

    // The editor of any installed plugin, loaded on its own (not playing).
    // Used to take pictures of plugins. nullptr when it has no editor.
    virtual core::Result<std::unique_ptr<IPluginEditor>> createEditorForPlugin(const QString& pluginId) = 0;

    // Settings. Outputs are probed when asked (ASIO drivers can take a moment).
    // Only 44.1, 48, 88.2 and 96 kHz are offered: what instruments are made
    // for (Arturia's Piano V2 crashed the whole process at 192 kHz).
    [[nodiscard]] virtual std::vector<AudioOutput> audioOutputs() const = 0;
    // What is running now: the device by name, the actual rate and buffer.
    [[nodiscard]] virtual AudioSetup audioSetup() const = 0;
    // Switches audio over; plugins keep their state (re-prepared, not
    // reloaded). If `setup` cannot run, the previous setup is restored and
    // the error says why (also logged).
    virtual core::Result<void> setAudioSetup(const AudioSetup& setup) = 0;
    // Every MIDI input plugged in now, as it plays (see resolveMidiInputs).
    // Inputs plugged in or pulled out are picked up by poll().
    [[nodiscard]] virtual std::vector<MidiPort> midiInputs() const = 0;
    [[nodiscard]] virtual MidiSetup midiSetup() const = 0;
    // Reopens the inputs as chosen. An input that fails to open is an error
    // naming it (the others still open).
    virtual core::Result<void> setMidiSetup(const MidiSetup& setup) = 0;

    // One line describing the audio setup, e.g. "Scarlett Solo · WASAPI · 5.3 ms".
    [[nodiscard]] virtual QString statusText() const = 0;

protected:
    IEngine() = default;
};

} // namespace gigchain::engine
