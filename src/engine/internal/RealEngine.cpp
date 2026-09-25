#include "RealEngine.h"

#include "ArturiaWindowSize.h"
#include "EngineLog.h"
#include "PluginCatalog.h"

#include "gigchain/core/Limits.h"

#include <QElapsedTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <set>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

core::Result<std::unique_ptr<IEngine>> createRealEngine(const RealEngineOptions& options)
{
    auto engine = RealEngine::create(options);
    if (!engine) return tl::unexpected(engine.error());
    return std::unique_ptr<IEngine>(std::move(*engine));
}

core::Result<std::unique_ptr<RealEngine>> RealEngine::create(const RealEngineOptions& options)
{
    std::unique_ptr<RealEngine> engine(new RealEngine()); // private constructor; owned immediately

    auto opened = engine->openAudio(options.audio);
    const AudioSetup systemAudio{AudioDriver::System, {}, 0, options.audio.bufferFrames};
    if (!opened && options.audio != systemAudio) {
        // The saved setup failed: fall back to system audio (logged by open()).
        engine->m_pendingNotices.push_back(u"%1 could not be used (%2); using system audio instead"_s.arg(
            options.audio.device.isEmpty() ? u"The saved audio setup"_s : options.audio.device, opened.error().message));
        opened = engine->openAudio(systemAudio);
    }
    if (!opened) {
        return core::fail(core::ErrorCode::DeviceUnavailable, opened.error().message);
    }

    engine->m_preparedRate = engine->m_audio.sampleRate();
    engine->m_preparedBlock = engine->m_audio.maxBlock();
    engine->m_midiSetup = options.midi;
    for (QString& notice : engine->openMidi()) engine->m_pendingNotices.push_back(std::move(notice));
    engine->m_progress = options.progress;
    PluginCatalog::Progress scanProgress;
    if (options.progress) {
        scanProgress = [&options](const QString& plugin, int done, int total) {
            options.progress(LoadStage::ScanningPlugins, plugin, done, total);
        };
    }
    engine->m_plugins = PluginCatalog::scan(
        options.pluginFolder.isEmpty() ? PluginCatalog::standardFolder() : options.pluginFolder, options.pluginCacheFile,
        nullptr, scanProgress);
    return engine;
}

RealEngine::~RealEngine()
{
    // Stop audio before any graph or plugin is destroyed.
    m_midi.close();
    m_audio.close();
    m_exchange.publish(nullptr);
    m_exchange.collectGarbage();
}

std::vector<RealEngine::PlannedSlot> RealEngine::planPatch(const core::SongId& song, const core::Patch& patch)
{
    std::vector<PlannedSlot> plan;
    std::map<QString, int> uses; // plugin id -> how many times this patch used it so far
    const auto keyFor = [&](const QString& role, const QString& pluginId) {
        const int n = uses[role + pluginId]++;
        return song.value() + u'|' + role + u'|' + pluginId + u'#' + QString::number(n);
    };
    for (std::size_t c = 0; c < patch.channels.size(); ++c) {
        const core::Channel& channel = patch.channels[c];
        if (channel.instrument) {
            plan.push_back(PlannedSlot{keyFor(u"i"_s, channel.instrument->pluginId), &*channel.instrument,
                                       static_cast<int>(c), -1});
        }
        for (std::size_t e = 0; e < channel.effects.size(); ++e) {
            if (channel.effects[e].bypass) continue;
            plan.push_back(PlannedSlot{keyFor(u"fx"_s, channel.effects[e].pluginId), &channel.effects[e],
                                       static_cast<int>(c), static_cast<int>(e)});
        }
    }
    return plan;
}

