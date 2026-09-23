// THROWAWAY SPIKE: MIDI keyboard -> VST3 instrument -> ASIO output.
//
// Usage:
//   engine_spike                       list ASIO devices, MIDI inputs, VST3 plugins
//   engine_spike <plugin.vst3> [--device <name part>] [--buffer <frames>]
//                [--midi <index> | --test-notes]
//
// Approach follows what Muse (Audacity 4 / MuseScore) does, studied not copied:
//  - plugin module loaded + initialised on the main thread; controller synced
//    to component state after init (vstplugininstance.cpp)
//  - setupProcessing(kSample32, max block) -> prepare process data -> activate
//    main audio buses (fallback bus 0) -> setActive -> setProcessing
//    (vstaudioclient.cpp)
//  - teardown reversed on the main thread after the stream stops
// The audio callback never allocates, locks or logs.

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <RtAudio.h>
#include <rtmidi/RtMidi.h>

#include <conio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Steinberg;

namespace {

// ---------------------------------------------------------------- MIDI queue
// Single-producer (MIDI thread) / single-consumer (audio thread) ring buffer.
struct MidiMessage
{
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

class MidiQueue
{
public:
    bool push(const MidiMessage& message)
    {
        const uint32_t head = m_head.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % kSize;
        if (next == m_tail.load(std::memory_order_acquire)) return false; // full: drop
        m_items[head] = message;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(MidiMessage& message)
    {
        const uint32_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false;
        message = m_items[tail];
        m_tail.store((tail + 1) % kSize, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kSize = 1024;
    std::array<MidiMessage, kSize> m_items{};
    std::atomic<uint32_t> m_head{0};
    std::atomic<uint32_t> m_tail{0};
};

// ---------------------------------------------------------------- stats
struct Stats
{
    std::atomic<uint64_t> callbacks{0};
    std::atomic<uint64_t> underflows{0};
    std::atomic<uint64_t> notes{0};
    std::atomic<uint64_t> droppedMidi{0};
    std::atomic<uint64_t> oversizedBlocks{0};
    std::atomic<int64_t> maxProcessMicros{0};
    std::atomic<int64_t> lastNoteMicros{0}; // MIDI arrival -> start of the callback that played it
};

int64_t nowMicros()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- plugin host
struct Host
{
    Vst::HostApplication hostContext;
    VST3::Hosting::Module::Ptr module;
    IPtr<Vst::PlugProvider> provider;
    IPtr<Vst::IComponent> component;
    IPtr<Vst::IAudioProcessor> processor;
    IPtr<Vst::IEditController> controller;
    Vst::HostProcessData processData;
    Vst::EventList events{512};
    Vst::ParameterChanges parameterChanges;
    Vst::ProcessContext processContext{};
    std::string name;
    bool active = false;
};

bool loadPlugin(Host& host, const std::string& path, double sampleRate, int32 maxBlock)
{
    Vst::PluginContextFactory::instance().setPluginContext(&host.hostContext);

    std::string error;
    host.module = VST3::Hosting::Module::create(path, error);
    if (!host.module) {
        std::cerr << "Could not load module: " << error << "\n";
        return false;
    }

    const auto factory = host.module->getFactory();
    for (const auto& info : factory.classInfos()) {
        if (info.category() == kVstAudioEffectClass) {
            host.name = info.name();
            host.provider = owned(new Vst::PlugProvider(factory, info, true));
            break;
        }
    }
    if (!host.provider || !host.provider->initialize()) {
        std::cerr << "No audio processor class, or it failed to initialise\n";
        return false;
    }

    host.component = host.provider->getComponentPtr();
    host.controller = host.provider->getControllerPtr();
    host.processor = FUnknownPtr<Vst::IAudioProcessor>(host.component);
    if (!host.component || !host.processor) {
        std::cerr << "Plugin has no IAudioProcessor\n";
        return false;
    }

    // Sync controller to the component's default state (Muse: some plugins
    // crash in the editor without this).
    if (host.controller) {
        MemoryStream state;
        if (host.component->getState(&state) == kResultOk && state.getSize() > 0) {
            state.seek(0, IBStream::kIBSeekSet, nullptr);
            host.controller->setComponentState(&state);
        }
    }

    Vst::ProcessSetup setup{Vst::kRealtime, Vst::kSample32, maxBlock, sampleRate};
    if (host.processor->setupProcessing(setup) != kResultOk) {
        std::cerr << "setupProcessing failed\n";
        return false;
    }

    // Activate the main audio outputs (fallback: bus 0) and the first event input.
    bool anyOutput = false;
    const int32 outputBuses = host.component->getBusCount(Vst::kAudio, Vst::kOutput);
    for (int32 i = 0; i < outputBuses; ++i) {
        Vst::BusInfo info{};
        host.component->getBusInfo(Vst::kAudio, Vst::kOutput, i, info);
        if (info.busType == Vst::kMain) {
            host.component->activateBus(Vst::kAudio, Vst::kOutput, i, true);
            anyOutput = true;
        }
    }
    if (!anyOutput && outputBuses > 0) host.component->activateBus(Vst::kAudio, Vst::kOutput, 0, true);
    if (host.component->getBusCount(Vst::kEvent, Vst::kInput) > 0) {
        host.component->activateBus(Vst::kEvent, Vst::kInput, 0, true);
    }

    host.processData.prepare(*host.component, maxBlock, Vst::kSample32);
    host.processContext.sampleRate = sampleRate;
    host.processContext.tempo = 120.0;
    host.processContext.state = Vst::ProcessContext::kTempoValid;
    host.processData.inputEvents = &host.events;
    host.processData.inputParameterChanges = &host.parameterChanges;
    host.processData.processContext = &host.processContext;

    host.component->setActive(true);
    host.processor->setProcessing(true);
    host.active = true;
    return true;
}

void unloadPlugin(Host& host)
{
    if (host.active) {
        host.processor->setProcessing(false);
        host.component->setActive(false);
        host.active = false;
    }
    host.processData.unprepare();
    host.processor = nullptr;
    host.controller = nullptr;
    host.component = nullptr;
    host.provider = nullptr;
    host.module = nullptr;
    Vst::PluginContextFactory::instance().setPluginContext(nullptr);
}

// ---------------------------------------------------------------- audio
struct Engine
{
    Host host;
    MidiQueue midi;
    Stats stats;
    std::atomic<int64_t> pendingNoteArrival{0};
    unsigned int maxBlock = 0;
};

int audioCallback(void* outputBuffer, void*, unsigned int frames, double, RtAudioStreamStatus status, void* user)
{
    auto& engine = *static_cast<Engine*>(user);
    auto* out = static_cast<float*>(outputBuffer); // non-interleaved: [L...][R...]
    engine.stats.callbacks.fetch_add(1, std::memory_order_relaxed);
    if (status & RTAUDIO_OUTPUT_UNDERFLOW) engine.stats.underflows.fetch_add(1, std::memory_order_relaxed);

    if (frames > engine.maxBlock) {
        engine.stats.oversizedBlocks.fetch_add(1, std::memory_order_relaxed);
        std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0F);
        return 0;
    }

    const int64_t start = nowMicros();
    Host& host = engine.host;
    host.events.clear();
    MidiMessage message;
    while (engine.midi.pop(message)) {
        const uint8_t type = message.status & 0xF0;
        Vst::Event event{};
        event.busIndex = 0;
        event.sampleOffset = 0;
        if (type == 0x90 && message.data2 > 0) {
            event.type = Vst::Event::kNoteOnEvent;
            event.noteOn.channel = message.status & 0x0F;
            event.noteOn.pitch = message.data1;
            event.noteOn.velocity = static_cast<float>(message.data2) / 127.0F;
            event.noteOn.noteId = -1;
        } else if (type == 0x80 || type == 0x90) {
            event.type = Vst::Event::kNoteOffEvent;
            event.noteOff.channel = message.status & 0x0F;
            event.noteOff.pitch = message.data1;
            event.noteOff.velocity = 0.0F;
            event.noteOff.noteId = -1;
        } else {
            continue;
        }
        host.events.addEvent(event);
    }
    if (host.events.getEventCount() > 0) {
        const int64_t arrival = engine.pendingNoteArrival.exchange(0, std::memory_order_relaxed);
        if (arrival != 0) engine.stats.lastNoteMicros.store(start - arrival, std::memory_order_relaxed);
    }

    host.processData.numSamples = static_cast<int32>(frames);
    host.processor->process(host.processData);

    float* left = out;
    float* right = out + frames;
    if (host.processData.numOutputs > 0 && host.processData.outputs[0].numChannels > 0) {
        const auto& bus = host.processData.outputs[0];
        std::copy_n(bus.channelBuffers32[0], frames, left);
        std::copy_n(bus.channelBuffers32[bus.numChannels > 1 ? 1 : 0], frames, right);
    } else {
        std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0F);
    }

    const int64_t elapsed = nowMicros() - start;
    int64_t previous = engine.stats.maxProcessMicros.load(std::memory_order_relaxed);
    while (elapsed > previous &&
           !engine.stats.maxProcessMicros.compare_exchange_weak(previous, elapsed, std::memory_order_relaxed)) {
    }
    return 0;
}

void midiCallback(double, std::vector<unsigned char>* bytes, void* user)
{
    auto& engine = *static_cast<Engine*>(user);
    if (bytes == nullptr || bytes->size() < 3) return;
    const uint8_t type = (*bytes)[0] & 0xF0;
    if (type != 0x80 && type != 0x90) return;
    if (type == 0x90 && (*bytes)[2] > 0) {
        engine.stats.notes.fetch_add(1, std::memory_order_relaxed);
        engine.pendingNoteArrival.store(nowMicros(), std::memory_order_relaxed);
    }
    if (!engine.midi.push(MidiMessage{(*bytes)[0], (*bytes)[1], (*bytes)[2]})) {
        engine.stats.droppedMidi.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------- listing
std::vector<fs::path> findVst3(const fs::path& root)
{
    std::vector<fs::path> found;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->path().extension() == ".vst3") {
            found.push_back(it->path());
            if (it->is_directory()) it.disable_recursion_pending(); // a bundle: don't look inside
        }
    }
    return found;
}

void listEverything(RtAudio& audio)
{
    std::cout << "ASIO devices:\n";
    for (unsigned int id : audio.getDeviceIds()) {
        const auto info = audio.getDeviceInfo(id);
        std::cout << "  [" << id << "] " << info.name << "  outs=" << info.outputChannels
                  << "  preferredRate=" << info.preferredSampleRate << "\n";
    }
    RtMidiIn midiIn;
    std::cout << "MIDI inputs:\n";
    for (unsigned int i = 0; i < midiIn.getPortCount(); ++i) {
        std::cout << "  [" << i << "] " << midiIn.getPortName(i) << "\n";
    }
    std::cout << "VST3 plugins in C:\\Program Files\\Common Files\\VST3:\n";
    for (const auto& path : findVst3("C:\\Program Files\\Common Files\\VST3")) {
        std::cout << "  " << path.string() << "\n";
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::string pluginPath;
    std::string deviceFilter;
    unsigned int bufferFrames = 128;
    int midiPort = 0;
    bool testNotes = false;
    int runSeconds = 0; // 0 = run until a key is pressed
    unsigned int rateOverride = 0;
    RtAudio::Api api = RtAudio::WINDOWS_ASIO;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--device" && i + 1 < argc) deviceFilter = argv[++i];
        else if (arg == "--buffer" && i + 1 < argc) bufferFrames = static_cast<unsigned int>(std::stoul(argv[++i]));
        else if (arg == "--midi" && i + 1 < argc) midiPort = std::stoi(argv[++i]);
        else if (arg == "--test-notes") testNotes = true;
        else if (arg == "--seconds" && i + 1 < argc) runSeconds = std::stoi(argv[++i]);
        else if (arg == "--rate" && i + 1 < argc) rateOverride = static_cast<unsigned int>(std::stoul(argv[++i]));
        else if (arg == "--wasapi") api = RtAudio::WINDOWS_WASAPI;
        else pluginPath = arg;
    }

    RtAudio audio(api, [](RtAudioErrorType, const std::string& text) {
        std::cerr << "RtAudio: " << text << "\n";
    });
    if (pluginPath.empty()) {
        listEverything(audio);
        return 0;
    }

    unsigned int deviceId = 0;
    for (unsigned int id : audio.getDeviceIds()) {
        const auto info = audio.getDeviceInfo(id);
        if (info.outputChannels >= 2 && (deviceFilter.empty() || info.name.find(deviceFilter) != std::string::npos)) {
            deviceId = id;
            break;
        }
    }
    if (deviceId == 0) {
        std::cerr << "No ASIO output device found" << (deviceFilter.empty() ? "" : " matching filter") << "\n";
        return 1;
    }
    const auto deviceInfo = audio.getDeviceInfo(deviceId);
    const unsigned int sampleRate =
        rateOverride ? rateOverride : (deviceInfo.preferredSampleRate ? deviceInfo.preferredSampleRate : 48000);

    auto engine = std::make_unique<Engine>();
    engine->maxBlock = std::max(bufferFrames, 4096U); // ASIO may hand us another size; be generous
    std::cout << "Loading " << pluginPath << " ...\n";
    const auto loadStart = std::chrono::steady_clock::now();
    if (!loadPlugin(engine->host, pluginPath, sampleRate, static_cast<int32>(engine->maxBlock))) return 1;
    const auto loadMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - loadStart).count();
    std::cout << "Loaded \"" << engine->host.name << "\" in " << loadMs << " ms\n";

