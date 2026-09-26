#include "RealEngine.h"

#include "EngineLog.h"
#include "PluginCatalog.h"
#include "gigchain/core/Checks.h"

#include "gigchain/core/Limits.h"

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
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
        engine->m_pendingNotices.push_back(Notice::warning(u"%1 could not be used (%2); using system audio instead"_s.arg(
            options.audio.device.isEmpty() ? u"The saved audio setup"_s : options.audio.device, opened.error().message)));
        opened = engine->openAudio(systemAudio);
    }
    if (!opened) {
        return core::fail(core::ErrorCode::DeviceUnavailable, opened.error().message);
    }

    engine->m_preparedRate = engine->m_audio.sampleRate();
    engine->m_preparedBlock = engine->m_audio.maxBlock();
    engine->m_midiSetup = options.midi;
    std::ranges::transform(engine->openMidi(), std::back_inserter(engine->m_pendingNotices), &Notice::warning);
    engine->m_progress = options.progress;
    engine->m_guard = PluginLoadGuard(options.pluginGuardFolder);
    for (const QString& crashed : engine->m_guard.takeCrashed()) {
        engine->m_pendingNotices.push_back(Notice::warning(
            u"%1 crashed the app while loading last time, so it is switched off (Settings > Plugins to try it again)"_s.arg(
                QFileInfo(crashed).completeBaseName())));
    }
    PluginCatalog::Progress scanProgress;
    if (options.progress) {
        scanProgress = [&options](const QString& plugin, int done, int total) {
            options.progress(LoadStage::ScanningPlugins, plugin, done, total);
        };
    }
    engine->m_plugins = PluginCatalog::scan(
        options.pluginFolder.isEmpty() ? PluginCatalog::standardFolder() : options.pluginFolder, options.pluginCacheFile,
        nullptr, scanProgress, &engine->m_guard, options.pluginScanner);
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
    GC_ONLY_MAIN_THREAD();
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
    GC_ONLY_MAIN_THREAD();
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

    auto node = loadWithSettings(slot);
    if (!node) return nullptr;
    m_nodeStates[key] = slot.state;
    m_editedNodes.erase(key);
    m_nodes.emplace(key, node);
    return node;
}