void RealEngine::preload(const core::Setlist& setlist)
{
    // Everything the setlist plays, each shared instance once.
    std::map<QString, const core::PluginSlot*> wanted;
    for (const core::Song& song : setlist.songs) {
        for (const core::Patch& patch : song.patches) {
            for (const PlannedSlot& planned : planPatch(song.id, patch)) wanted.emplace(planned.key, planned.slot);
        }
    }
    // Unload what this setlist does not use, and what must load again with
    // the setlist's settings: different from what it plays, or changed since
    // (the sounding graph keeps its own references until it is replaced).
    collectEdits();
    const std::size_t before = m_nodes.size();
    std::size_t reloads = 0;
    std::erase_if(m_nodes, [&](const auto& entry) {
        const auto it = wanted.find(entry.first);
        if (it != wanted.end()) {
            const auto had = m_nodeStates.find(entry.first);
            const bool same = had != m_nodeStates.end() && had->second == it->second->state;
            if (same && m_editedNodes.count(entry.first) == 0) return false;
            ++reloads;
        }
        m_arturiaLoadedSize.erase(entry.second.get());
        m_nodeStates.erase(entry.first);
        m_editedNodes.erase(entry.first);
        return true;
    });
    if (m_nodes.size() + reloads != before) {
        qCInfo(lcEngine) << "Unloaded" << before - m_nodes.size() - reloads << "plugins the setlist no longer uses";
    }
    if (reloads > 0) qCInfo(lcEngine) << "Reloading" << reloads << "plugins with the setlist's saved settings";

    QElapsedTimer timer;
    timer.start();
    const int total = static_cast<int>(wanted.size());
    int done = 0;
    for (const auto& [key, slot] : wanted) {
        if (m_progress) m_progress(LoadStage::LoadingSounds, slot->displayName, done, total);
        (void)nodeFor(key, *slot, false); // failures are reported by nodeFor
        ++done;
    }
    if (m_progress) m_progress(LoadStage::LoadingSounds, {}, total, total);
    qCInfo(lcEngine) << "Setlist ready:" << total << "plugins in memory, loaded in" << timer.elapsed() << "ms";
}

std::shared_ptr<Vst3Node> RealEngine::nodeFor(const QString& key, const core::PluginSlot& slot, bool announce)
{
    if (const auto it = m_nodes.find(key); it != m_nodes.end()) return it->second;
    if (announce && m_progress) m_progress(LoadStage::LoadingSounds, slot.displayName, 0, 1);
    struct AnnounceDone
    {
        const LoadProgress& progress;
        bool on;
        ~AnnounceDone()
        {
            if (on && progress) progress(LoadStage::LoadingSounds, {}, 1, 1);
        }
    } announceDone{m_progress, announce};

    // Arturia reads its window size when it loads: use the size fitted this
    // session, and remember what this instance starts with.
    std::optional<double> arturiaSize;
    if (const auto prefs = arturiaPrefsFile(slot.pluginId, arturiaDataRoot())) {
        if (const auto fitted = m_arturiaFitted.find(slot.pluginId); fitted != m_arturiaFitted.end()) {
            if (auto written = writeArturiaGuiSize(*prefs, fitted->second); !written) {
                qCWarning(lcEngine).noquote() << written.error().message;
            }
        }
        if (auto size = readArturiaGuiSize(*prefs)) arturiaSize = *size;
        else qCWarning(lcEngine).noquote() << size.error().message;
    }

    QElapsedTimer timer;
    timer.start();
    auto node = Vst3Node::load(slot.pluginId, m_audio.sampleRate(), m_audio.maxBlock());
    if (!node) {
        // Already logged by Vst3Node::load; tell the user too.
        m_pendingNotices.push_back(u"Could not load %1: %2"_s.arg(slot.displayName, node.error().message));
        return nullptr;
    }
    if (!slot.state.isEmpty()) {
        auto state = Vst3Node::State::decode(slot.state);
        auto restored = state ? (*node)->restoreState(*state) : core::Result<void>(tl::unexpected(state.error()));
        if (!restored) {
            // It still plays, at its defaults; the user needs to know.
            const QString problem = u"%1 could not take its saved settings (%2); it plays with its defaults"_s.arg(
                slot.displayName, restored.error().message);
            qCWarning(lcEngine).noquote() << problem;
            m_pendingNotices.push_back(problem);
        }
    }
    (void)(*node)->takeEdited(); // loading and restoring are not edits
    qCInfo(lcEngine).noquote() << "Plugin" << slot.displayName << "ready in" << timer.elapsed() << "ms";
    if (arturiaSize) m_arturiaLoadedSize[node->get()] = *arturiaSize;
    m_nodeStates[key] = slot.state;
    m_editedNodes.erase(key);
    m_nodes.emplace(key, *node);
    return *node;
}