    RtAudio::StreamParameters output;
    output.deviceId = deviceId;
    output.nChannels = 2;
    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
    unsigned int frames = bufferFrames;
    if (audio.openStream(&output, nullptr, RTAUDIO_FLOAT32, sampleRate, &frames, &audioCallback, engine.get(),
                         &options) != RTAUDIO_NO_ERROR) {
        unloadPlugin(engine->host);
        return 1;
    }

    std::unique_ptr<RtMidiIn> midiIn;
    if (!testNotes) {
        midiIn = std::make_unique<RtMidiIn>();
        if (midiIn->getPortCount() == 0) {
            std::cout << "No MIDI inputs; falling back to --test-notes\n";
            testNotes = true;
            midiIn.reset();
        } else {
            midiIn->openPort(static_cast<unsigned int>(midiPort));
            midiIn->setCallback(&midiCallback, engine.get());
            midiIn->ignoreTypes(true, true, true);
            std::cout << "MIDI in: " << midiIn->getPortName(static_cast<unsigned int>(midiPort)) << "\n";
        }
    }

    audio.startStream();
    const double bufferMs = 1000.0 * frames / sampleRate;
    const long latencyFrames = audio.getStreamLatency();
    std::cout << "ASIO device: " << deviceInfo.name << "\n"
              << "Sample rate: " << sampleRate << " Hz, buffer: " << frames << " frames (" << bufferMs << " ms)\n"
              << "Driver-reported output latency: " << latencyFrames << " frames ("
              << 1000.0 * static_cast<double>(latencyFrames) / sampleRate << " ms)\n"
              << "Play your keyboard. Press any key to stop.\n";

