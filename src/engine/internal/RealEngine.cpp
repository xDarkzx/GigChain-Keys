#include "RealEngine.h"

#include "EngineLog.h"
#include "PluginCatalog.h"
#include "gigchain/core/Chart.h"
#include "gigchain/core/Checks.h"

#include "gigchain/core/Limits.h"
#include "gigchain/platform/PluginFolders.h"

#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

// Strips of earlier patches ringing out at once, at most.
constexpr std::size_t kMaxTails = 8;
// The slowest tempo taken (the fastest is core::limits::kMaxTempo).
constexpr double kMinTempo = 20.0;

} // namespace

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
    const AudioSetup systemAudio{.driver = AudioDriver::System,
                                 .device = {},
                                 .sampleRate = 0,
                                 .bufferFrames = options.audio.bufferFrames,
                                 .inputDevice = {}};
    if (!opened && options.audio != systemAudio) {
        // The saved setup failed: fall back to system audio (logged by open()).
        engine->m_pendingNotices.push_back(Notice::warning(u"%1 could not be used (%2); using system audio until it is there"_s.arg(
            options.audio.device.isEmpty() ? u"The saved audio setup"_s : options.audio.device, opened.error().message)));
        opened = engine->openAudio(systemAudio);
        // The saved device (unplugged when the app started) is taken back
        // when it is plugged in.
        // (Not a driver this system lacks: nothing would ever plug in there.)
        const auto drivers = systemAudioDrivers();
        const bool here = std::ranges::find(drivers, options.audio.driver) != drivers.end();
        if (opened && !options.audio.device.isEmpty() && here) {
            const AudioApi api = options.audio.driver;
            std::optional<DeviceChoice> input;
            if (!options.audio.inputDevice.isEmpty()) input = DeviceChoice{.api = api, .name = options.audio.inputDevice};
            engine->m_audio.standIn(DeviceChoice{.api = api, .name = options.audio.device}, options.audio.sampleRate, input);
        }
    }
    if (!opened) {
        return core::fail(core::ErrorCode::DeviceUnavailable, opened.error().message);
    }
    // Devices plugged in or out: the audio looks again at once.
    engine->m_mediaDevices = std::make_unique<QMediaDevices>();
    RealEngine* self = engine.get();
    QObject::connect(engine->m_mediaDevices.get(), &QMediaDevices::audioOutputsChanged, engine->m_mediaDevices.get(),
                     [self] { self->m_audio.devicesChanged(); });
    QObject::connect(engine->m_mediaDevices.get(), &QMediaDevices::audioInputsChanged, engine->m_mediaDevices.get(),
                     [self] { self->m_audio.devicesChanged(); });

    engine->m_preparedRate = engine->m_audio.sampleRate();
    engine->m_preparedBlock = engine->m_audio.maxBlock();
    engine->m_midiSetup = options.midi;
    engine->m_midiInputs = options.midiInputs;
    std::ranges::transform(engine->openMidi(), std::back_inserter(engine->m_pendingNotices), &Notice::warning);
    std::ranges::transform(engine->applyClockSetup(), std::back_inserter(engine->m_pendingNotices), &Notice::warning);
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
    engine->m_pluginFolder = options.pluginFolder.isEmpty() ? PluginCatalog::standardFolder() : options.pluginFolder;
    engine->m_plugins = PluginCatalog::scan(engine->m_pluginFolder, options.pluginCacheFile, nullptr, scanProgress,
                                            &engine->m_guard, options.pluginScanner);
    // The system's other standard folders (Linux has several; Windows one),
    // each with a cache of its own. A plugin found twice is taken once.
    if (options.pluginFolder.isEmpty()) {
        const QStringList standard = platform::standardVst3Folders();
        for (qsizetype i = 1; i < standard.size(); ++i) {
            if (!QFileInfo(standard.at(i)).isDir()) continue;
            engine->m_otherPluginFolders << standard.at(i);
            const QString cache =
                options.pluginCacheFile.isEmpty() ? QString() : options.pluginCacheFile + u".%1"_s.arg(i + 1);
            for (PluginInfo& plugin : PluginCatalog::scan(standard.at(i), cache, nullptr, scanProgress, &engine->m_guard,
                                                          options.pluginScanner)) {
                const bool found = std::ranges::any_of(engine->m_plugins, [&plugin](const PluginInfo& p) {
                    return p.name == plugin.name && p.vendor == plugin.vendor;
                });
                if (!found) engine->m_plugins.push_back(std::move(plugin));
            }
        }
        // VST2 plugins (older and free ones often come only as VST2). One
        // installed both ways is taken as VST3 (its newer, better version).
        const QStringList vst2 = platform::standardVst2Folders();
        int vst2Added = 0;
        for (qsizetype i = 0; i < vst2.size(); ++i) {
            if (!QFileInfo(vst2.at(i)).isDir()) continue;
            engine->m_otherPluginFolders << vst2.at(i);
            const QString cache = options.pluginCacheFile.isEmpty() ? QString() : options.pluginCacheFile + u".vst2-%1"_s.arg(i + 1);
            for (PluginInfo& plugin : PluginCatalog::scan(vst2.at(i), cache, nullptr, scanProgress, &engine->m_guard,
                                                          options.pluginScanner, PluginFormat::Vst2)) {
                const bool found = std::ranges::any_of(engine->m_plugins, [&plugin](const PluginInfo& p) {
                    return QString::compare(p.name, plugin.name, Qt::CaseInsensitive) == 0
                           && QString::compare(p.vendor, plugin.vendor, Qt::CaseInsensitive) == 0;
                });
                if (found) continue;
                engine->m_plugins.push_back(std::move(plugin));
                ++vst2Added;
            }
        }
        qCInfo(lcEngine).noquote() << "VST2 plugins added:" << vst2Added << "(those also installed as VST3 are taken as VST3)";
    }
    // The app's own plugins, unless the same one is installed already.
    if (!options.bundledPluginFolder.isEmpty() && QFileInfo(options.bundledPluginFolder).isDir()) {
        engine->m_bundledPluginFolder = options.bundledPluginFolder;
        const QString bundledCache = options.pluginCacheFile.isEmpty() ? QString() : options.pluginCacheFile + u".bundled"_s;
        int added = 0;
        for (PluginInfo& plugin : PluginCatalog::scan(options.bundledPluginFolder, bundledCache, nullptr, scanProgress,
                                                      &engine->m_guard, options.pluginScanner)) {
            const bool installed = std::ranges::any_of(engine->m_plugins, [&plugin](const PluginInfo& p) {
                return p.name == plugin.name && p.vendor == plugin.vendor;
            });
            if (installed) continue;
            engine->m_plugins.push_back(std::move(plugin));
            ++added;
        }
        qCInfo(lcEngine).noquote() << "Plugins that come with the app:" << added << "from" << options.bundledPluginFolder;
    }
    return engine;
}

RealEngine::~RealEngine()
{
    // Nothing may leave a destructor, and every step must still run when
    // one before it failed (the audio thread must stop before the graph goes).
    const auto step = [](const char* what, const auto& run) noexcept {
        try {
            run();
        } catch (const std::exception& e) {
            qCCritical(lcEngine).noquote() << "Shutting down:" << what << "failed:" << e.what();
        } catch (...) {
            qCCritical(lcEngine).noquote() << "Shutting down:" << what << "failed with an error of an unknown kind";
        }
    };
    // Stop audio before any graph or plugin is destroyed.
    step("closing the MIDI clock", [this] { m_clockOut.close(); });
    step("closing the MIDI inputs", [this] { m_midi.close(); });
    step("closing the audio device", [this] { m_audio.close(); });
    step("releasing the graph", [this] {
        m_exchange.publish(nullptr);
        m_exchange.collectGarbage();
    });
    step("releasing the backing track", [this] {
        m_track.publish(nullptr);
        m_track.collectGarbage();
    });
    // A backing track still being read writes into this engine: let it stop.
    step("stopping the backing track reader", [this] {
        if (m_trackReader) {
            m_cancelTrackRead.store(true, std::memory_order_relaxed);
            m_trackReader->wait();
        }
    });
}