core::Result<void> RealEngine::openAudio(const AudioSetup& setup)
{
    std::optional<DeviceChoice> choice;
    if (!setup.device.isEmpty()) {
        choice = DeviceChoice{setup.driver == AudioDriver::Asio ? AudioApi::Asio : AudioApi::Wasapi, setup.device};
    } else if (setup.driver == AudioDriver::Asio) {
        return core::fail(core::ErrorCode::InvalidData, u"Choose an ASIO device"_s);
    }
    return m_audio.open(choice, setup.bufferFrames, [this](AudioBlock out) { render(out); }, setup.sampleRate);
}

std::vector<AudioOutput> RealEngine::audioOutputs() const
{
    std::vector<AudioOutput> outputs;
    constexpr std::array<unsigned int, 4> kLiveRates{44100, 48000, 88200, 96000};
    for (const AudioDeviceInfo& info : AudioDevice::listOutputs()) {
        std::vector<unsigned int> rates;
        for (const unsigned int rate : info.sampleRates) {
            if (std::find(kLiveRates.begin(), kLiveRates.end(), rate) != kLiveRates.end()) rates.push_back(rate);
        }
        outputs.push_back(AudioOutput{info.api == AudioApi::Asio ? AudioDriver::Asio : AudioDriver::System, info.name,
                                      std::move(rates), info.preferredSampleRate, info.isDefault});
    }
    return outputs;
}

AudioSetup RealEngine::audioSetup() const
{
    return AudioSetup{m_audio.api() == AudioApi::Asio ? AudioDriver::Asio : AudioDriver::System, m_audio.deviceName(),
                      static_cast<unsigned int>(m_audio.sampleRate()), static_cast<unsigned int>(m_audio.maxBlock())};
}

core::Result<void> RealEngine::setAudioSetup(const AudioSetup& setup)
{
    const AudioSetup before = audioSetup();
    AudioSetup previous = before; // as it was asked for, so it reopens the same way
    previous.sampleRate = m_audio.requestedSampleRate();
    previous.bufferFrames = m_audio.requestedBufferFrames();
    if (auto opened = openAudio(setup); !opened) {
        // Logged by open(). Put the working setup back.
        if (auto restored = openAudio(previous); !restored) {
            m_pendingNotices.push_back(u"Could not go back to %1 either: %2"_s.arg(before.device, restored.error().message));
            qCWarning(lcEngine).noquote() << m_pendingNotices.back();
        }
        syncPluginsToDevice();
        return opened;
    }
    syncPluginsToDevice();
    qCInfo(lcEngine).noquote() << "Audio setup changed:" << statusText();
    return {};
}

void RealEngine::syncPluginsToDevice()
{
    const double rate = m_audio.sampleRate();
    const int block = m_audio.maxBlock();
    if (!m_audio.isOpen() || (rate == m_preparedRate && block == m_preparedBlock)) return;

    // No render callback may touch a plugin while it is re-prepared.
    if (auto paused = m_audio.pause(); !paused) {
        m_pendingNotices.push_back(paused.error().message);
        qCWarning(lcEngine).noquote() << m_pendingNotices.back();
        return; // plugins stay as they were; the graph skips blocks larger than they expect
    }
    for (const auto& [key, node] : m_nodes) {
        if (auto prepared = node->prepare(rate, block); !prepared) { // logged by prepare()
            m_pendingNotices.push_back(prepared.error().message);
        }
    }
    m_preparedRate = rate;
    m_preparedBlock = block;
    applyPatch(m_song, m_patch); // a graph sized for the new block
    if (auto resumed = m_audio.resume(); !resumed) {
        m_pendingNotices.push_back(resumed.error().message);
        qCWarning(lcEngine).noquote() << m_pendingNotices.back();
    }
}

std::vector<MidiPort> RealEngine::midiInputs() const
{
    return resolveMidiInputs(MidiInput::listPorts(), m_midiSetup);
}

std::vector<QString> RealEngine::openMidi()
{
    m_midiPorts = MidiInput::listPorts();
    m_lastMidiCheck = std::chrono::steady_clock::now();
    const auto ports = resolveMidiInputs(m_midiPorts, m_midiSetup);
    // The audio thread drains the ports: pause it while they change.
    const bool running = m_audio.isOpen();
    if (running) {
        if (auto paused = m_audio.pause(); !paused) {
            qCWarning(lcEngine).noquote() << paused.error().message;
            return {paused.error().message};
        }
    }
    std::vector<QString> problems = m_midi.openAll(ports); // each logged
    if (running) {
        if (auto resumed = m_audio.resume(); !resumed) {
            qCWarning(lcEngine).noquote() << resumed.error().message;
            problems.push_back(resumed.error().message);
        }
    }
    return problems;
}