std::shared_ptr<Vst3Node> RealEngine::loadWithSettings(const core::PluginSlot& slot)
{
    GC_ONLY_MAIN_THREAD();
    if (m_guard.isBlocked(slot.pluginId)) {
        const QString problem =
            u"%1 is switched off: it crashed the app while loading before (Settings > Plugins to try it again)"_s.arg(
                slot.displayName);
        qCWarning(lcEngine).noquote() << problem;
        m_pendingNotices.push_back(Notice::warning(problem));
        return nullptr;
    }
    QElapsedTimer timer;
    timer.start();
    // Until it has loaded and taken its settings: a crash here blocks it next start.
    const auto loading = m_guard.loading(slot.pluginId);
    auto node = Vst3Node::load(slot.pluginId, m_audio.sampleRate(), m_audio.maxBlock());
    if (!node) {
        // Already logged by Vst3Node::load; tell the user too.
        m_pendingNotices.push_back(Notice::error(u"Could not load %1: %2"_s.arg(slot.displayName, node.error().message)));
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
            m_pendingNotices.push_back(Notice::warning(problem));
        }
    }
    (void)(*node)->takeEdited(); // loading and restoring are not edits
    qCInfo(lcEngine).noquote() << "Plugin" << slot.displayName << "ready in" << timer.elapsed() << "ms";
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
    GC_ONLY_MAIN_THREAD();
    const AudioSetup before = audioSetup();
    AudioSetup previous = before; // as it was asked for, so it reopens the same way
    previous.sampleRate = m_audio.requestedSampleRate();
    previous.bufferFrames = m_audio.requestedBufferFrames();
    if (auto opened = openAudio(setup); !opened) {
        // Logged by open(). Put the working setup back.
        if (auto restored = openAudio(previous); !restored) {
            m_pendingNotices.push_back(
                Notice::error(u"Could not go back to %1 either: %2"_s.arg(before.device, restored.error().message)));
            qCWarning(lcEngine).noquote() << m_pendingNotices.back().text;
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
    GC_ONLY_MAIN_THREAD();
    const double rate = m_audio.sampleRate();
    const int block = m_audio.maxBlock();
    if (!m_audio.isOpen() || (rate == m_preparedRate && block == m_preparedBlock)) return;

    // No render callback may touch a plugin while it is re-prepared.
    if (auto paused = m_audio.pause(); !paused) {
        m_pendingNotices.push_back(Notice::error(paused.error().message));
        qCWarning(lcEngine).noquote() << m_pendingNotices.back().text;
        return; // plugins stay as they were; the graph skips blocks larger than they expect
    }
    for (const auto* nodes : {&m_nodes, &m_masterNodes}) {
        for (const auto& [key, node] : *nodes) {
            if (auto prepared = node->prepare(rate, block); !prepared) { // logged by prepare()
                m_pendingNotices.push_back(Notice::error(prepared.error().message));
            }
        }
    }
    m_preparedRate = rate;
    m_preparedBlock = block;
    applyPatch(m_song, m_patch); // a graph sized for the new block
    if (auto resumed = m_audio.resume(); !resumed) {
        m_pendingNotices.push_back(Notice::error(resumed.error().message));
        qCWarning(lcEngine).noquote() << m_pendingNotices.back().text;
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
    GC_ONLY_MAIN_THREAD();
    m_midiSetup = setup;
    const std::vector<QString> problems = openMidi();
    if (!problems.empty()) {
        QStringList text;
        for (const QString& problem : problems) text << problem;
        return core::fail(core::ErrorCode::DeviceUnavailable, text.join(u"; "_s));
    }
    return {};
}

void RealEngine::watchMidiPorts(std::vector<Notice>& notices)
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
    for (const QString& change : changes) qCInfo(lcEngine).noquote() << change;
    std::ranges::transform(changes, std::back_inserter(notices), &Notice::info); // news, not a problem
    std::ranges::transform(openMidi(), std::back_inserter(notices), &Notice::warning);
}

void RealEngine::applyPatch(const core::SongId& song, const core::Patch& patch)
{
    GC_ONLY_MAIN_THREAD();
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
    std::vector<std::shared_ptr<INode>> master;
    for (const QString& key : masterKeys()) {
        if (const auto it = m_masterNodes.find(key); it != m_masterNodes.end()) master.push_back(it->second);
    }
    m_exchange.publish(std::make_shared<RenderGraph>(std::move(specs), m_audio.sampleRate(), m_audio.maxBlock(),
                                                     std::move(master)));
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

std::vector<Notice> RealEngine::poll()
{
    GC_ONLY_MAIN_THREAD();
    std::vector<Notice> notices;
    notices.swap(m_pendingNotices);

    m_exchange.collectGarbage();
    std::ranges::move(m_audio.poll(), std::back_inserter(notices));
    // A lost device may have come back at another rate or block size.
    syncPluginsToDevice();
    watchMidiPorts(notices);
    std::ranges::move(m_pendingNotices, std::back_inserter(notices));
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
    GC_ONLY_MAIN_THREAD();
    const auto it = m_currentInstruments.find(id.value());
    if (it == m_currentInstruments.end()) {
        return std::unique_ptr<IPluginEditor>(); // no instrument on this channel (a failed load was already reported)
    }
    return Vst3Node::createEditor(it->second);
}

core::Result<std::unique_ptr<IPluginEditor>> RealEngine::createEffectEditor(const core::ChannelId& id, int effect)
{
    GC_ONLY_MAIN_THREAD();
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

std::vector<QString> RealEngine::masterKeys() const
{
    std::vector<QString> keys;
    std::map<QString, int> uses;
    for (const core::PluginSlot& slot : m_masterSlots) {
        if (slot.bypass) {
            keys.emplace_back();
            continue;
        }
        const int n = uses[slot.pluginId]++;
        keys.push_back(u"master|fx|"_s + slot.pluginId + u'#' + QString::number(n));
    }
    return keys;
}

void RealEngine::setMasterEffects(const std::vector<core::PluginSlot>& effects)
{
    GC_ONLY_MAIN_THREAD();
    QElapsedTimer timer;
    timer.start();
    m_masterSlots = effects;
    const std::vector<QString> keys = masterKeys();
    // Take edits made to instances that are going away before they go.
    (void)takeMasterEdits();
    m_masterEdited = false;
    std::erase_if(m_masterNodes, [&](const auto& entry) {
        return std::find(keys.begin(), keys.end(), entry.first) == keys.end();
    });
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].isEmpty() || m_masterNodes.count(keys[i]) != 0) continue;
        if (auto node = loadWithSettings(m_masterSlots[i])) m_masterNodes.emplace(keys[i], std::move(node));
    }
    applyPatch(m_song, m_patch); // the graph now plays them
    qCInfo(lcEngine) << "Master effects:" << m_masterNodes.size() << "loaded in" << timer.elapsed() << "ms";
}