void RealEngine::preload(const core::Setlist& setlist)
{
    GC_ONLY_MAIN_THREAD();
    // Everything the setlist plays, each shared instance once.
    const std::map<QString, const core::PluginSlot*> wanted = wantedSlots(setlist);
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
        m_nodePlugins.erase(entry.first);
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

std::map<QString, const core::PluginSlot*> RealEngine::wantedSlots(const core::Setlist& setlist)
{
    std::map<QString, const core::PluginSlot*> wanted;
    for (const core::Song& song : setlist.songs) {
        for (const core::Patch& patch : song.patches) {
            // The first of each key is kept (a map insert never replaces).
            std::ranges::transform(planPatch(song.id, patch), std::inserter(wanted, wanted.end()),
                                   [](const PlannedSlot& planned) { return std::pair{planned.key, planned.slot}; });
        }
    }
    return wanted;
}

void RealEngine::relinkInstances(const core::Setlist& setlist)
{
    GC_ONLY_MAIN_THREAD();
    const std::map<QString, const core::PluginSlot*> wanted = wantedSlots(setlist);
    // Instances no slot plays under their key any more: free to move to a
    // slot of the same plugin whose new key has none (what the player hears
    // stays; its settings are stored with the setlist on the next save).
    std::vector<QString> spare;
    for (const auto& [key, node] : m_nodes) {
        if (!wanted.contains(key)) spare.push_back(key);
    }
    int moved = 0;
    for (const auto& [key, slot] : wanted) {
        if (m_nodes.contains(key)) continue;
        const auto match = std::ranges::find_if(spare, [this, slot](const QString& old) {
            const auto plugin = m_nodePlugins.find(old);
            return plugin != m_nodePlugins.end() && plugin->second == slot->pluginId;
        });
        if (match == spare.end()) continue; // nothing to move: it loads when played (a song's own copy)
        m_nodes.emplace(key, m_nodes.extract(*match).mapped());
        if (auto state = m_nodeStates.extract(*match)) m_nodeStates.emplace(key, std::move(state.mapped()));
        if (auto plugin = m_nodePlugins.extract(*match)) m_nodePlugins.emplace(key, std::move(plugin.mapped()));
        if (m_editedNodes.erase(*match) > 0) m_editedNodes.insert(key);
        spare.erase(match);
        ++moved;
    }
    if (moved > 0) qCInfo(lcEngine) << "Kept" << moved << "loaded plugins for the songs now sharing them (nothing reloaded)";
}

std::shared_ptr<PluginNode> RealEngine::nodeFor(const QString& key, const core::PluginSlot& slot, bool announce)
{
    GC_ONLY_MAIN_THREAD();
    if (const auto it = m_nodes.find(key); it != m_nodes.end()) return it->second;
    if (announce && m_progress) m_progress(LoadStage::LoadingSounds, slot.displayName, 0, 1);
    // Says "done" on every way out.
    struct AnnounceDone
    {
        const LoadProgress& progress;
        bool on;
        AnnounceDone(const LoadProgress& onProgress, bool announcing) : progress(onProgress), on(announcing) {}
        AnnounceDone(const AnnounceDone&) = delete;
        AnnounceDone& operator=(const AnnounceDone&) = delete;
        AnnounceDone(AnnounceDone&&) = delete;
        AnnounceDone& operator=(AnnounceDone&&) = delete;
        ~AnnounceDone()
        {
            if (on && progress) progress(LoadStage::LoadingSounds, {}, 1, 1);
        }
    } announceDone(m_progress, announce);

    auto node = loadWithSettings(slot);
    if (!node) return nullptr;
    m_nodeStates[key] = slot.state;
    m_nodePlugins[key] = slot.pluginId;
    m_editedNodes.erase(key);
    m_nodes.emplace(key, node);
    return node;
}

bool RealEngine::isInstalledPlugin(const QString& pluginId) const
{
    // Where it is named to be, with any "../" worked out (a name climbing out
    // of the plugin folder is refused), and it must be there. Links inside
    // the plugin folder are not followed out of it: a folder linked in from
    // another drive (plugins kept on a music drive) is installed there, as
    // every DAW sees it; only an administrator can put a link there.
    if (!QFileInfo::exists(pluginId)) return false;
    const QString file = QDir::cleanPath(QFileInfo(pluginId).absoluteFilePath());
    const QStringList folders = QStringList{m_pluginFolder, m_bundledPluginFolder} + m_otherPluginFolders;
    return std::ranges::any_of(folders, [&file](const QString& folder) {
        const QString inside = folder.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
        // Paths compared as this system does (on Windows the same whatever the case).
        return !inside.isEmpty() && file.startsWith(inside + u'/', platform::fileNameCase());
    });
}

core::Result<void> RealEngine::checkInstalled(const QString& pluginId, const QString& name) const
{
    // A VST2 plugin is a plain library, and loading one runs its code before
    // anything can check it: only one the (sandboxed) scan found and read as
    // a VST2 plugin, never just any file a setlist names in a plugin folder.
    if (PluginNode::isVst2(pluginId) && isInstalledPlugin(pluginId)) {
        const QString file = QDir::cleanPath(QFileInfo(pluginId).absoluteFilePath());
        const bool scanned = std::ranges::any_of(m_plugins, [&file](const PluginInfo& p) {
            return p.format == PluginFormat::Vst2
                   && QString::compare(QDir::cleanPath(QFileInfo(p.id).absoluteFilePath()), file, platform::fileNameCase()) == 0;
        });
        if (scanned) return {};
        const QString problem = u"%1 (%2) is not a VST2 plugin the scan found: not loaded"_s.arg(name, pluginId);
        qCWarning(lcEngine).noquote() << problem;
        return core::fail(core::ErrorCode::InvalidData, problem);
    }
    if (isInstalledPlugin(pluginId)) return {};
    const QString problem = u"%1 is not one of the installed plugins (%2): not loaded. Install it in %3, "
                            u"or choose another plugin"_s.arg(name, pluginId, QDir::toNativeSeparators(m_pluginFolder));
    qCWarning(lcEngine).noquote() << problem;
    return core::fail(core::ErrorCode::InvalidData, problem);
}

std::shared_ptr<PluginNode> RealEngine::loadWithSettings(const core::PluginSlot& slot)
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
    // A setlist names its plugins by file: only installed ones load, so
    // opening a setlist (maybe someone else's) cannot run any other program.
    if (auto installed = checkInstalled(slot.pluginId, slot.displayName); !installed) {
        m_pendingNotices.push_back(Notice::error(installed.error().message));
        return nullptr;
    }
    QElapsedTimer timer;
    timer.start();
    // Until it has loaded and taken its settings: a crash here blocks it next start.
    const auto loading = m_guard.loading(slot.pluginId);
    auto node = PluginNode::load(slot.pluginId, m_audio.sampleRate(), m_audio.maxBlock());
    if (!node) {
        // Already logged by the plugin's load; tell the user too.
        m_pendingNotices.push_back(Notice::error(u"Could not load %1: %2"_s.arg(slot.displayName, node.error().message)));
        return nullptr;
    }
    if (!slot.state.isEmpty()) {
        auto restored = (*node)->restoreEncodedState(slot.state);
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
    const AudioApi api = setup.driver;
    // A setup from another system (ASIO on Linux, JACK on Windows).
    if (const auto here = systemAudioDrivers(); std::ranges::find(here, api) == here.end()) {
        const QString problem = u"%1 is not available on this system"_s.arg(apiName(api));
        qCWarning(lcEngine).noquote() << problem;
        return core::fail(core::ErrorCode::DeviceUnavailable, problem);
    }
    std::optional<DeviceChoice> choice;
    if (!setup.device.isEmpty()) {
        choice = DeviceChoice{.api = api, .name = setup.device};
    } else if (setup.driver == AudioDriver::Asio) {
        return core::fail(core::ErrorCode::InvalidData, u"Choose an ASIO device"_s);
    }
    std::optional<DeviceChoice> input;
    if (!setup.inputDevice.isEmpty()) input = DeviceChoice{.api = api, .name = setup.inputDevice};
    return m_audio.open(choice, setup.bufferFrames,
                        [this](const AudioBlock& out, const AudioInputs& inputs) { render(out, inputs); }, setup.sampleRate,
                        input);
}

std::vector<AudioInputDevice> RealEngine::audioInputDevices() const
{
    std::vector<AudioInputDevice> devices;
    std::ranges::transform(AudioDevice::listInputs(), std::back_inserter(devices), [](const AudioDeviceInfo& info) {
        return AudioInputDevice{.driver = info.api,
                                .name = info.name,
                                .channels = std::min(info.inputChannels, kMaxAudioInputs)};
    });
    return devices;
}

std::vector<AudioOutput> RealEngine::audioOutputs() const
{
    std::vector<AudioOutput> outputs;
    // Static: one array, the same one the lambda below searches and ends at.
    static constexpr std::array<unsigned int, 4> kLiveRates{44100, 48000, 88200, 96000};
    for (const AudioDeviceInfo& info : AudioDevice::listOutputs()) {
        std::vector<unsigned int> rates;
        std::ranges::copy_if(info.sampleRates, std::back_inserter(rates),
                             [](unsigned int rate) { return std::ranges::find(kLiveRates, rate) != kLiveRates.end(); });
        outputs.push_back(AudioOutput{.driver = info.api,
                                      .name = info.name,
                                      .sampleRates = std::move(rates),
                                      .preferredSampleRate = info.preferredSampleRate,
                                      .isDefault = info.isDefault});
    }
    return outputs;
}

AudioSetup RealEngine::audioSetup() const
{
    // Standing in for a missing device: the setup is still the one chosen
    // (Settings must not quietly replace the interface with the stand-in).
    if (m_audio.standingIn()) {
        const std::optional<DeviceChoice>& wanted = m_audio.wanted();
        return AudioSetup{.driver = wanted ? wanted->api : AudioDriver::System,
                          .device = wanted ? wanted->name : QString(),
                          .sampleRate = m_audio.wantedRate(),
                          .bufferFrames = m_audio.requestedBufferFrames(),
                          .inputDevice = m_audio.wantedInput() ? m_audio.wantedInput()->name : QString()};
    }
    return AudioSetup{.driver = m_audio.api(),
                      .device = m_audio.deviceName(),
                      .sampleRate = static_cast<unsigned int>(m_audio.sampleRate()),
                      .bufferFrames = static_cast<unsigned int>(m_audio.maxBlock()),
                      .inputDevice = m_audio.inputName()};
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
    for (const auto* nodes : {&m_nodes, &m_master.nodes, &m_aux.nodes}) {
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
    // (None at all when the engine was made without MIDI inputs.)
    const auto ports = m_midiInputs ? resolveMidiInputs(m_midiPorts, m_midiSetup) : std::vector<MidiPort>{};
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
    std::vector<QString> problems = openMidi();
    std::ranges::move(applyClockSetup(), std::back_inserter(problems));
    if (!problems.empty()) {
        QStringList text;
        for (const QString& problem : problems) text << problem;
        return core::fail(core::ErrorCode::DeviceUnavailable, text.join(u"; "_s));
    }
    return {};
}

std::vector<QString> RealEngine::applyClockSetup()
{
    GC_ONLY_MAIN_THREAD();
    m_followClock.store(m_midiSetup.followClock, std::memory_order_relaxed);
    m_midi.setTransportControls(m_midiSetup.transportButtons, m_midiSetup.sustainDoubleTap);
    if (m_midiSetup.clockOutput == m_clockOut.portName()) return {};
    m_clockOut.setTempo(tempo());
    if (auto opened = m_clockOut.open(m_midiSetup.clockOutput); !opened) return {opened.error().message}; // logged
    return {};
}

void RealEngine::watchMidiPorts(std::vector<Notice>& notices)
{
    constexpr auto kInterval = std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastMidiCheck < kInterval) return;
    m_lastMidiCheck = now;

    // The clock's output plugged in again (the clock stopped when it was
    // pulled out): it starts again.
    const QStringList outputs = MidiClockOut::listPorts();
    if (outputs != std::exchange(m_midiOutputs, outputs) && !m_midiSetup.clockOutput.isEmpty() &&
        m_clockOut.portName().isEmpty() && outputs.contains(m_midiSetup.clockOutput)) {
        const std::vector<QString> problems = applyClockSetup(); // each logged
        if (problems.empty()) {
            notices.push_back(Notice::info(u"MIDI clock to %1 started again"_s.arg(m_midiSetup.clockOutput)));
            qCInfo(lcEngine).noquote() << notices.back().text;
        }
        std::ranges::transform(problems, std::back_inserter(notices), &Notice::warning);
    }

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
    callUpExternalSounds(patch);
    m_song = song;
    std::set<PluginNode*> used;
    m_currentInstruments.clear();
    m_currentEffects.clear();
    const std::vector<PlannedSlot> plan = planPatch(song, patch);
    std::vector<StripSpec> specs;
    specs.reserve(patch.channels.size());
    std::set<const HardwareOut*> hardwareUsed; // the synths the new patch's channels play
    for (std::size_t c = 0; c < patch.channels.size(); ++c) {
        const core::Channel& channel = patch.channels.at(c);
        StripSpec spec;
        spec.id = channel.id;
        spec.route = RouteSettings{.keyLow = channel.keyLow,
                                   .keyHigh = channel.keyHigh,
                                   .transpose = channel.transpose,
                                   .midiChannel = channel.midiChannel,
                                   .velocityLow = channel.velocityLow,
                                   .velocityHigh = channel.velocityHigh,
                                   .ignores = (channel.takesSustain ? 0U : midi_filter::kSustain)
                                              | (channel.takesExpression ? 0U : midi_filter::kExpression)
                                              | (channel.takesModWheel ? 0U : midi_filter::kModWheel)
                                              | (channel.takesPitchBend ? 0U : midi_filter::kPitchBend)
                                              | (channel.takesAftertouch ? 0U : midi_filter::kAftertouch)};
        spec.volumeDb = channel.volumeDb;
        spec.pan = channel.pan;
        spec.mute = channel.mute;
        spec.solo = channel.solo;
        spec.inputLeft = channel.inputLeft - 1; // 1-based in the setlist, -1 = none
        spec.inputRight = channel.inputRight - 1;
        spec.outputPair = channel.outputPair;
        spec.sendDb = channel.auxSendDb;
        if (!channel.midiOutPort.isEmpty()) {
            const QString key = u"%1|%2|%3"_s.arg(channel.id.value(), channel.midiOutPort).arg(channel.midiOutChannel);
            std::shared_ptr<HardwareOut>& out = m_hardwareOuts[key];
            if (!out) out = std::make_shared<HardwareOut>(channel.midiOutPort, channel.midiOutChannel);
            spec.hardware = out;
            hardwareUsed.insert(out.get());
        }
        spec.midiEffects = MidiEffectSettings{.chord = channel.chord,
                                              .arpeggio = channel.arpeggio,
                                              .arpRate = channel.arpRate,
                                              .arpOctaves = channel.arpOctaves};
        std::map<int, int> effectAt; // the channel's effect position -> its place in the strip (switched-off ones are left out)
        for (const PlannedSlot& planned : plan) {
            if (std::cmp_not_equal(planned.channel, c)) continue;
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
                effects.at(static_cast<std::size_t>(planned.effect)) = node;
                effectAt[planned.effect] = static_cast<int>(spec.effects.size());
                spec.effects.push_back(std::move(node));
            }
        }
        for (const core::ControlMapping& m : channel.mappings) {
            int target = -1;
            if (m.target >= 0) {
                const auto at = effectAt.find(m.target);
                if (at == effectAt.end()) continue; // its effect is switched off or not loaded
                target = at->second;
            } else if (!spec.instrument) {
                continue;
            }
            spec.mappings.push_back(ParameterMapping{.midiChannel = m.midiChannel,
                                                     .controller = m.controller,
                                                     .target = target,
                                                     .parameter = m.parameter,
                                                     .minimum = m.minimum,
                                                     .maximum = m.maximum,
                                                     .curve = m.curve,
                                                     .pickup = m.pickup});
        }
        specs.push_back(std::move(spec));
    }

    // Strips of the sounding graph (and its own tails) that the new patch
    // does not play ring out: held notes until they are let go, reverbs
    // until they fade. A strip sharing a plugin with the new patch cannot
    // (the plugin plays in the new patch).
    const std::set<const INode*> playing(used.begin(), used.end());
    std::vector<std::shared_ptr<ChannelStrip>> tails;
    std::set<const INode*> ringing;
    std::set<const INode*> releasing; // instruments whose old strip's MIDI effects made notes
    if (const std::shared_ptr<RenderGraph>& previous = m_exchange.currentShared()) {
        for (const auto* strips : {&previous->strips(), &previous->tails()}) {
            for (const auto& strip : *strips) {
                const std::vector<const INode*> nodes = strip->nodes();
                const bool shared = std::ranges::any_of(nodes, [&playing](const INode* n) { return playing.contains(n); });
                // A strip whose MIDI effects made notes (a chord's, an arpeggio's), going
                // while its instrument plays on: its notes go too (its effects' state does
                // not carry over, so nothing else would end them).
                if (shared && strip->hasMidiEffects() && !nodes.empty()) releasing.insert(nodes.front());
                // A hardware synth's notes held when the patch changes end with their keys
                // (unless the new patch plays that synth from the same channel: it ends them).
                const bool synthHeld = strip->holdsHardwareNotes() && !hardwareUsed.contains(strip->hardware().get());
                if (strip->tailDone() || shared || (nodes.empty() && !synthHeld) || tails.size() >= kMaxTails) continue;
                tails.push_back(strip);
                ringing.insert(nodes.begin(), nodes.end());
            }
        }
    }
    // The synths still played, by the new patch or its tails; the rest are let go.
    std::set<const HardwareOut*> hardwareKept = hardwareUsed;
    for (const auto& tail : tails) {
        if (tail->hardware()) hardwareKept.insert(tail->hardware().get());
    }
    std::erase_if(m_hardwareOuts, [&hardwareKept](const auto& entry) { return !hardwareKept.contains(entry.second.get()); });
    std::vector<std::shared_ptr<HardwareOut>> synths;
    synths.reserve(m_hardwareOuts.size());
    for (const auto& [key, out] : m_hardwareOuts) synths.push_back(out);
    m_hardwareSender.setOuts(std::move(synths));

    // Instruments leaving the sound (and not ringing out) release their
    // notes, so they do not hang when the patch comes back.
    for (const auto& [key, node] : m_nodes) {
        if ((used.count(node.get()) == 0 && !ringing.contains(node.get())) || releasing.contains(node.get())) node->releaseAllNotes();
    }
    auto next = std::make_shared<RenderGraph>(std::move(specs), m_audio.sampleRate(), m_audio.maxBlock(), busNodes(m_master),
                                              std::move(tails), busNodes(m_aux));
    applySectionMasks(*next); // before it plays: no moment with the wrong channels taking notes
    applyLoopSlots(*next);
    m_exchange.publish(std::move(next));
    publishTimeline(); // the sections count only for the patch they were worked out for
    qCInfo(lcEngine).noquote() << "Patch applied:" << patch.name << "(" << patch.channels.size() << "channels,"
                               << m_exchange.current()->tails().size() << "ringing out ) in" << timer.elapsed() << "ms";
}