core::Result<void> RealEngine::setMidiSetup(const MidiSetup& setup)
{
    m_midiSetup = setup;
    const std::vector<QString> problems = openMidi();
    if (!problems.empty()) {
        QStringList text;
        for (const QString& problem : problems) text << problem;
        return core::fail(core::ErrorCode::DeviceUnavailable, text.join(u"; "_s));
    }
    return {};
}

void RealEngine::watchMidiPorts(std::vector<QString>& notices)
{
    constexpr auto kInterval = std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastMidiCheck < kInterval) return;
    m_lastMidiCheck = now;
    const QStringList present = MidiInput::listPorts();
    if (present == m_midiPorts) return;

    std::vector<QString> changes;
    for (const QString& name : present) {
        if (!m_midiPorts.contains(name)) changes.push_back(u"MIDI input connected: %1"_s.arg(name));
    }
    for (const QString& name : m_midiPorts) {
        if (!present.contains(name)) changes.push_back(u"MIDI input disconnected: %1"_s.arg(name));
    }
    for (QString& change : changes) {
        qCInfo(lcEngine).noquote() << change;
        notices.push_back(std::move(change));
    }
    for (QString& problem : openMidi()) notices.push_back(std::move(problem));
}

void RealEngine::applyPatch(const core::SongId& song, const core::Patch& patch)
{
    QElapsedTimer timer;
    timer.start();
    if (&patch != &m_patch) m_patch = patch;
    m_song = song;
    std::set<Vst3Node*> used;
    m_currentInstruments.clear();
    m_currentEffects.clear();
    const std::vector<PlannedSlot> plan = planPatch(song, patch);
    std::vector<StripSpec> specs;
    specs.reserve(patch.channels.size());
    for (std::size_t c = 0; c < patch.channels.size(); ++c) {
        const core::Channel& channel = patch.channels[c];
        StripSpec spec;
        spec.id = channel.id;
        spec.route = RouteSettings{channel.keyLow, channel.keyHigh, channel.transpose, channel.midiChannel};
        spec.volumeDb = channel.volumeDb;
        spec.pan = channel.pan;
        spec.mute = channel.mute;
        spec.solo = channel.solo;
        for (const PlannedSlot& planned : plan) {
            if (planned.channel != static_cast<int>(c)) continue;
            // Loaded up front by preload(); a plugin just added loads here.
            auto node = nodeFor(planned.key, *planned.slot, true);
            if (!node) continue;
            used.insert(node.get());
            if (planned.effect < 0) {
                m_currentInstruments[channel.id.value()] = node;
                spec.instrument = std::move(node);
            } else {
                auto& effects = m_currentEffects[channel.id.value()];
                effects.resize(channel.effects.size());
                effects[static_cast<std::size_t>(planned.effect)] = node;
                spec.effects.push_back(std::move(node));
            }
        }
        specs.push_back(std::move(spec));
    }

    // Instruments leaving the sound release their notes, so they do not hang
    // when the patch comes back.
    for (const auto& [key, node] : m_nodes) {
        if (used.count(node.get()) == 0) node->releaseAllNotes();
    }
    m_exchange.publish(std::make_shared<RenderGraph>(std::move(specs), m_audio.sampleRate(), m_audio.maxBlock()));
    qCInfo(lcEngine).noquote() << "Patch applied:" << patch.name << "(" << patch.channels.size() << "channels ) in"
                               << timer.elapsed() << "ms";
}

LevelReading RealEngine::channelLevel(const core::ChannelId& id)
{
    RenderGraph* graph = m_exchange.current();
    ChannelStrip* strip = graph != nullptr ? graph->findStrip(id) : nullptr;
    return strip != nullptr ? strip->takeLevel() : LevelReading{};
}

LevelReading RealEngine::masterLevel()
{
    return LevelReading{m_masterPeak.exchange(0.0F, std::memory_order_relaxed),
                        m_masterRms.load(std::memory_order_relaxed)};
}

void RealEngine::setChannelVolume(const core::ChannelId& id, double volumeDb)
{
    if (RenderGraph* graph = m_exchange.current()) {
        if (ChannelStrip* strip = graph->findStrip(id)) strip->setVolumeDb(volumeDb);
    }
}

