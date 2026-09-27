#pragma once

#include "gigchain/core/Ids.h"
#include "gigchain/core/Model.h"
#include "gigchain/engine/EngineTypes.h"
#include "gigchain/engine/IPluginEditor.h"
#include "gigchain/engine/MidiControl.h"
#include "gigchain/engine/MidiSetup.h"
#include "gigchain/engine/Notice.h"

#include <QSize>
#include <QStringList>

#include <memory>
#include <optional>
#include <utility>
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

    // Makes `patch` of `song` the sounding patch. The engine copies what it
    // needs. A song's patches share their plugins (the same plugin in the
    // same position is one instance), so moving between a song's sections
    // loads nothing and keeps sounds ringing.
    virtual void applyPatch(const core::SongId& song, const core::Patch& patch) = 0;
    // A patch outside any song (tests, previews).
    void applyPatch(const core::Patch& patch) { applyPatch(core::SongId{}, patch); }

    // Loads every plugin the setlist uses, so switching songs and patches
    // never loads anything mid-show, and unloads plugins it no longer uses.
    // Progress goes to the progress handler.
    virtual void preload(const core::Setlist& setlist) = 0;
    virtual void setProgressHandler(LoadProgress handler) = 0;

    // Plugin settings (preset, knobs, loaded sounds), for saving: each loaded
    // plugin's settings go into its slots of `setlist` (core::PluginSlot::
    // state). Slots whose plugin is not loaded (bypassed, failed to load)
    // keep what they had. Returns what could not be stored (each logged).
    // A plugin loads with its slot's settings; preload() reloads a plugin
    // whose settings in the setlist differ from what it plays (reopening a
    // file after changes that were not saved gives the file's sound back).
    virtual std::vector<QString> storePluginStates(core::Setlist& setlist) = 0;
    // True once after any plugin reported a change to its settings (a knob
    // turned or a preset picked in its own window) since the last call.
    virtual bool takePluginEdits() = 0;
    // Plugin instances in memory (for tests and diagnostics).
    [[nodiscard]] virtual std::size_t loadedPluginCount() const = 0;

    [[nodiscard]] virtual std::vector<PluginInfo> availablePlugins() const = 0;
    // The folder the plugins were found in (empty when none was scanned).
    [[nodiscard]] virtual QString pluginFolder() const = 0;
    // Plugins that crashed the app while loading: not loaded again (a patch
    // using one says so) until unblocked ("Try again" in Settings).
    [[nodiscard]] virtual QStringList blockedPlugins() const = 0;
    virtual void unblockPlugin(const QString& pluginId) = 0;
    // Peak since the previous call for this channel (then reset) and current RMS.
    [[nodiscard]] virtual LevelReading channelLevel(const core::ChannelId& id) = 0;
    // The same for everything that leaves the app, after the master fader.
    [[nodiscard]] virtual LevelReading masterLevel() = 0;
    [[nodiscard]] virtual float cpuLoad() const = 0;
    [[nodiscard]] virtual bool midiActivity() const = 0;
    // The keys down (and how hard), wheels and sustain pedal right now, as
    // the instruments hear them (controls learned for switching are left
    // out). For the on-screen keyboard.
    [[nodiscard]] virtual MidiActivity keyboardActivity() const = 0;

    // Fast paths for mixer moves. Unknown ids and non-finite values are ignored;
    // volumes are clamped to core::limits.
    virtual void setChannelVolume(const core::ChannelId& id, double volumeDb) = 0;
    virtual void setChannelPan(const core::ChannelId& id, double pan) = 0;
    virtual void setChannelMute(const core::ChannelId& id, bool mute) = 0;
    virtual void setChannelSolo(const core::ChannelId& id, bool solo) = 0;
    virtual void setMasterVolume(double volumeDb) = 0;
    [[nodiscard]] virtual double masterVolume() const = 0;
    // The master bus belongs to the rig, not to a setlist: its effects work
    // on everything (after the channels mix, before the master fader) and
    // stay loaded whatever setlist is open. Loads what is missing, with each
    // slot's settings; a switched-off effect is not loaded. Problems are
    // reported through poll().
    virtual void setMasterEffects(const std::vector<core::PluginSlot>& effects) = 0;
    // Each loaded master effect's settings into its slot (for saving).
    // Returns what could not be stored (each logged).
    virtual std::vector<QString> storeMasterEffectStates(std::vector<core::PluginSlot>& effects) = 0;
    // True once after a master effect's settings changed in its window.
    virtual bool takeMasterEdits() = 0;
    virtual core::Result<std::unique_ptr<IPluginEditor>> createMasterEffectEditor(int effect) = 0;
    // The safety limiter, last before the output: nothing leaves louder than
    // `ceilingDb` (-24..0). Garbage from a plugin (NaN, infinity) is always
    // silenced. On at -1 dB unless set otherwise.
    virtual void setOutputLimiter(bool enabled, double ceilingDb) = 0;
    // True once if the limiter caught a peak since the last call (the LIM light).
    virtual bool takeLimiterActivity() = 0;

    // Silences everything that leaves the app; the volume is kept for unmute.
    virtual void setMasterMute(bool mute) = 0;
    [[nodiscard]] virtual bool masterMuted() const = 0;

    // Plays a note as if it came from the keyboard (on-screen keyboard,
    // auditioning). velocity 0 = note off. Out-of-range values are ignored.
    virtual void injectNote(int midiChannel, int note, int velocity) = 0;

    // Main thread, regularly (the UI polls ~30 Hz): housekeeping, logging of
    // anything the audio thread counted, device recovery. Returns messages the
    // user should see, each with its level (each is also logged).
    virtual std::vector<Notice> poll() = 0;

    // The instrument plugin's own editor for a channel of the current patch.
    // nullptr when the channel has no instrument or the plugin has no editor;
    // an error when opening it failed (also logged).
    virtual core::Result<std::unique_ptr<IPluginEditor>> createEditor(const core::ChannelId& id) = 0;
    // The same for effect `effect` (its position in the channel's effect
    // list) of a channel of the current patch. A switched-off effect is not
    // loaded, so it has no editor: that is an error saying so.
    virtual core::Result<std::unique_ptr<IPluginEditor>> createEffectEditor(const core::ChannelId& id, int effect) = 0;

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

    // Pedals, pads and buttons that switch songs and patches or stop all
    // sound (learned in Settings). Their messages are taken out of what the
    // instruments hear (a sustain pedal used for "next song" never also
    // sustains); a press is an action, returned once by takeControlActions().
    virtual void setControlTriggers(const ControlTriggers& triggers) = 0;
    virtual std::vector<ControlAction> takeControlActions() = 0;
    // The last Program Change (0-127) received since the previous call, or
    // -1: a keyboard's patch buttons. Taken out of what the instruments hear
    // (it picks a patch, not a plugin preset); a learned trigger wins.
    virtual int takeProgramChange() = 0;
    // For "Learn": the last control pressed (a pedal down, a pad or key hit,
    // a program change) since the previous call; unset when none.
    virtual MidiTrigger takeLearnedTrigger() = 0;
    // Stops every sound now: every plugin is reset (its voices and tails
    // cleared) and held notes are released. Instruments play again at once.
    virtual void panic() = 0;

    // One line describing the audio setup, e.g. "Scarlett Solo · WASAPI · 5.3 ms".
    [[nodiscard]] virtual QString statusText() const = 0;

    // ---- Tempo and click
    // The tempo plugins follow (arpeggiators, delays): 20-400 BPM; anything
    // else is ignored. While a MIDI clock is followed (MidiSetup::followClock)
    // the clock's tempo wins.
    virtual void setTempo(double bpm) = 0;
    // The tempo playing now (the followed clock's, when there is one).
    [[nodiscard]] virtual double tempo() const = 0;
    // The click on every beat (accented on the bar), at `volumeDb` (<= 0).
    virtual void setClick(bool on, double volumeDb) = 0;
    [[nodiscard]] virtual bool clickOn() const = 0;

    // ---- Backing track (one at a time: the current song's)
    // Reads the file in the background (problems come through poll()) and
    // makes it the track; empty = none. Asking for the same file again keeps
    // it (and where it is).
    virtual void setBackingTrack(const QString& path) = 0;
    // Plays from where it is, or pauses.
    virtual void playBackingTrack(bool play) = 0;
    virtual void rewindBackingTrack() = 0;
    virtual void setBackingTrackVolume(double volumeDb) = 0;
    [[nodiscard]] virtual BackingTrackState backingTrack() const = 0;

    // ---- Knobs mapped to plugin parameters (MainStage's screen controls)
    // The parameters of a channel's instrument (target -1) or effect in the
    // current patch that a knob can move; empty when it is not loaded.
    [[nodiscard]] virtual std::vector<PluginParameter> pluginParameters(const core::ChannelId& id, int target) const = 0;
    // For "learn": the parameter last moved in that plugin's own window
    // since the previous call, or nothing.
    virtual std::optional<PluginParameter> takeTouchedParameter(const core::ChannelId& id, int target) = 0;
    // For "learn": the last keyboard knob, fader or pedal moved (any value)
    // since the previous call, as {MIDI channel 1-16, controller 0-127}.
    virtual std::optional<std::pair<int, int>> takeMovedController() = 0;

    // ---- Audio inputs and MIDI outputs, for Settings
    [[nodiscard]] virtual std::vector<AudioInputDevice> audioInputDevices() const = 0;
    // Input channels open now (0 when no input device is chosen or it failed).
    [[nodiscard]] virtual int audioInputChannels() const = 0;
    [[nodiscard]] virtual QStringList midiOutputs() const = 0;

protected:
    IEngine() = default;
};

} // namespace gigchain::engine