std::vector<QString> RealEngine::storeMasterEffectStates(std::vector<core::PluginSlot>& effects)
{
    GC_ONLY_MAIN_THREAD();
    std::vector<QString> problems;
    const std::vector<QString> keys = masterKeys();
    for (std::size_t i = 0; i < effects.size() && i < keys.size(); ++i) {
        const auto node = m_masterNodes.find(keys[i]);
        if (node == m_masterNodes.end() || effects[i].pluginId != m_masterSlots[i].pluginId) continue;
        auto state = node->second->saveState();
        if (!state) {
            problems.push_back(u"The settings of %1 could not be saved: %2"_s.arg(effects[i].displayName, state.error().message));
            qCWarning(lcEngine).noquote() << problems.back();
            continue;
        }
        effects[i].state = state->encode();
        m_masterSlots[i].state = effects[i].state;
    }
    return problems;
}

bool RealEngine::takeMasterEdits()
{
    GC_ONLY_MAIN_THREAD();
    for (const auto& [key, node] : m_masterNodes) {
        if (node->takeEdited()) m_masterEdited = true;
    }
    return std::exchange(m_masterEdited, false);
}

core::Result<std::unique_ptr<IPluginEditor>> RealEngine::createMasterEffectEditor(int effect)
{
    GC_ONLY_MAIN_THREAD();
    const std::vector<QString> keys = masterKeys();
    if (effect < 0 || static_cast<std::size_t>(effect) >= keys.size()) {
        return core::fail(core::ErrorCode::OutOfRange, u"That master effect is no longer there"_s);
    }
    const core::PluginSlot& slot = m_masterSlots[static_cast<std::size_t>(effect)];
    if (slot.bypass) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is switched off: switch it on to open its window"_s.arg(slot.displayName));
    }
    const auto node = m_masterNodes.find(keys[static_cast<std::size_t>(effect)]);
    if (node == m_masterNodes.end()) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is not loaded (see the message about why)"_s.arg(slot.displayName));
    }
    return Vst3Node::createEditor(node->second);
}

void RealEngine::setOutputLimiter(bool enabled, double ceilingDb)
{
    m_limiter.setEnabled(enabled);
    m_limiter.setCeilingDb(ceilingDb);
    qCInfo(lcEngine) << "Safety limiter" << (enabled ? "on at" : "off (ceiling") << ceilingDb << "dB";
}

std::size_t RealEngine::takeControlMessages(std::size_t count) noexcept
{
    std::array<MidiTrigger, kControlActionCount> triggers;
    bool any = false;
    for (int i = 0; i < kControlActionCount; ++i) {
        triggers[static_cast<std::size_t>(i)] = MidiTrigger::unpack(m_triggers[static_cast<std::size_t>(i)].load(std::memory_order_relaxed));
        any = any || triggers[static_cast<std::size_t>(i)].isSet();
    }
    std::size_t kept = 0;
    for (std::size_t e = 0; e < count; ++e) {
        const MidiEvent& event = m_events[e];
        if (const MidiTrigger press = learnable(event.status, event.data1, event.data2); press.isSet()) {
            m_learned.store(press.pack(), std::memory_order_relaxed);
        }
        bool consumed = false;
        if (any) {
            for (int i = 0; i < kControlActionCount; ++i) {
                const TriggerMatch match =
                    matchTrigger(triggers[static_cast<std::size_t>(i)], event.status, event.data1, event.data2);
                if (!match.belongs) continue;
                consumed = true; // the instruments never hear a control
                if (match.pressed) m_pressedActions.fetch_or(1U << i, std::memory_order_relaxed);
            }
        }
        // A patch button: it picks a patch (a learned trigger above wins).
        if (const int program = programOf(event.status, event.data1); !consumed && program >= 0) {
            m_program.store(program, std::memory_order_relaxed);
            consumed = true;
        }
        if (!consumed) m_events[kept++] = event;
    }
    return kept;
}

void RealEngine::setControlTriggers(const ControlTriggers& triggers)
{
    GC_ONLY_MAIN_THREAD();
    for (std::size_t i = 0; i < triggers.size(); ++i) m_triggers[i].store(triggers[i].pack(), std::memory_order_relaxed);
}

std::vector<ControlAction> RealEngine::takeControlActions()
{
    GC_ONLY_MAIN_THREAD();
    const uint32_t pressed = m_pressedActions.exchange(0, std::memory_order_relaxed);
    std::vector<ControlAction> actions;
    for (int i = 0; i < kControlActionCount; ++i) {
        if ((pressed & (1U << i)) != 0) actions.push_back(static_cast<ControlAction>(i));
    }
    return actions;
}

int RealEngine::takeProgramChange()
{
    GC_ONLY_MAIN_THREAD();
    return m_program.exchange(-1, std::memory_order_relaxed);
}