void RealEngine::setChannelPan(const core::ChannelId& id, double pan)
{
    if (RenderGraph* graph = m_exchange.current()) {
        if (ChannelStrip* strip = graph->findStrip(id)) strip->setPan(pan);
    }
}

void RealEngine::setChannelMute(const core::ChannelId& id, bool mute)
{
    if (RenderGraph* graph = m_exchange.current()) {
        if (ChannelStrip* strip = graph->findStrip(id)) strip->setMute(mute);
    }
}

void RealEngine::setChannelSolo(const core::ChannelId& id, bool solo)
{
    if (RenderGraph* graph = m_exchange.current()) {
        if (ChannelStrip* strip = graph->findStrip(id)) strip->setSolo(solo);
    }
}

void RealEngine::setMasterVolume(double volumeDb)
{
    if (!std::isfinite(volumeDb)) return;
    m_masterDb = std::clamp(volumeDb, core::limits::kMinVolumeDb, core::limits::kMaxVolumeDb);
    m_masterGain.store(m_masterMuted ? 0.0F : dbToGain(m_masterDb), std::memory_order_relaxed);
}

void RealEngine::setMasterMute(bool mute)
{
    m_masterMuted = mute;
    m_masterGain.store(m_masterMuted ? 0.0F : dbToGain(m_masterDb), std::memory_order_relaxed);
}

void RealEngine::injectNote(int midiChannel, int note, int velocity)
{
    if (midiChannel < 1 || midiChannel > 16 || note < 0 || note > 127 || velocity < 0 || velocity > 127) return;
    const auto status = static_cast<uint8_t>((velocity > 0 ? 0x90 : 0x80) | (midiChannel - 1));
    if (!m_injected.push(MidiEvent{status, static_cast<uint8_t>(note), static_cast<uint8_t>(velocity), 0})) {
        m_droppedInjected.fetch_add(1, std::memory_order_relaxed);
    }
}

std::vector<QString> RealEngine::poll()
{
    std::vector<QString> notices;
    notices.swap(m_pendingNotices);

    m_exchange.collectGarbage();
    for (QString& notice : m_audio.poll()) notices.push_back(std::move(notice));
    // A lost device may have come back at another rate or block size.
    syncPluginsToDevice();
    watchMidiPorts(notices);
    rewriteArturiaSizes();
    for (QString& notice : m_pendingNotices) notices.push_back(std::move(notice));
    m_pendingNotices.clear();

    if (const uint64_t dropped = m_midi.takeDropped() + m_droppedInjected.exchange(0); dropped > 0) {
        qCWarning(lcEngine) << "Dropped" << dropped << "MIDI events (input queue full)";
    }
    if (m_midi.takeActivity()) m_midiSeen.store(true, std::memory_order_relaxed);
    else m_midiSeen.store(false, std::memory_order_relaxed);

    if (RenderGraph* graph = m_exchange.current()) {
        if (const uint64_t oversized = graph->takeOversizedBlocks(); oversized > 0) {
            qCWarning(lcEngine) << "Skipped" << oversized << "audio blocks larger than the prepared size";
        }
    }
    for (const auto& [key, node] : m_nodes) {
        const auto problems = node->takeProblems();
        if (!problems.any()) continue;
        qCWarning(lcEngine).noquote() << node->name() << "problems: process failures" << problems.processFailures
                                      << ", dropped events" << problems.droppedEvents << ", oversized blocks"
                                      << problems.oversizedBlocks;
    }
    return notices;
}

core::Result<std::unique_ptr<IPluginEditor>> RealEngine::createEditor(const core::ChannelId& id)
{
    const auto it = m_currentInstruments.find(id.value());
    if (it == m_currentInstruments.end()) {
        return std::unique_ptr<IPluginEditor>(); // no instrument on this channel (a failed load was already reported)
    }
    return Vst3Node::createEditor(it->second);
}