LevelReading RealEngine::channelLevel(const core::ChannelId& id)
{
    RenderGraph* graph = m_exchange.current();
    ChannelStrip* strip = graph != nullptr ? graph->findStrip(id) : nullptr;
    return strip != nullptr ? strip->takeLevel() : LevelReading{};
}

LevelReading RealEngine::masterLevel()
{
    return LevelReading{.peak = m_masterPeak.exchange(0.0F, std::memory_order_relaxed),
                        .rms = m_masterRms.load(std::memory_order_relaxed)};
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
    if (midiChannel < 1 || midiChannel > 16 || note < 0 || note > 127 || velocity < 0 || velocity > 127) {
        qCWarning(lcEngine) << "Note ignored: channel" << midiChannel << "(1-16), note" << note << "(0-127), velocity"
                            << velocity << "(0-127)";
        return;
    }
    const auto status = static_cast<uint8_t>((velocity > 0 ? 0x90 : 0x80) | (midiChannel - 1));
    if (velocity > 0) m_midi.recordPress(note, velocity); // (played from the screen: timed like the keyboard)
    if (!m_injected.push(MidiEvent{.status = status,
                                   .data1 = static_cast<uint8_t>(note),
                                   .data2 = static_cast<uint8_t>(velocity),
                                   .sampleOffset = 0})) {
        m_droppedInjected.fetch_add(1, std::memory_order_relaxed);
    }
}

void RealEngine::injectController(int midiChannel, int controller, int value)
{
    if (midiChannel < 1 || midiChannel > 16 || controller < 0 || controller > 127 || value < 0 || value > 127) {
        qCWarning(lcEngine) << "Controller ignored: channel" << midiChannel << "(1-16), controller" << controller
                            << "(0-127), value" << value << "(0-127)";
        return;
    }
    if (!m_injected.push(MidiEvent{.status = static_cast<uint8_t>(0xB0 | (midiChannel - 1)),
                                   .data1 = static_cast<uint8_t>(controller),
                                   .data2 = static_cast<uint8_t>(value),
                                   .sampleOffset = 0})) {
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
    collectBackingTrack(notices);
    serviceLoops(notices);
    m_clockOut.setTempo(tempo());
    if (m_clockOut.takeFailed()) {
        notices.push_back(Notice::warning(u"The MIDI clock to %1 stopped: the output stopped working"_s.arg(m_clockOut.portName())));
        qCWarning(lcEngine).noquote() << notices.back().text;
        m_clockOut.close(); // choosing the output again in Settings restarts it
    }
    if (m_recorder.takeFailed()) {
        notices.push_back(Notice::error(u"Recording stopped: the recording could not be written (is the disk full?): %1"_s.arg(m_recorder.filePath())));
        (void)m_recorder.stop(); // finishes what was written; its problem is in the notice above
    }
    // Knobs mapped to parameters: shown in the plugins' own windows, and
    // where each parameter is now, for the knobs' pickup.
    for (const auto& [key, node] : m_nodes) node->showParameterChanges();
    if (const RenderGraph* graph = m_exchange.current()) {
        for (const auto& strip : graph->strips()) strip->refreshMappedValues();
    }
    std::ranges::move(m_pendingNotices, std::back_inserter(notices));
    m_pendingNotices.clear();

    if (const uint64_t dropped = m_midi.takeDropped() + m_droppedInjected.exchange(0); dropped > 0) {
        qCWarning(lcEngine) << "Dropped" << dropped << "MIDI events (input queue full)";
    }
    for (const QString& problem : m_hardwareSender.takeProblems()) {
        qCWarning(lcEngine).noquote() << problem;
        notices.push_back(Notice::warning(problem));
    }
    if (const uint64_t dropped = m_hardwareSender.takeDropped(); dropped > 0) {
        qCWarning(lcEngine) << "Left out" << dropped << "messages to hardware synths (their output was down, or too many at once)";
    }
    if (m_midi.takeActivity()) m_midiSeen.store(true, std::memory_order_relaxed);
    else m_midiSeen.store(false, std::memory_order_relaxed);

    if (RenderGraph* graph = m_exchange.current()) {
        if (const uint64_t oversized = graph->takeOversizedBlocks(); oversized > 0) {
            qCWarning(lcEngine) << "Skipped" << oversized << "audio blocks larger than the prepared size";
        }
        if (const uint64_t dropped = graph->takeDroppedEvents(); dropped > 0) {
            qCWarning(lcEngine) << "Dropped" << dropped << "MIDI events: more than" << kMaxStripEventsPerBlock
                                << "reached one channel in one audio block";
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
    return PluginNode::createEditor(it->second);
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
    const core::PluginSlot& slot = channel->effects.at(static_cast<std::size_t>(effect));
    if (slot.bypass) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is switched off: switch it on to open its window"_s.arg(slot.displayName));
    }
    const auto effects = m_currentEffects.find(id.value());
    if (effects == m_currentEffects.end() || static_cast<std::size_t>(effect) >= effects->second.size()
        || !effects->second.at(static_cast<std::size_t>(effect))) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is not loaded (see the message about why)"_s.arg(slot.displayName));
    }
    return PluginNode::createEditor(effects->second.at(static_cast<std::size_t>(effect)));
}

std::vector<QString> RealEngine::busKeys(const EffectsBus& bus)
{
    std::vector<QString> keys;
    std::map<QString, int> uses;
    for (const core::PluginSlot& slot : bus.effects) {
        if (slot.bypass) {
            keys.emplace_back();
            continue;
        }
        const int n = uses[slot.pluginId]++;
        keys.push_back(bus.name + u"|fx|"_s + slot.pluginId + u'#' + QString::number(n));
    }
    return keys;
}

std::vector<std::shared_ptr<INode>> RealEngine::busNodes(const EffectsBus& bus)
{
    std::vector<std::shared_ptr<INode>> nodes;
    for (const QString& key : busKeys(bus)) {
        if (const auto it = bus.nodes.find(key); it != bus.nodes.end()) nodes.push_back(it->second);
    }
    return nodes;
}

void RealEngine::setBusEffects(EffectsBus& bus, const std::vector<core::PluginSlot>& effects)
{
    GC_ONLY_MAIN_THREAD();
    QElapsedTimer timer;
    timer.start();
    // Take edits made to instances that are going away before they go.
    (void)takeBusEdits(bus);
    bus.effects = effects;
    bus.edited = false;
    const std::vector<QString> keys = busKeys(bus);
    std::erase_if(bus.nodes, [&](const auto& entry) { return std::ranges::find(keys, entry.first) == keys.end(); });
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const QString& key = keys.at(i);
        if (key.isEmpty() || bus.nodes.contains(key)) continue;
        if (auto node = loadWithSettings(bus.effects.at(i))) bus.nodes.emplace(key, std::move(node));
    }
    applyPatch(m_song, m_patch); // the graph now plays them
    qCInfo(lcEngine).noquote() << "Effects on the" << bus.name << "bus:" << bus.nodes.size() << "loaded in" << timer.elapsed() << "ms";
}