MidiTrigger RealEngine::takeLearnedTrigger()
{
    GC_ONLY_MAIN_THREAD();
    return MidiTrigger::unpack(m_learned.exchange(0, std::memory_order_relaxed));
}

void RealEngine::panic()
{
    GC_ONLY_MAIN_THREAD();
    QElapsedTimer timer;
    timer.start();
    // No render callback may touch a plugin while it is reset.
    if (auto paused = m_audio.pause(); !paused) {
        m_pendingNotices.push_back(Notice::error(paused.error().message));
        qCWarning(lcEngine).noquote() << m_pendingNotices.back().text;
    }
    const double rate = m_audio.sampleRate();
    const int block = m_audio.maxBlock();
    for (const auto* nodes : {&m_nodes, &m_masterNodes}) {
        for (const auto& [key, node] : *nodes) {
            node->releaseAllNotes();
            // Deactivate + activate: VST3's reset, clearing voices and tails.
            if (auto prepared = node->prepare(rate, block); !prepared) {
                m_pendingNotices.push_back(Notice::error(prepared.error().message));
            }
        }
    }
    if (m_audio.isOpen()) {
        if (auto resumed = m_audio.resume(); !resumed) {
            m_pendingNotices.push_back(Notice::error(resumed.error().message));
            qCWarning(lcEngine).noquote() << m_pendingNotices.back().text;
        }
    }
    qCWarning(lcEngine) << "Panic: every sound stopped (" << m_nodes.size() + m_masterNodes.size() << "plugins reset in"
                        << timer.elapsed() << "ms )";
}

void RealEngine::collectEdits()
{
    GC_ONLY_MAIN_THREAD();
    for (const auto& [key, node] : m_nodes) {
        if (!node->takeEdited()) continue;
        m_editedNodes.insert(key);
        m_unreportedEdit = true;
    }
}

bool RealEngine::takePluginEdits()
{
    GC_ONLY_MAIN_THREAD();
    collectEdits();
    return std::exchange(m_unreportedEdit, false);
}

std::vector<QString> RealEngine::storePluginStates(core::Setlist& setlist)
{
    GC_ONLY_MAIN_THREAD();
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
                // planPatch only plans an instrument slot for a channel with one.
                GC_IF_FAILED(planned.effect >= 0 || channel.instrument.has_value()) { continue; }
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

core::Result<std::unique_ptr<IPluginEditor>> RealEngine::createEditorForPlugin(const QString& pluginId)
{
    GC_ONLY_MAIN_THREAD();
    // A separate instance, not in the audio graph; the editor keeps it alive.
    if (m_guard.isBlocked(pluginId)) {
        return core::fail(core::ErrorCode::InvalidData, u"This plugin crashed the app while loading before, so it is switched off"_s);
    }
    const auto loading = m_guard.loading(pluginId);
    auto node = Vst3Node::load(pluginId, m_audio.sampleRate(), m_audio.maxBlock());
    if (!node) return tl::unexpected(node.error()); // logged by Vst3Node::load
    return Vst3Node::createEditor(*node);
}

QString RealEngine::statusText() const
{
    const QStringList ports = m_midi.openPortNames();
    return u"%1 Â· %2 Â· %3 kHz Â· %4 ms Â· MIDI: %5"_s.arg(m_audio.deviceName(), apiName(m_audio.api()))
        .arg(m_audio.sampleRate() / 1000.0, 0, 'f', 1)
        .arg(m_audio.latencyMs(), 0, 'f', 1)
        .arg(ports.isEmpty() ? u"none"_s : ports.join(u", "_s));
}

void RealEngine::render(AudioBlock out) noexcept
{
    GC_ONLY_AUDIO_THREAD();
    const auto start = std::chrono::steady_clock::now();

    std::size_t count = m_midi.drain(m_events);
    MidiEvent injected;
    while (count < m_events.size() && m_injected.pop(injected)) m_events[count++] = injected;
    count = takeControlMessages(count);

    RenderGraph* graph = m_exchange.acquire();
    if (graph != nullptr) {
        graph->render(std::span<const MidiEvent>(m_events.data(), count), out, m_masterGain.load(std::memory_order_relaxed));
    } else {
        std::fill_n(out.left, out.frames, 0.0F);
        std::fill_n(out.right, out.frames, 0.0F);
    }
    m_exchange.release();

    // The safety limiter: last before the output.
    if (const double rate = m_audio.sampleRate(); rate != m_limiterRate) {
        m_limiter.setSampleRate(rate);
        m_limiterRate = rate;
    }
    m_limiter.process(out);

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