core::Result<std::unique_ptr<IPluginEditor>> RealEngine::createEffectEditor(const core::ChannelId& id, int effect)
{
    const core::Channel* channel = nullptr;
    for (const core::Channel& c : m_patch.channels) {
        if (c.id == id) channel = &c;
    }
    if (channel == nullptr || effect < 0 || static_cast<std::size_t>(effect) >= channel->effects.size()) {
        return core::fail(core::ErrorCode::OutOfRange, u"That effect is no longer in this patch"_s);
    }
    const core::PluginSlot& slot = channel->effects[static_cast<std::size_t>(effect)];
    if (slot.bypass) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is switched off: switch it on to open its window"_s.arg(slot.displayName));
    }
    const auto effects = m_currentEffects.find(id.value());
    if (effects == m_currentEffects.end() || static_cast<std::size_t>(effect) >= effects->second.size()
        || !effects->second[static_cast<std::size_t>(effect)]) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is not loaded (see the message about why)"_s.arg(slot.displayName));
    }
    return Vst3Node::createEditor(effects->second[static_cast<std::size_t>(effect)]);
}

core::Result<bool> RealEngine::fitEditorToArea(const core::ChannelId& id, QSize editorSize, QSize area)
{
    const auto current = m_currentInstruments.find(id.value());
    if (current == m_currentInstruments.end() || editorSize.isEmpty() || area.isEmpty()) return false;
    const std::shared_ptr<Vst3Node> old = current->second;
    const auto prefs = arturiaPrefsFile(old->bundlePath(), arturiaDataRoot());
    const auto loaded = m_arturiaLoadedSize.find(old.get());
    if (!prefs || loaded == m_arturiaLoadedSize.end()) return false; // not a plugin with its own size setting

    const double scale = arturiaScale(loaded->second);
    const QSizeF full(editorSize.width() / scale, editorSize.height() / scale);
    const double best = fitArturiaGuiSize(full, QSizeF(area));
    if (std::abs(best - loaded->second) < 0.05) return false; // already the best fit

    auto state = old->saveState();
    if (!state) {
        qCWarning(lcEngine).noquote() << state.error().message;
        return tl::unexpected(state.error());
    }
    if (auto written = writeArturiaGuiSize(*prefs, best); !written) {
        qCWarning(lcEngine).noquote() << written.error().message;
        return tl::unexpected(written.error());
    }
    auto fresh = Vst3Node::load(old->bundlePath(), m_audio.sampleRate(), m_audio.maxBlock()); // logged
    if (!fresh) return tl::unexpected(fresh.error());
    if (auto restored = (*fresh)->restoreState(*state); !restored) {
        // Keep the old instance playing rather than lose the sound.
        qCWarning(lcEngine).noquote() << restored.error().message;
        return tl::unexpected(restored.error());
    }

    (void)(*fresh)->takeEdited(); // restoring is not an edit
    collectEdits();                // but changes made to the old one still count
    for (auto& [key, node] : m_nodes) {
        if (node == old) node = *fresh;
    }
    m_arturiaLoadedSize.erase(old.get());
    m_arturiaLoadedSize[fresh->get()] = best;
    m_arturiaFitted[old->bundlePath()] = best;
    m_pendingSizeWrites.push_back(PendingSizeWrite{old, *prefs, best});
    applyPatch(m_song, m_patch); // the graph now plays the new instance
    qCInfo(lcEngine).noquote() << old->name() << "reloaded at" << qRound(arturiaScale(best) * 100)
                               << "% to fit the window (" << area.width() << "x" << area.height() << ")";
    return true;
}

void RealEngine::collectEdits()
{
    for (const auto& [key, node] : m_nodes) {
        if (!node->takeEdited()) continue;
        m_editedNodes.insert(key);
        m_unreportedEdit = true;
    }
}

bool RealEngine::takePluginEdits()
{
    collectEdits();
    return std::exchange(m_unreportedEdit, false);
}