std::vector<QString> RealEngine::storeBusEffectStates(EffectsBus& bus, std::vector<core::PluginSlot>& effects)
{
    GC_ONLY_MAIN_THREAD();
    std::vector<QString> problems;
    const std::vector<QString> keys = busKeys(bus);
    for (std::size_t i = 0; i < effects.size() && i < keys.size(); ++i) {
        core::PluginSlot& effect = effects.at(i);
        const auto node = bus.nodes.find(keys.at(i));
        if (node == bus.nodes.end() || effect.pluginId != bus.effects.at(i).pluginId) continue;
        auto state = node->second->saveEncodedState();
        if (!state) {
            problems.push_back(u"The settings of %1 could not be saved: %2"_s.arg(effect.displayName, state.error().message));
            qCWarning(lcEngine).noquote() << problems.back();
            continue;
        }
        effect.state = *state;
        bus.effects.at(i).state = effect.state;
    }
    return problems;
}

bool RealEngine::takeBusEdits(EffectsBus& bus)
{
    GC_ONLY_MAIN_THREAD();
    for (const auto& [key, node] : bus.nodes) {
        if (node->takeEdited()) bus.edited = true;
    }
    return std::exchange(bus.edited, false);
}

core::Result<std::unique_ptr<IPluginEditor>> RealEngine::createBusEffectEditor(EffectsBus& bus, int effect)
{
    GC_ONLY_MAIN_THREAD();
    const std::vector<QString> keys = busKeys(bus);
    if (effect < 0 || static_cast<std::size_t>(effect) >= keys.size()) {
        return core::fail(core::ErrorCode::OutOfRange, u"That %1 effect is no longer there"_s.arg(bus.name));
    }
    const core::PluginSlot& slot = bus.effects.at(static_cast<std::size_t>(effect));
    if (slot.bypass) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is switched off: switch it on to open its window"_s.arg(slot.displayName));
    }
    const auto node = bus.nodes.find(keys.at(static_cast<std::size_t>(effect)));
    if (node == bus.nodes.end()) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is not loaded (see the message about why)"_s.arg(slot.displayName));
    }
    return PluginNode::createEditor(node->second);
}

void RealEngine::setChannelSend(const core::ChannelId& id, double sendDb)
{
    GC_ONLY_MAIN_THREAD();
    if (!std::isfinite(sendDb)) {
        qCWarning(lcEngine) << "Ignored a send of" << sendDb << "dB";
        return;
    }
    if (RenderGraph* graph = m_exchange.current()) {
        if (ChannelStrip* strip = graph->findStrip(id)) strip->setSendDb(sendDb);
    }
}

void RealEngine::setOutputLimiter(bool enabled, double ceilingDb)
{
    m_limiter.setEnabled(enabled);
    m_limiter.setCeilingDb(ceilingDb);
    for (SafetyLimiter& send : m_sendLimiters) {
        send.setEnabled(enabled);
        send.setCeilingDb(ceilingDb);
    }
    qCInfo(lcEngine) << "Safety limiter" << (enabled ? "on at" : "off (ceiling") << ceilingDb << "dB";
}

std::size_t RealEngine::takeControlMessages(std::size_t count) noexcept
{
    std::array<MidiTrigger, kControlActionCount> triggers;
    std::ranges::transform(m_triggers, triggers.begin(),
                           [](const auto& packed) { return MidiTrigger::unpack(packed.load(std::memory_order_relaxed)); });
    const bool any = std::ranges::any_of(triggers, [](const MidiTrigger& t) { return t.isSet(); });
    std::array<MidiTrigger, kLoopButtonCount> loopButtons;
    std::ranges::transform(m_loopTriggers, loopButtons.begin(),
                           [](const auto& packed) { return MidiTrigger::unpack(packed.load(std::memory_order_relaxed)); });
    const MidiTrigger selector = MidiTrigger::unpack(m_selectorKnob.load(std::memory_order_relaxed));
    std::array<uint32_t, kAppKnobCount> appKnobs{};
    std::ranges::transform(m_appKnobs, appKnobs.begin(), [](const auto& packed) { return packed.load(std::memory_order_relaxed); });
    const auto selectorMode = static_cast<SelectorKnob::Mode>(m_selectorMode.load(std::memory_order_relaxed));

    // The block's events, filtered in place: what is not a control stays.
    const std::span<MidiEvent> events(m_events.data(), count);
    auto kept = events.begin();
    for (const MidiEvent event : events) { // a copy: `kept` may write over it
        if (const MidiTrigger press = learnable(event.status, event.data1, event.data2); press.isSet()) {
            m_learned.store(press.pack(), std::memory_order_relaxed);
        }
        if ((event.status & 0xF0) == 0xB0) {
            m_movedController.store(((((event.status & 0x0F) * 128) + event.data1) * 128) + event.data2,
                                    std::memory_order_relaxed);
        }
        bool consumed = false;
        // The app's knobs (the mixer's faders and pans): where each is.
        if ((event.status & 0xF0) == 0xB0) {
            const uint32_t packed =
                MidiTrigger{.kind = MidiTrigger::ControlChange, .channel = static_cast<uint8_t>(event.status & 0x0F), .number = event.data1}.pack();
            for (std::size_t k = 0; k < appKnobs.size(); ++k) {
                if (appKnobs.at(k) != packed) continue;
                m_appKnobValues.at(k).store(event.data2 + 1, std::memory_order_relaxed);
                consumed = true;
            }
        }
        // The instrument knob.
        if (selector.isSet() && (event.status & 0xF0) == 0xB0 && (event.status & 0x0F) == selector.channel &&
            event.data1 == selector.number) {
            consumed = true;
            if (selectorMode == SelectorKnob::Absolute) m_selectorValue.store(event.data2, std::memory_order_relaxed);
            else m_selectorSteps.fetch_add(encoderSteps(selectorMode, event.data2), std::memory_order_relaxed);
        }
        // The looper's buttons; Record and PlayStop held together clear.
        for (std::size_t b = 0; b < loopButtons.size(); ++b) {
            const TriggerMatch match = matchTrigger(loopButtons.at(b), event.status, event.data1, event.data2);
            if (!match.belongs) continue;
            consumed = true;
            const uint32_t bit = 1U << b;
            if (!match.pressed) {
                m_loopHeld &= ~bit;
                continue;
            }
            m_loopHeld |= bit;
            const uint32_t pair = (1U << static_cast<int>(LoopAction::Record)) | (1U << static_cast<int>(LoopAction::PlayStop));
            const bool both = (bit & pair) != 0 && (m_loopHeld & pair) == pair;
            m_loopPressed.fetch_or(both ? (1U << static_cast<int>(LoopAction::Clear)) : bit, std::memory_order_relaxed);
        }
        if (any) {
            uint32_t action = 1U;
            for (const MidiTrigger& trigger : triggers) {
                const TriggerMatch match = matchTrigger(trigger, event.status, event.data1, event.data2);
                if (match.belongs) {
                    consumed = true; // the instruments never hear a control
                    if (match.pressed) m_pressedActions.fetch_or(action, std::memory_order_relaxed);
                }
                action <<= 1U;
            }
        }
        // A patch button: it picks a patch (a learned trigger above wins).
        if (const int program = programOf(event.status, event.data1); !consumed && program >= 0) {
            m_program.store(program, std::memory_order_relaxed);
            consumed = true;
        }
        // A keyboard's DAW port only ever controls: the instruments never hear it.
        if (!consumed && !event.controlsOnly) *kept++ = event;
    }
    return static_cast<std::size_t>(kept - events.begin());
}

