#include "RealEngine.h"

#include "EngineLog.h"
#include "PluginCatalog.h"

#include "openstage/core/Limits.h"

#include <QElapsedTimer>

#include <chrono>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace openstage::engine {

core::Result<std::unique_ptr<IEngine>> createRealEngine(const RealEngineOptions& options)
{
    auto engine = RealEngine::create(options);
    if (!engine) return tl::unexpected(engine.error());
    return std::unique_ptr<IEngine>(std::move(*engine));
}

core::Result<std::unique_ptr<RealEngine>> RealEngine::create(const RealEngineOptions& options)
{
    std::unique_ptr<RealEngine> engine(new RealEngine()); // private constructor; owned immediately

    std::optional<DeviceChoice> choice;
    if (!options.asioDevice.isEmpty()) choice = DeviceChoice{AudioApi::Asio, options.asioDevice};
    RealEngine* self = engine.get();
    auto opened = engine->m_audio.open(choice, options.bufferFrames, [self](AudioBlock out) { self->render(out); });
    if (!opened && choice) {
        // ASIO was requested but failed: fall back to system audio (logged by open()).
        engine->m_pendingNotices.push_back(
            u"%1 could not be used (%2); using system audio instead"_s.arg(options.asioDevice, opened.error().message));
        opened = engine->m_audio.open(std::nullopt, options.bufferFrames, [self](AudioBlock out) { self->render(out); });
    }
    if (!opened) {
        return core::fail(core::ErrorCode::DeviceUnavailable, opened.error().message);
    }

    for (const QString& notice : engine->m_midi.openAll()) engine->m_pendingNotices.push_back(notice);
    engine->m_plugins =
        PluginCatalog::scan(options.pluginFolder.isEmpty() ? PluginCatalog::standardFolder() : options.pluginFolder);
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

std::shared_ptr<Vst3Node> RealEngine::nodeFor(const QString& cacheKey, const core::PluginSlot& slot)
{
    const QString key = cacheKey + u'|' + slot.pluginId;
    if (const auto it = m_nodes.find(key); it != m_nodes.end()) return it->second;

    QElapsedTimer timer;
    timer.start();
    auto node = Vst3Node::load(slot.pluginId, m_audio.sampleRate(), m_audio.maxBlock());
    if (!node) {
        // Already logged by Vst3Node::load; tell the user too.
        m_pendingNotices.push_back(u"Could not load %1: %2"_s.arg(slot.displayName, node.error().message));
        return nullptr;
    }
    qCInfo(lcEngine).noquote() << "Plugin" << slot.displayName << "ready in" << timer.elapsed() << "ms";
    m_nodes.emplace(key, *node);
    return *node;
}

void RealEngine::applyPatch(const core::Patch& patch)
{
    std::set<Vst3Node*> used;
    m_currentInstruments.clear();
    std::vector<StripSpec> specs;
    specs.reserve(patch.channels.size());
    for (const core::Channel& channel : patch.channels) {
        StripSpec spec;
        spec.id = channel.id;
        spec.route = RouteSettings{channel.keyLow, channel.keyHigh, channel.transpose, channel.midiChannel};
        spec.volumeDb = channel.volumeDb;
        spec.mute = channel.mute;
        spec.solo = channel.solo;
        if (channel.instrument) {
            auto node = nodeFor(channel.id.value() + u"|instrument"_s, *channel.instrument);
            used.insert(node.get());
            if (node) m_currentInstruments[channel.id.value()] = node;
            spec.instrument = std::move(node);
        }
        for (std::size_t i = 0; i < channel.effects.size(); ++i) {
            const core::PluginSlot& slot = channel.effects[i];
            if (slot.bypass) continue;
            if (auto node = nodeFor(channel.id.value() + u"|fx"_s + QString::number(i), slot)) {
                used.insert(node.get());
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
    qCInfo(lcEngine).noquote() << "Patch applied:" << patch.name << "(" << patch.channels.size() << "channels )";
}

LevelReading RealEngine::channelLevel(const core::ChannelId& id)
{
    RenderGraph* graph = m_exchange.current();
    ChannelStrip* strip = graph != nullptr ? graph->findStrip(id) : nullptr;
    return strip != nullptr ? strip->takeLevel() : LevelReading{};
}

void RealEngine::setChannelVolume(const core::ChannelId& id, double volumeDb)
{
    if (RenderGraph* graph = m_exchange.current()) {
        if (ChannelStrip* strip = graph->findStrip(id)) strip->setVolumeDb(volumeDb);
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
    m_masterGain.store(dbToGain(m_masterDb), std::memory_order_relaxed);
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

    // Share of the block's time budget spent rendering, smoothed.
    const double budget = static_cast<double>(out.frames) / m_audio.sampleRate();
    const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const float load = budget > 0.0 ? static_cast<float>(spent / budget) : 0.0F;
    const float previous = m_cpuLoad.load(std::memory_order_relaxed);
    m_cpuLoad.store(previous + 0.1F * (std::min(load, 1.0F) - previous), std::memory_order_relaxed);
}

} // namespace openstage::engine