    std::thread testThread;
    std::atomic<bool> stopTest{false};
    if (testNotes) {
        testThread = std::thread([&engine, &stopTest] {
            const uint8_t chord[] = {60, 64, 67, 72};
            while (!stopTest.load()) {
                for (const uint8_t note : chord) {
                    if (stopTest.load()) break;
                    std::vector<unsigned char> on{0x90, note, 100};
                    midiCallback(0, &on, engine.get());
                    std::this_thread::sleep_for(std::chrono::milliseconds(350));
                    std::vector<unsigned char> off{0x80, note, 0};
                    midiCallback(0, &off, engine.get());
                }
            }
        });
    }

    const double budgetMicros = bufferMs * 1000.0;
    for (int elapsed = 0; runSeconds > 0 ? elapsed < runSeconds : !_kbhit(); ++elapsed) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto& s = engine->stats;
        const int64_t maxMicros = engine->stats.maxProcessMicros.exchange(0);
        std::printf("callbacks=%llu underflows=%llu notes=%llu dropped=%llu | worst process %.2f ms (%.0f%% of "
                    "buffer) | last note->audio %.2f ms\n",
                    static_cast<unsigned long long>(s.callbacks.load()),
                    static_cast<unsigned long long>(s.underflows.load()),
                    static_cast<unsigned long long>(s.notes.load()),
                    static_cast<unsigned long long>(s.droppedMidi.load()), maxMicros / 1000.0,
                    100.0 * static_cast<double>(maxMicros) / budgetMicros, s.lastNoteMicros.load() / 1000.0);
        std::fflush(stdout);
    }
    if (runSeconds == 0) (void)_getch();

    stopTest = true;
    if (testThread.joinable()) testThread.join();
    if (midiIn) midiIn->closePort();
    if (audio.isStreamRunning()) audio.stopStream();
    if (audio.isStreamOpen()) audio.closeStream();
    unloadPlugin(engine->host);
    std::cout << "Stopped cleanly.\n";
    return 0;
}