void RealEngine::setControlTriggers(const ControlTriggers& triggers)
{
    GC_ONLY_MAIN_THREAD();
    for (std::size_t i = 0; i < triggers.size(); ++i) m_triggers.at(i).store(triggers.at(i).pack(), std::memory_order_relaxed);
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

std::optional<std::pair<int, int>> RealEngine::takeMovedController()
{
    GC_ONLY_MAIN_THREAD();
    const int moved = m_movedController.exchange(-1, std::memory_order_relaxed);
    if (moved < 0) return std::nullopt;
    return std::pair{(moved / (128 * 128)) + 1, (moved / 128) % 128};
}

std::optional<std::array<int, 3>> RealEngine::takeControllerMove()
{
    GC_ONLY_MAIN_THREAD();
    const int moved = m_movedController.exchange(-1, std::memory_order_relaxed);
    if (moved < 0) return std::nullopt;
    return std::array{(moved / (128 * 128)) + 1, (moved / 128) % 128, moved % 128};
}

void RealEngine::setAppKnobs(const AppKnobs& knobs)
{
    GC_ONLY_MAIN_THREAD();
    for (std::size_t i = 0; i < knobs.size(); ++i) {
        m_appKnobs.at(i).store(knobs.at(i).kind == MidiTrigger::ControlChange ? knobs.at(i).pack() : 0U, std::memory_order_relaxed);
    }
}

AppKnobValues RealEngine::takeAppKnobValues()
{
    GC_ONLY_MAIN_THREAD();
    AppKnobValues values{};
    for (std::size_t i = 0; i < values.size(); ++i) values.at(i) = m_appKnobValues.at(i).exchange(0, std::memory_order_relaxed) - 1;
    return values;
}

void RealEngine::setLoopControls(const LoopTriggers& buttons, const SelectorKnob& selector)
{
    GC_ONLY_MAIN_THREAD();
    for (std::size_t i = 0; i < buttons.size(); ++i) m_loopTriggers.at(i).store(buttons.at(i).pack(), std::memory_order_relaxed);
    m_selectorMode.store(selector.mode, std::memory_order_relaxed);
    m_selectorKnob.store(selector.knob.kind == MidiTrigger::ControlChange ? selector.knob.pack() : 0U, std::memory_order_relaxed);
}

std::vector<LoopAction> RealEngine::takeLoopActions()
{
    GC_ONLY_MAIN_THREAD();
    const uint32_t pressed = m_loopPressed.exchange(0, std::memory_order_relaxed);
    std::vector<LoopAction> actions;
    for (int i = 0; i <= static_cast<int>(LoopAction::Clear); ++i) {
        if ((pressed & (1U << i)) != 0) actions.push_back(static_cast<LoopAction>(i));
    }
    return actions;
}

SelectorMove RealEngine::takeSelectorMove()
{
    GC_ONLY_MAIN_THREAD();
    return SelectorMove{.value = m_selectorValue.exchange(-1, std::memory_order_relaxed),
                        .steps = m_selectorSteps.exchange(0, std::memory_order_relaxed)};
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
    m_keyboard.clear(); // every key shown up again
    m_hardwareSender.allNotesOff(); // the hardware synths too
    for (const auto* nodes : {&m_nodes, &m_master.nodes, &m_aux.nodes}) {
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
    qCWarning(lcEngine) << "Panic: every sound stopped (" << m_nodes.size() + m_master.nodes.size() + m_aux.nodes.size() << "plugins reset in"
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
        auto state = node->second->saveEncodedState();
        if (!state) {
            const QString problem =
                u"The settings of %1 could not be saved: %2"_s.arg(node->second->name(), state.error().message);
            qCWarning(lcEngine).noquote() << problem;
            problems.push_back(problem);
            return stored[key] = std::nullopt;
        }
        return stored[key] = *state;
    };
    for (core::Song& song : setlist.songs) {
        for (core::Patch& patch : song.patches) {
            for (const PlannedSlot& planned : planPatch(song.id, patch)) {
                const auto bytes = stateOf(planned.key);
                if (!bytes) continue;
                core::Channel& channel = patch.channels.at(static_cast<std::size_t>(planned.channel));
                // planPatch only plans an instrument slot for a channel with one.
                GC_IF_FAILED(planned.effect >= 0 || channel.instrument.has_value()) { continue; }
                core::PluginSlot& slot = planned.effect < 0
                                             ? *channel.instrument
                                             : channel.effects.at(static_cast<std::size_t>(planned.effect));
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

void RealEngine::setTempo(double bpm)
{
    if (!std::isfinite(bpm) || bpm < kMinTempo || bpm > core::limits::kMaxTempo) {
        qCWarning(lcEngine) << "Tempo" << bpm << "ignored: it must be between" << kMinTempo << "and" << core::limits::kMaxTempo;
        return;
    }
    m_tempo.store(bpm, std::memory_order_relaxed);
}

void RealEngine::setTimeSignature(int numerator, int denominator)
{
    GC_ONLY_MAIN_THREAD();
    if (!core::isTimeSignature(numerator, denominator)) {
        qCWarning(lcEngine) << "Time signature" << numerator << "/" << denominator
                            << "ignored: it must be 1-32 beats of a 1, 2, 4, 8, 16 or 32 note";
        return;
    }
    if (numerator == m_timeNumerator.load(std::memory_order_relaxed) &&
        denominator == m_timeDenominator.load(std::memory_order_relaxed)) {
        return;
    }
    m_timeNumerator.store(numerator, std::memory_order_relaxed);
    m_timeDenominator.store(denominator, std::memory_order_relaxed);
    publishTimeline(); // bars are longer or shorter now
}

void RealEngine::setSongSections(const SongSections& sections)
{
    GC_ONLY_MAIN_THREAD();
    if (sections.sections.size() > static_cast<std::size_t>(core::limits::kMaxSectionsPerSong)) {
        qCWarning(lcEngine) << "Song sections ignored:" << sections.sections.size() << "sections, at most"
                            << core::limits::kMaxSectionsPerSong;
        return;
    }
    m_sections = sections;
    if (RenderGraph* graph = m_exchange.current()) applySectionMasks(*graph);
    publishTimeline();
}

void RealEngine::publishTimeline()
{
    GC_ONLY_MAIN_THREAD();
    if (m_sections.sections.empty() || m_sections.patch != m_patch.id) {
        m_timeline.publish(nullptr); // no sections (or not this patch's): everything plays
        return;
    }
    const int numerator = m_timeNumerator.load(std::memory_order_relaxed);
    const int denominator = m_timeDenominator.load(std::memory_order_relaxed);
    const double beat = 4.0 / denominator; // in quarter notes
    std::vector<int> bars;
    bars.reserve(m_sections.sections.size());
    std::ranges::transform(m_sections.sections, std::back_inserter(bars), [](const auto& s) { return s.bars; });
    // Just before the section (a sixteenth), or a whole beat early.
    const double lead = m_sections.switchEarly ? beat : 0.25;
    m_timeline.publish(std::make_shared<SongTimeline>(SongTimeline::fromParts(songParts(), bars, numerator * beat, lead)));
}

std::vector<int> RealEngine::songParts() const
{
    // The flow (parts naming no section dropped, as the timeline drops them),
    // else each section once.
    std::vector<int> parts;
    const auto sections = static_cast<int>(m_sections.sections.size());
    std::ranges::copy_if(m_sections.parts, std::back_inserter(parts), [sections](int s) { return s >= 0 && s < sections; });
    if (parts.empty()) {
        for (int s = 0; s < sections; ++s) parts.push_back(s);
    }
    return parts;
}

int RealEngine::partForSection(int section) const
{
    // The next time the song comes to that section (after the part in
    // force), else its first time.
    const std::vector<int> parts = songParts();
    const int current = m_transport.position().part;
    int first = -1;
    for (int p = 0; std::cmp_less(p, parts.size()); ++p) {
        if (parts.at(static_cast<std::size_t>(p)) != section) continue;
        if (first < 0) first = p;
        if (p > current) return p;
    }
    return first;
}

void RealEngine::queueNextPart()
{
    GC_ONLY_MAIN_THREAD();
    m_transport.queueNext();
}

void RealEngine::queuePart(int part)
{
    GC_ONLY_MAIN_THREAD();
    const auto count = static_cast<int>(songParts().size());
    if (part < 0 || part >= count) {
        qCWarning(lcEngine) << "Go to part" << part + 1 << "ignored: the song has" << count << "parts";
        return;
    }
    // Stopped: that part is where the song is (and where Play starts).
    if (!m_transport.position().playing) m_transport.jump(part);
    else m_transport.queuePart(part);
}

void RealEngine::repeatPart()
{
    GC_ONLY_MAIN_THREAD();
    m_transport.queueRepeat();
}

void RealEngine::toggleHoldPart()
{
    GC_ONLY_MAIN_THREAD();
    m_transport.toggleHold();
}

void RealEngine::toggleStopAtEndOfPart()
{
    GC_ONLY_MAIN_THREAD();
    m_transport.toggleStopAtEnd();
}

void RealEngine::cancelQueuedParts()
{
    GC_ONLY_MAIN_THREAD();
    m_transport.cancelQueued();
}

void RealEngine::applySectionMasks(RenderGraph& graph) const
{
    GC_ONLY_MAIN_THREAD();
    const bool thisPatch = m_sections.patch == m_patch.id;
    const bool ours = !m_sections.sections.empty() && thisPatch;
    const auto& unsectioned = m_sections.unsectioned;
    for (std::size_t i = 0; i < graph.stripCount(); ++i) {
        ChannelStrip* strip = graph.strip(i);
        uint64_t mask = ours ? 0 : ~uint64_t{0};
        for (std::size_t s = 0; ours && s < m_sections.sections.size(); ++s) {
            const auto& live = m_sections.sections.at(s).live;
            if (std::ranges::find(live, strip->id()) != live.end()) mask |= uint64_t{1} << s;
        }
        strip->setSections(mask);
        // Outside any section: every channel, unless the sound plays one at a time.
        strip->setUnsectioned(!thisPatch || !unsectioned || std::ranges::find(*unsectioned, strip->id()) != unsectioned->end());
    }
}

void RealEngine::playSong(int fromSection, bool countIn)
{
    GC_ONLY_MAIN_THREAD();
    const int count = static_cast<int>(m_sections.sections.size());
    if (count == 0 || fromSection < -1 || fromSection >= count) {
        qCWarning(lcEngine) << "Play from section" << fromSection + 1 << "ignored: the song has" << count << "sections";
        return;
    }
    if (fromSection < 0) { // from the top
        m_transport.play(0, countIn);
        return;
    }
    // From the part in force when it is that section, else that section's first time.
    const std::vector<int> parts = songParts();
    const int current = m_transport.position().part;
    int from = current >= 0 && std::cmp_less(current, parts.size()) && parts.at(static_cast<std::size_t>(current)) == fromSection
                   ? current
                   : -1;
    if (from < 0) {
        const auto first = std::ranges::find(parts, fromSection);
        from = first != parts.end() ? static_cast<int>(first - parts.begin()) : 0;
    }
    m_transport.play(from, countIn);
}

void RealEngine::jumpToSection(int section)
{
    GC_ONLY_MAIN_THREAD();
    const int count = static_cast<int>(m_sections.sections.size());
    if (section < 0 || section >= count) {
        qCWarning(lcEngine) << "Jump to section" << section + 1 << "ignored: the song has" << count << "sections";
        return;
    }
    if (const int at = partForSection(section); at >= 0) m_transport.jump(at);
}

// ---------------------------------------------------------------- loops

namespace {
constexpr double kLongestLoopSeconds = 120.0;
constexpr double kLongestLoopBars = 64.0;
// Room for a loop's layers; a long loop gets fewer than LoopStation::kMaxLayers.
constexpr int64_t kLayerBudgetBytes = 256LL * 1024 * 1024;
} // namespace

int RealEngine::loopSlot(const core::ChannelId& channel) const
{
    const auto it = std::ranges::find(m_loopOwners, channel);
    return channel.isNull() || it == m_loopOwners.end() ? -1 : static_cast<int>(it - m_loopOwners.begin());
}

double RealEngine::barFrames() const
{
    const double bpm = tempo();
    const double quartersPerBar = m_timeNumerator.load(std::memory_order_relaxed) * 4.0 /
                                  m_timeDenominator.load(std::memory_order_relaxed);
    return bpm > 0.0 ? quartersPerBar * 60.0 / bpm * m_audio.sampleRate() : 0.0;
}

void RealEngine::applyLoopSlots(RenderGraph& graph) const
{
    GC_ONLY_MAIN_THREAD();
    for (std::size_t i = 0; i < graph.stripCount(); ++i) graph.strip(i)->setLoopSlot(loopSlot(graph.strip(i)->id()));
}

void RealEngine::loopCommand(const core::ChannelId& channel, LoopCommand command)
{
    GC_ONLY_MAIN_THREAD();
    const auto report = [this](const QString& text) {
        m_pendingNotices.push_back(Notice::warning(text));
        qCWarning(lcEngine).noquote() << text;
    };
    int slot = loopSlot(channel);
    if (slot < 0) {
        if (command != LoopCommand::Record || channel.isNull()) return; // no loop to act on
        const auto free = std::ranges::find_if(m_loopOwners, [](const core::ChannelId& id) { return id.isNull(); });
        if (free == m_loopOwners.end()) {
            report(u"No room for another loop: %1 loops at most (clear one first)"_s.arg(LoopStation::kSlots));
            return;
        }
        slot = static_cast<int>(free - m_loopOwners.begin());
        *free = channel;
        if (RenderGraph* graph = m_exchange.current()) applyLoopSlots(*graph);
    }
    const auto index = static_cast<std::size_t>(slot);
    if (command == LoopCommand::Record && !m_loops.pending(slot) && m_loops.read(slot).state == LoopState::Empty) {
        // Room for the longest loop: 2 minutes, or 64 bars when that is shorter.
        const double rate = m_audio.sampleRate();
        const double bars = barFrames() * kLongestLoopBars;
        const auto frames = static_cast<int64_t>(bars > 0.0 ? std::min(kLongestLoopSeconds * rate, bars) : kLongestLoopSeconds * rate);
        std::shared_ptr<LoopData> data;
        try {
            data = std::make_shared<LoopData>();
            data->base = std::make_shared<LoopTake>(frames);
        } catch (const std::bad_alloc&) {
            report(u"Not enough memory to record a loop (%1 MB needed)"_s.arg(frames * int64_t{8} / (int64_t{1024} * 1024)));
            m_loopOwners.at(index) = {};
            return;
        }
        m_loops.setData(slot, std::move(data));
    }
    if (!m_loops.post(slot, command)) report(u"Too many loop presses at once: one was dropped"_s);
}

void RealEngine::stopAllLoops()
{
    GC_ONLY_MAIN_THREAD();
    for (int slot = 0; slot < LoopStation::kSlots; ++slot) {
        if (!m_loopOwners.at(static_cast<std::size_t>(slot)).isNull()) (void)m_loops.post(slot, LoopCommand::Stop);
    }
}

void RealEngine::clearAllLoops()
{
    GC_ONLY_MAIN_THREAD();
    for (int slot = 0; slot < LoopStation::kSlots; ++slot) {
        if (!m_loopOwners.at(static_cast<std::size_t>(slot)).isNull()) (void)m_loops.post(slot, LoopCommand::Clear);
    }
}

void RealEngine::serviceLoops(std::vector<Notice>& notices)
{
    GC_ONLY_MAIN_THREAD();
    const auto tell = [&notices](Notice notice) {
        qCInfo(lcEngine).noquote() << notice.text;
        notices.push_back(std::move(notice));
    };
    bool anyLoop = false;
    bool freed = false;
    for (int slot = 0; slot < LoopStation::kSlots; ++slot) {
        const auto index = static_cast<std::size_t>(slot);
        if (m_loopOwners.at(index).isNull()) continue;
        if (m_loops.takeFull(slot)) {
            tell(Notice::warning(u"A loop reached its longest (2 minutes or 64 bars) and closed there"_s));
        }
        if (m_loops.takeNoLayerLeft(slot)) {
            tell(Notice::warning(u"That loop has all the layers it can take: undo one to record another"_s));
        }
        if (m_loops.takeFault(slot)) {
            tell(Notice::warning(u"A loop's recording did not fit its memory, so it was silenced: clear it and record it again"_s));
        }
        // (Read after the ask: what it publishes with it, not what was there before.)
        if (const LoopReading closed = m_loops.takeNeedsLayers(slot) ? m_loops.read(slot) : LoopReading{}; closed.length > 0) {
            // The base cut to the take and what rang on past it, and room for layers.
            const LoopData* now = m_loops.data(slot);
            const int64_t kept = closed.take + closed.tail;
            if (now == nullptr || !now->base || closed.take <= 0 || now->base->frames() < kept) {
                tell(Notice::warning(u"A loop closed without its recording in memory (%1 of %2 frames): it plays, without layers"_s
                                         .arg(now != nullptr && now->base ? now->base->frames() : 0)
                                         .arg(kept)));
            } else {
                try {
                    auto data = std::make_shared<LoopData>();
                    data->base = std::make_shared<LoopTake>(kept);
                    std::copy_n(now->base->left.begin(), kept, data->base->left.begin());
                    std::copy_n(now->base->right.begin(), kept, data->base->right.begin());
                    const int64_t layerBytes = closed.length * 2 * static_cast<int64_t>(sizeof(float));
                    const auto layers =
                        std::clamp<int64_t>(kLayerBudgetBytes / std::max<int64_t>(layerBytes, 1), 1, LoopStation::kMaxLayers);
                    for (int64_t i = 0; i < layers; ++i) data->layers.push_back(std::make_shared<LoopTake>(closed.length));
                    m_loops.setData(slot, std::move(data));
                } catch (const std::bad_alloc&) {
                    tell(Notice::warning(u"Not enough memory for layers on that loop: it plays, without layers"_s));
                }
            }
            // Free: the first loop sets the tempo, bar 1 on it.
            if (m_tempoFromLoop && !m_loops.sync() && !m_freeTempoTaken) {
                const double quartersPerBar = m_timeNumerator.load(std::memory_order_relaxed) * 4.0 /
                                              m_timeDenominator.load(std::memory_order_relaxed);
                double best = 0.0;
                for (const double loopBars : {1.0, 2.0, 4.0}) {
                    const double bpm = quartersPerBar * loopBars * 60.0 * m_audio.sampleRate() / static_cast<double>(closed.length);
                    if (bpm >= 60.0 && bpm <= 180.0 && (best == 0.0 || std::abs(bpm - 110.0) < std::abs(best - 110.0))) best = bpm;
                }
                m_freeTempoTaken = true;
                if (best > 0.0) {
                    setTempo(best);
                    m_barOriginAt.store(m_loops.freeGrid().origin, std::memory_order_release);
                    tell(Notice::info(u"Tempo %1 BPM taken from the first loop"_s.arg(best, 0, 'f', 1)));
                } else {
                    tell(Notice::warning(u"No tempo taken from the first loop: it is not 1, 2 or 4 bars of 60-180 BPM"_s));
                }
            }
        }
        // Undone layers: fresh, silent buffers before they are recorded on again.
        const uint32_t dirty = m_loops.dirtyLayers(slot);
        if (dirty == 0) m_loopFreshSent.at(index) = 0;
        if (const LoopData* now = m_loops.data(slot);
            dirty != 0 && dirty != m_loopFreshSent.at(index) && now != nullptr && !now->layers.empty()) {
            try {
                auto fresh = std::make_shared<LoopData>(*now);
                for (std::size_t i = 0; i < fresh->layers.size(); ++i) {
                    if ((dirty & (1U << i)) != 0) fresh->layers.at(i) = std::make_shared<LoopTake>(now->layers.at(i)->frames());
                }
                fresh->freshMask = dirty;
                m_loops.setData(slot, std::move(fresh));
                m_loopFreshSent.at(index) = dirty;
            } catch (const std::bad_alloc&) {
                tell(Notice::warning(u"Not enough memory to record another layer on that loop"_s));
            }
        }
        // Cleared (and no press on its way): its room is given back. (Nothing
        // waiting first, then the state as it is now: a press just taken
        // has had its outcome said by then.)
        if (!m_loops.pending(slot) && m_loops.read(slot).state == LoopState::Empty) {
            m_loops.setData(slot, nullptr);
            m_loopOwners.at(index) = {};
            freed = true;
            continue;
        }
        anyLoop = true;
    }
    if (!anyLoop) m_freeTempoTaken = false;
    if (freed) {
        if (RenderGraph* graph = m_exchange.current()) applyLoopSlots(*graph);
    }
    m_loops.collectGarbage();
}

std::vector<ChannelLoop> RealEngine::loops() const
{
    GC_ONLY_MAIN_THREAD();
    std::vector<ChannelLoop> list;
    const double bar = m_loops.sync() ? barFrames() : 0.0;
    for (int slot = 0; slot < LoopStation::kSlots; ++slot) {
        const core::ChannelId& channel = m_loopOwners.at(static_cast<std::size_t>(slot));
        if (channel.isNull()) continue;
        const LoopReading loop = m_loops.read(slot);
        ChannelLoop item{.channel = channel, .state = loop.state, .progress = 0.0, .bar = 0, .bars = 0, .layers = loop.layers};
        if (bar > 0.0) {
            const double beat = bar / m_timeNumerator.load(std::memory_order_relaxed);
            item.beatsToGo = loop.wait > 0 ? static_cast<int>(std::ceil(static_cast<double>(loop.wait) / beat)) : 0;
        }
        if ((loop.state == LoopState::Recording || loop.state == LoopState::Closing) && bar > 0.0) {
            // Recording: which bar, and how far into it (so its end can be seen coming).
            const double bars = static_cast<double>(loop.position) / bar;
            item.bar = static_cast<int>(bars) + 1;
            item.progress = bars - std::floor(bars);
            item.bars = m_loops.targetLines(); // "2 of 4" (0: open)
        } else if (loop.length > 0) {
            item.progress = static_cast<double>(loop.position) / static_cast<double>(loop.length);
            if (bar > 0.0) {
                item.bars = std::max(1, static_cast<int>(std::lround(static_cast<double>(loop.length) / bar)));
                item.bar = std::min(item.bars, static_cast<int>(static_cast<double>(loop.position) / bar) + 1);
            }
        }
        list.push_back(item);
    }
    return list;
}

double RealEngine::tempo() const
{
    if (m_followClock.load(std::memory_order_relaxed)) {
        if (const double clock = m_midi.clockTempo(); clock > 0.0) return clock;
    }
    return m_tempo.load(std::memory_order_relaxed);
}

void RealEngine::callUpExternalSounds(const core::Patch& patch)
{
    GC_ONLY_MAIN_THREAD();
    // Once per patch chosen (or its hardware sounds changed): a rebuild of the
    // same patch (a device change, a fader) must not send the synths their
    // sound again mid-song.
    if (m_calledUp && m_calledUp->first == patch.id && m_calledUp->second == patch.externalPrograms) return;
    m_calledUp = std::pair{patch.id, patch.externalPrograms};
    for (const core::ExternalProgram& sound : patch.externalPrograms) {
        const auto channel = static_cast<unsigned char>(std::clamp(sound.midiChannel, 1, 16) - 1);
        const auto send = [this, &sound](std::initializer_list<unsigned char> bytes) {
            const std::vector<unsigned char> message(bytes);
            if (auto sent = m_externalOut.send(sound.port, message); !sent) {
                m_pendingNotices.push_back(Notice::warning(sent.error().message)); // logged by send
                return false;
            }
            return true;
        };
        // Bank select (MSB, LSB) first, then the program: the synth needs them in that order.
        if (sound.bank >= 0 && (!send({static_cast<unsigned char>(0xB0 | channel), 0, static_cast<unsigned char>((sound.bank >> 7) & 0x7F)})
                                || !send({static_cast<unsigned char>(0xB0 | channel), 32, static_cast<unsigned char>(sound.bank & 0x7F)}))) {
            continue;
        }
        if (send({static_cast<unsigned char>(0xC0 | channel), static_cast<unsigned char>(std::clamp(sound.program, 0, 127))})) {
            qCInfo(lcEngine).noquote() << "Called up program" << sound.program + 1 << "on" << sound.port << "channel" << channel + 1;
        }
    }
}

void RealEngine::setClickOutput(int pair)
{
    m_clickPair.store(std::max(pair, 0), std::memory_order_relaxed);
    qCInfo(lcEngine) << "The click plays on" << (pair <= 0 ? u"the mix (1-2)"_s : u"outputs %1-%2"_s.arg((2 * pair) + 1).arg((2 * pair) + 2));
}

void RealEngine::setClick(bool on, double volumeDb)
{
    m_click.setVolumeDb(volumeDb);
    m_click.setOn(on);
}

std::shared_ptr<PluginNode> RealEngine::currentNode(const core::ChannelId& id, int target) const
{
    if (target < 0) {
        const auto it = m_currentInstruments.find(id.value());
        return it != m_currentInstruments.end() ? it->second : nullptr;
    }
    const auto effects = m_currentEffects.find(id.value());
    if (effects == m_currentEffects.end() || std::cmp_greater_equal(target, effects->second.size())) return nullptr;
    return effects->second.at(static_cast<std::size_t>(target));
}

std::vector<PluginParameter> RealEngine::pluginParameters(const core::ChannelId& id, int target) const
{
    GC_ONLY_MAIN_THREAD();
    std::vector<PluginParameter> list;
    const auto node = currentNode(id, target);
    if (!node) return list;
    std::ranges::transform(node->parameters(), std::back_inserter(list),
                           [](const PluginNode::Parameter& p) { return PluginParameter{.id = p.id, .name = p.name}; });
    return list;
}

std::optional<PluginParameter> RealEngine::takeTouchedParameter(const core::ChannelId& id, int target)
{
    GC_ONLY_MAIN_THREAD();
    const auto node = currentNode(id, target);
    if (!node) return std::nullopt;
    const std::optional<uint32_t> touched = node->takeTouchedParameter();
    if (!touched) return std::nullopt;
    const std::vector<PluginNode::Parameter> listed = node->parameters();
    const auto found = std::ranges::find_if(listed, [&touched](const PluginNode::Parameter& p) { return p.id == *touched; });
    if (found != listed.end()) return PluginParameter{.id = found->id, .name = found->name};
    return PluginParameter{.id = *touched, .name = u"Parameter %1"_s.arg(*touched)}; // moved, but not listed as automatable
}

std::vector<QString> RealEngine::trackPaths() const
{
    if (m_trackPath.isEmpty() && m_stemPaths.empty()) return {};
    std::vector<QString> paths{m_trackPath};
    paths.insert(paths.end(), m_stemPaths.begin(), m_stemPaths.end());
    return paths;
}

void RealEngine::setBackingTrack(const QString& path)
{
    GC_ONLY_MAIN_THREAD();
    if (path == m_trackPath) return;
    m_trackPath = path;
    m_stemPaths.clear(); // another song's track: its stems come with it (setBackingStems)
    rereadTracks();
}

void RealEngine::setBackingStems(const std::vector<BackingStemFile>& stems)
{
    GC_ONLY_MAIN_THREAD();
    const std::size_t count = std::min(stems.size(), m_stemGains.size());
    std::vector<QString> paths;
    paths.reserve(count);
    for (std::size_t i = 0; i < count; ++i) paths.push_back(stems.at(i).path);
    if (stems.size() > m_stemGains.size()) {
        m_pendingNotices.push_back(Notice::warning(u"Only the first %1 stems play"_s.arg(m_stemGains.size())));
        qCWarning(lcEngine).noquote() << m_pendingNotices.back().text;
    }
    // Other files: the old set stops first, so no stem of it plays at the
    // level or on the outputs of the one now in its place (a loud stem into
    // the in-ears). The same files: a level, mute or output, at once.
    if (paths != m_stemPaths) {
        m_stemPaths = std::move(paths);
        rereadTracks();
    }
    for (std::size_t i = 0; i < count; ++i) {
        const BackingStemFile& stem = stems.at(i);
        m_stemGains.at(i).store(stem.mute ? 0.0F : dbToGain(stem.volumeDb), std::memory_order_relaxed);
        m_stemPairs.at(i).store(std::max(stem.outputPair, 0), std::memory_order_relaxed);
    }
}

void RealEngine::rereadTracks()
{
    GC_ONLY_MAIN_THREAD();
    m_trackFailed.clear();
    m_trackPlaying.store(false, std::memory_order_relaxed);
    m_trackPosition.store(0, std::memory_order_relaxed);
    m_track.publish(nullptr); // the old set stops at once
    if (m_trackReader) {
        m_cancelTrackRead.store(true, std::memory_order_relaxed); // the new one starts when it has stopped (poll)
        return;
    }
    if (const std::vector<QString> paths = trackPaths(); !paths.empty()) startReadingTracks(paths);
}

void RealEngine::seekBackingTrack(double seconds)
{
    const TrackSet* set = m_track.current();
    if (set == nullptr || !std::isfinite(seconds)) return; // nothing read yet: nothing to move
    const auto at = static_cast<int64_t>(std::llround(std::max(seconds, 0.0) * set->sampleRate));
    m_trackSeek.store(std::min(at, set->frames()), std::memory_order_relaxed);
}

void RealEngine::startReadingTracks(const std::vector<QString>& paths)
{
    GC_ONLY_MAIN_THREAD();
    m_cancelTrackRead.store(false, std::memory_order_relaxed);
    const double rate = m_audio.sampleRate();
    m_trackReader.reset(QThread::create(&RealEngine::readTracks, this, paths, rate));
    m_trackReader->setObjectName(u"BackingTrackReader"_s);
    m_trackReader->start(QThread::LowPriority);
}

void RealEngine::readTracks(const std::vector<QString>& paths, double rate) noexcept
{
    // Nothing may leave a thread (the app would end): out of memory on a big
    // set is kept here, without allocating, and said and logged by the main
    // thread (poll).
    const auto broke = [this](std::string_view why) noexcept {
        const std::size_t n = std::min(why.size(), m_trackReadWhy.size() - 1);
        std::copy_n(why.begin(), n, m_trackReadWhy.begin());
        m_trackReadWhy.at(n) = '\0';
        m_trackReadBroke.store(true, std::memory_order_release);
    };
    try {
        TrackRead read = readTrackSet(paths, rate, &m_cancelTrackRead);
        const std::scoped_lock lock(m_trackMutex);
        m_trackRead = std::move(read);
    } catch (const std::exception& e) {
        broke(e.what());
    } catch (...) {
        broke("an error of an unknown kind");
    }
}

void RealEngine::collectBackingTrack(std::vector<Notice>& notices)
{
    GC_ONLY_MAIN_THREAD();
    m_track.collectGarbage();
    const std::vector<QString> paths = trackPaths();
    if (m_trackReader && m_trackReader->isFinished()) {
        m_trackReader.reset();
        std::optional<TrackRead> read;
        {
            const std::scoped_lock lock(m_trackMutex);
            read.swap(m_trackRead);
        }
        if (m_trackReadBroke.exchange(false, std::memory_order_acquire)) {
            const QString why = u"The backing tracks could not be read: %1"_s.arg(QString::fromUtf8(m_trackReadWhy.data()));
            qCWarning(lcEngine).noquote() << why;
            notices.push_back(Notice::error(why));
            m_trackFailed = paths; // not tried again until asked for again
        }
        if (read && read->set.paths == paths) { // (else: asked for other files meanwhile; read below)
            std::ranges::transform(read->problems, std::back_inserter(notices), &Notice::error); // logged by the reader
            m_trackFailed = std::move(read->failed);
            m_trackPosition.store(0, std::memory_order_relaxed);
            if (read->set.frames() > 0) m_track.publish(std::make_shared<TrackSet>(std::move(read->set)));
        }
    }
    if (m_trackReader || paths.empty()) return;
    // Not read yet (another read was running), or read for another sample
    // rate (the audio device changed): read it (again). Files that failed
    // are not tried again until asked for again.
    const TrackSet* set = m_track.current();
    const bool current = set != nullptr && set->paths == paths && set->sampleRate == m_audio.sampleRate();
    const bool failedAlready = set == nullptr && !m_trackFailed.empty();
    if (!current && !failedAlready) {
        if (set != nullptr) {
            m_trackPlaying.store(false, std::memory_order_relaxed);
            m_track.publish(nullptr);
        }
        startReadingTracks(paths);
    }
}

void RealEngine::playBackingTrack(bool play)
{
    const TrackSet* set = m_track.current();
    if (play && (set == nullptr || set->paths != trackPaths())) return; // nothing ready to play
    if (play && m_trackPosition.load(std::memory_order_relaxed) >= set->frames()) {
        m_trackPosition.store(0, std::memory_order_relaxed); // it ended: from the start
    }
    m_trackPlaying.store(play, std::memory_order_relaxed);
}

BackingTrackState RealEngine::backingTrack() const
{
    const TrackSet* set = m_track.current();
    const bool loaded = set != nullptr && set->paths == trackPaths() && set->frames() > 0;
    const double rate = loaded ? set->sampleRate : 0.0;
    return BackingTrackState{.path = m_trackPath,
                             .loading = m_trackReader != nullptr,
                             .loaded = loaded,
                             .playing = loaded && m_trackPlaying.load(std::memory_order_relaxed),
                             .position = rate > 0.0 ? static_cast<double>(m_trackPosition.load(std::memory_order_relaxed)) / rate : 0.0,
                             .length = rate > 0.0 ? static_cast<double>(set->frames()) / rate : 0.0};
}

core::Result<std::unique_ptr<IPluginEditor>> RealEngine::createEditorForPlugin(const QString& pluginId)
{
    GC_ONLY_MAIN_THREAD();
    // A separate instance, not in the audio graph; the editor keeps it alive.
    if (m_guard.isBlocked(pluginId)) {
        return core::fail(core::ErrorCode::InvalidData, u"This plugin crashed the app while loading before, so it is switched off"_s);
    }
    if (auto installed = checkInstalled(pluginId, QFileInfo(pluginId).completeBaseName()); !installed) {
        return tl::unexpected(installed.error());
    }
    const auto loading = m_guard.loading(pluginId);
    auto node = PluginNode::load(pluginId, m_audio.sampleRate(), m_audio.maxBlock());
    if (!node) return tl::unexpected(node.error()); // logged by the plugin's load
    return PluginNode::createEditor(*node);
}

QString RealEngine::statusText() const
{
    const QStringList ports = m_midi.openPortNames();
    // Standing in for an unplugged interface: says what it waits for.
    QString output = m_audio.isOpen() ? m_audio.deviceName() : u"No audio output"_s;
    if (m_audio.standingIn() && m_audio.wanted()) output += u" (waiting for %1)"_s.arg(m_audio.wanted()->name);
    return u"%1 · %2 · %3 kHz · %4 ms · MIDI: %5"_s.arg(output, apiName(m_audio.api()))
        .arg(m_audio.sampleRate() / 1000.0, 0, 'f', 1)
        .arg(m_audio.latencyMs(), 0, 'f', 1)
        .arg(ports.isEmpty() ? u"none"_s : ports.join(u", "_s));
}

void RealEngine::render(AudioBlock out, const AudioInputs& inputs) noexcept
{
    GC_ONLY_AUDIO_THREAD();
    const auto start = std::chrono::steady_clock::now();

    std::size_t count = m_midi.drain(m_events);
    MidiEvent injected;
    while (count < m_events.size() && m_injected.pop(injected)) m_events.at(count++) = injected; // room checked
    count = takeControlMessages(count);
    m_keyboard.apply(std::span<const MidiEvent>(m_events.data(), count));

    // The clock: the set tempo, or a followed MIDI clock's (whose Start
    // begins bar 1 here too).
    double bpm = m_tempo.load(std::memory_order_relaxed);
    const bool clockStarted = m_midi.takeClockStart();
    if (m_followClock.load(std::memory_order_relaxed)) {
        if (clockStarted) {
            m_samplePosition = 0;
            m_ppq = 0.0;
        }
        if (const double clock = m_midi.clockTempo(); clock > 0.0) bpm = clock;
    }
    const double rate = m_audio.sampleRate();

    // The song's sections: which one is in force (and where it changes in
    // this block); Play and jumps move the clock.
    const double quartersPerSample = rate > 0.0 ? bpm / 60.0 / rate : 0.0;
    const SongTransport::Block song = m_transport.advance(m_timeline.acquire(), m_ppq, out.frames, quartersPerSample);
    m_timeline.release();
    m_ppq = song.ppq;
    const SectionGate gate = song.gate;
    // A free first loop set the tempo: bar 1 starts where it started.
    if (const int64_t origin = m_barOriginAt.exchange(-1, std::memory_order_acq_rel); origin >= 0) {
        m_ppq = static_cast<double>(m_samplePosition - origin) * quartersPerSample;
    }

    TimeInfo time{.tempo = bpm,
                  .sampleRate = rate,
                  .samplePosition = m_samplePosition,
                  .ppqPosition = m_ppq,
                  .barStartPpq = 0.0,
                  .timeSigNumerator = m_timeNumerator.load(std::memory_order_relaxed),
                  .timeSigDenominator = m_timeDenominator.load(std::memory_order_relaxed)};
    time.barStartPpq = std::floor(m_ppq / time.quartersPerBar()) * time.quartersPerBar();

    // The loops' bar lines: where the current bar started, a bar apart.
    LoopGrid bars;
    if (quartersPerSample > 0.0) {
        const double samplesPerQuarter = 1.0 / quartersPerSample;
        bars.unit = time.quartersPerBar() * samplesPerQuarter;
        bars.origin = m_samplePosition - std::llround((m_ppq - time.barStartPpq) * samplesPerQuarter);
    }
    m_loops.beginBlock(m_samplePosition, out.frames, bars);

    const float masterGain = m_masterGain.load(std::memory_order_relaxed);
    const std::span<float* const> sends = m_audio.extraOutputs(); // outputs 3 and up, silent so far
    RenderGraph* graph = m_exchange.acquire();
    if (graph != nullptr) {
        graph->render(std::span<const MidiEvent>(m_events.data(), count), out, masterGain, time, inputs, gate, &m_loops,
                      sends);
    } else {
        std::fill_n(out.left, out.frames, 0.0F);
        std::fill_n(out.right, out.frames, 0.0F);
    }
    m_exchange.release();
    m_loops.endBlock();

    // The backing track and its stems, locked together: the track and stems
    // in the mix through the master fader, stems on outputs of their own at
    // their own level (the in-ears, not the audience).
    const auto frames = static_cast<std::size_t>(std::max(out.frames, 0));
    if (const TrackSet* set = m_track.acquire(); set != nullptr) {
        const int64_t length = set->frames();
        if (m_trackRewind.exchange(false, std::memory_order_relaxed)) m_trackPosition.store(0, std::memory_order_relaxed);
        if (const int64_t seek = m_trackSeek.exchange(-1, std::memory_order_relaxed); seek >= 0) {
            m_trackPosition.store(std::min(seek, length), std::memory_order_relaxed); // a marker
        }
        // Played with the song: from the section's place in the track, on its first beat.
        if (song.seekTrack && bpm > 0.0) {
            const auto at = static_cast<int64_t>(std::llround(song.trackQuarter * 60.0 / bpm * set->sampleRate));
            m_trackPosition.store(std::clamp<int64_t>(at, 0, length), std::memory_order_relaxed);
        }
        if (song.stopTrack) m_trackPlaying.store(false, std::memory_order_relaxed);
        std::size_t offset = 0; // where in this block the track plays from
        if (song.startTrackAt >= 0) {
            m_trackPlaying.store(true, std::memory_order_relaxed);
            offset = static_cast<std::size_t>(song.startTrackAt);
        }
        const int64_t position = std::clamp<int64_t>(m_trackPosition.load(std::memory_order_relaxed), 0, length);
        if (m_trackPlaying.load(std::memory_order_relaxed) && offset < frames) {
            const auto n = static_cast<std::size_t>(std::min<int64_t>(static_cast<int64_t>(frames - offset), length - position));
            const float trackGain = m_trackGain.load(std::memory_order_relaxed);
            for (std::size_t c = 0; c < set->clips.size(); ++c) {
                const AudioClip& clip = set->clips.at(c);
                if (position >= clip.frames()) continue; // empty, or a shorter stem that has ended
                const auto take = std::min<std::size_t>(n, static_cast<std::size_t>(clip.frames() - position));
                const int pair = c == 0 ? 0 : m_stemPairs.at(c - 1).load(std::memory_order_relaxed);
                const AudioBlock to = sendPair(sends, pair, out);
                const bool inMix = to.left == out.left; // (a pair the device does not have: the mix)
                const float gain = trackGain * (c == 0 ? 1.0F : m_stemGains.at(c - 1).load(std::memory_order_relaxed))
                                   * (inMix ? masterGain : 1.0F);
                if (gain <= 0.0F) continue;
                const auto from = static_cast<std::size_t>(position);
                const auto add = [gain](float mix, float sample) { return mix + (sample * gain); };
                const std::span<float> left = std::span<float>(to.left, frames).subspan(offset, take);
                const std::span<float> right = std::span<float>(to.right, frames).subspan(offset, take);
                std::ranges::transform(left, std::span<const float>(clip.left).subspan(from, take), left.begin(), add);
                std::ranges::transform(right, std::span<const float>(clip.right).subspan(from, take), right.begin(), add);
            }
            m_trackPosition.store(position + static_cast<int64_t>(n), std::memory_order_relaxed);
            if (position + static_cast<int64_t>(n) >= length) m_trackPlaying.store(false, std::memory_order_relaxed);
        }
    }
    m_track.release();

    // The click, on top of everything (not through the master fader: it
    // still counts in when the band is muted); on outputs of its own when
    // sent there (the in-ears only, not the audience).
    m_click.process(sendPair(sends, m_clickPair.load(std::memory_order_relaxed), out), time);

    m_samplePosition += static_cast<int64_t>(frames);
    if (rate > 0.0) m_ppq += static_cast<double>(frames) * bpm / 60.0 / rate;

    // The safety limiter: last before the output.
    if (rate != m_limiterRate) {
        m_limiter.setSampleRate(rate);
        for (SafetyLimiter& send : m_sendLimiters) send.setSampleRate(rate);
        m_limiterRate = rate;
    }
    m_limiter.process(out);
    m_recorder.push(out.left, out.right, out.frames); // what the audience hears, when recording
    // The other outputs, each pair through its own limiter (ears are on them).
    for (std::size_t pair = 0; ((2 * pair) + 2) <= sends.size() && pair < m_sendLimiters.size(); ++pair) {
        m_sendLimiters.at(pair).process(sendPair(sends, static_cast<int>(pair) + 1, out));
    }

    // The master meter: what leaves the app.
    float peak = 0.0F;
    double sumSquares = 0.0;
    const std::span<const float> left(out.left, frames);
    const std::span<const float> right(out.right, frames);
    auto r = right.begin();
    for (const float l : left) {
        const float rr = *r++;
        peak = std::max({peak, std::abs(l), std::abs(rr)});
        sumSquares += 0.5 * (static_cast<double>(l) * l + static_cast<double>(rr) * rr);
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