std::vector<QString> RealEngine::storePluginStates(core::Setlist& setlist)
{
    QElapsedTimer timer;
    timer.start();
    collectEdits();
    std::vector<QString> problems;
    std::map<QString, std::optional<QByteArray>> stored; // per instance; nullopt = not stored
    const auto stateOf = [&](const QString& key) -> std::optional<QByteArray> {
        if (const auto it = stored.find(key); it != stored.end()) return it->second;
        const auto node = m_nodes.find(key);
        if (node == m_nodes.end()) return stored[key] = std::nullopt; // not loaded: its slot keeps what it had
        auto state = node->second->saveState();
        if (!state) {
            const QString problem =
                u"The settings of %1 could not be saved: %2"_s.arg(node->second->name(), state.error().message);
            qCWarning(lcEngine).noquote() << problem;
            problems.push_back(problem);
            return stored[key] = std::nullopt;
        }
        return stored[key] = state->encode();
    };
    for (core::Song& song : setlist.songs) {
        for (core::Patch& patch : song.patches) {
            for (const PlannedSlot& planned : planPatch(song.id, patch)) {
                const auto bytes = stateOf(planned.key);
                if (!bytes) continue;
                core::Channel& channel = patch.channels[static_cast<std::size_t>(planned.channel)];
                core::PluginSlot& slot = planned.effect < 0
                                             ? *channel.instrument
                                             : channel.effects[static_cast<std::size_t>(planned.effect)];
                slot.state = *bytes;
            }
        }
    }
    qsizetype total = 0;
    int count = 0;
    for (const auto& [key, bytes] : stored) {
        if (!bytes) continue;
        m_nodeStates[key] = *bytes;
        m_editedNodes.erase(key);
        total += bytes->size();
        ++count;
    }
    qCInfo(lcEngine) << "Stored the settings of" << count << "plugins (" << total / 1024 << "KB ) in"
                     << timer.elapsed() << "ms";
    return problems;
}

void RealEngine::rewriteArturiaSizes()
{
    std::erase_if(m_pendingSizeWrites, [](const PendingSizeWrite& write) {
        if (!write.old.expired()) return false;
        if (auto written = writeArturiaGuiSize(write.file, write.guiSize); !written) {
            qCWarning(lcEngine).noquote() << written.error().message;
        }
        return true;
    });
}

core::Result<std::unique_ptr<IPluginEditor>> RealEngine::createEditorForPlugin(const QString& pluginId)
{
    // A separate instance, not in the audio graph; the editor keeps it alive.
    auto node = Vst3Node::load(pluginId, m_audio.sampleRate(), m_audio.maxBlock());
    if (!node) return tl::unexpected(node.error()); // logged by Vst3Node::load
    return Vst3Node::createEditor(*node);
}

QString RealEngine::statusText() const
{
    const QStringList ports = m_midi.openPortNames();
    return u"%1 · %2 · %3 kHz · %4 ms · MIDI: %5"_s.arg(m_audio.deviceName(), apiName(m_audio.api()))
        .arg(m_audio.sampleRate() / 1000.0, 0, 'f', 1)
        .arg(m_audio.latencyMs(), 0, 'f', 1)
        .arg(ports.isEmpty() ? u"none"_s : ports.join(u", "_s));
}

void RealEngine::render(AudioBlock out) noexcept
{
    const auto start = std::chrono::steady_clock::now();

    std::size_t count = m_midi.drain(m_events);
    MidiEvent injected;
    while (count < m_events.size() && m_injected.pop(injected)) m_events[count++] = injected;

    RenderGraph* graph = m_exchange.acquire();
    if (graph != nullptr) {
        graph->render(std::span<const MidiEvent>(m_events.data(), count), out, m_masterGain.load(std::memory_order_relaxed));
    } else {
        std::fill_n(out.left, out.frames, 0.0F);
        std::fill_n(out.right, out.frames, 0.0F);
    }
    m_exchange.release();

    // The master meter: what leaves the app.
    float peak = 0.0F;
    double sumSquares = 0.0;
    for (std::size_t i = 0; i < out.frames; ++i) {
        peak = std::max({peak, std::abs(out.left[i]), std::abs(out.right[i])});
        sumSquares += 0.5 * (static_cast<double>(out.left[i]) * out.left[i] + static_cast<double>(out.right[i]) * out.right[i]);
    }
    float held = m_masterPeak.load(std::memory_order_relaxed);
    while (peak > held && !m_masterPeak.compare_exchange_weak(held, peak, std::memory_order_relaxed)) {
    }
    m_masterRms.store(out.frames > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(out.frames))) : 0.0F,
                      std::memory_order_relaxed);

    // Share of the block's time budget spent rendering, smoothed.
    const double budget = static_cast<double>(out.frames) / m_audio.sampleRate();
    const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const float load = budget > 0.0 ? static_cast<float>(spent / budget) : 0.0F;
    const float previous = m_cpuLoad.load(std::memory_order_relaxed);
    m_cpuLoad.store(previous + 0.1F * (std::min(load, 1.0F) - previous), std::memory_order_relaxed);
}

} // namespace gigchain::engine
