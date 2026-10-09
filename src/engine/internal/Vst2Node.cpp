#include "Vst2Node.h"

#include "EngineLog.h"
#include "Vst2Abi.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Checks.h"
#include "gigchain/platform/PluginFolders.h"
#include "gigchain/platform/Process.h"

#include <QDataStream>
#include <QFileInfo>
#include <QLibrary>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <exception>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

// MIDI messages given to a plugin in one block (as many as a strip can route,
// plus a note-off for every held note when its notes are let go).
constexpr std::size_t kMaxEvents = static_cast<std::size_t>(kMaxStripEventsPerBlock) + 128;

// Whether the calling thread is in a plugin's process() (the plugin may ask).
bool& inProcess()
{
    thread_local bool in = false;
    return in;
}

// What we call ourselves when a plugin asks (the branding's names).
const std::string& vendorName()
{
    static const std::string name = branding::organization().toStdString();
    return name;
}
const std::string& productName()
{
    static const std::string name = branding::name().toStdString();
    return name;
}
constexpr int32_t kVstVersion = 2400; // the version of VST2 this host speaks

// The events list a plugin is given: the layout of vst2::Events, with room.
struct EventList
{
    int32_t numEvents = 0;
    intptr_t reserved = 0;
    std::array<vst2::MidiEvent*, kMaxEvents> events{};
};
static_assert(offsetof(EventList, events) == offsetof(vst2::Events, events));

// A C string the plugin wrote into `text` (it may not end it).
QString fromPlugin(const std::array<char, 256>& text)
{
    const auto end = std::ranges::find(text, '\0');
    return QString::fromLocal8Bit(text.data(), end - text.begin()).trimmed();
}

// Copies `text` for a plugin that gave room for `room` bytes (ended).
void toPlugin(void* ptr, std::string_view text, std::size_t room)
{
    if (ptr == nullptr || room == 0) return;
    const std::span<char> buffer(static_cast<char*>(ptr), room);
    const std::size_t n = std::min(text.size(), room - 1);
    std::ranges::copy(text.substr(0, n), buffer.begin());
    buffer.subspan(n, 1).front() = '\0';
}

intptr_t hostCallback(vst2::Effect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);

} // namespace

struct Vst2Node::Impl
{
    QString path;
    QString name;
    QLibrary library;
    vst2::Effect* effect = nullptr;
    bool instrument = false;
    bool active = false;
    double sampleRate = 48000.0;
    int maxBlock = 0;

    // The plugin's buffers (as many as it has inputs and outputs).
    std::vector<std::vector<float>> inputs;
    std::vector<std::vector<float>> outputs;
    std::vector<float*> inputPointers;
    std::vector<float*> outputPointers;
    // Audio thread: this block's events, and where the music is.
    std::array<vst2::MidiEvent, kMaxEvents> midi{};
    EventList eventList;
    vst2::TimeInfo time{};
    std::bitset<std::size_t{16} * 128> heldNotes; // channel * 128 + note

    std::atomic<bool> releaseRequested{false};
    std::atomic<uint64_t> droppedEvents{0};
    std::atomic<uint64_t> oversizedBlocks{0};
    // A parameter changed in the plugin's own window (any thread may say).
    std::atomic<bool> edited{false};
    std::atomic<int32_t> touched{-1};
    // Main thread: the editor open, for the plugin's size requests.
    std::function<void(int, int)> resized;

    intptr_t call(int32_t opcode, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr, float opt = 0.0F) const
    {
        return effect->dispatcher(effect, opcode, index, value, ptr, opt);
    }

    QString text(int32_t opcode, int32_t index = 0) const
    {
        std::array<char, 256> buffer{};
        call(opcode, index, 0, buffer.data());
        return fromPlugin(buffer);
    }

    // Suspended -> sized -> resumed, as a host must (Audacity 4: VSTWrapper).
    void activate(double rate, int block)
    {
        sampleRate = rate;
        maxBlock = block;
        call(vst2::op::kSetSampleRate, 0, 0, nullptr, static_cast<float>(rate));
        call(vst2::op::kSetBlockSize, 0, block);
        const auto size = static_cast<std::size_t>(std::max(block, 0));
        inputs.assign(static_cast<std::size_t>(std::max(effect->numInputs, 0)), std::vector<float>(size, 0.0F));
        outputs.assign(static_cast<std::size_t>(std::max(effect->numOutputs, 0)), std::vector<float>(size, 0.0F));
        // (Not const: the plugin writes through these pointers.)
        // cppcheck-suppress constParameterReference
        const auto start = [](std::vector<float>& buffer) { return buffer.data(); };
        inputPointers.clear();
        outputPointers.clear();
        std::ranges::transform(inputs, std::back_inserter(inputPointers), start);
        std::ranges::transform(outputs, std::back_inserter(outputPointers), start);
        call(vst2::op::kMainsChanged, 0, 1);
        call(vst2::op::kStartProcess);
        active = true;
    }

    void deactivate()
    {
        if (!active) return;
        call(vst2::op::kStopProcess);
        call(vst2::op::kMainsChanged, 0, 0);
        active = false;
    }

    void setTime(const TimeInfo& now)
    {
        time = vst2::TimeInfo{};
        time.samplePos = static_cast<double>(now.samplePosition);
        time.sampleRate = now.sampleRate;
        time.ppqPos = now.ppqPosition;
        time.tempo = now.tempo;
        time.barStartPos = now.barStartPpq;
        time.timeSigNumerator = now.timeSigNumerator;
        time.timeSigDenominator = now.timeSigDenominator;
        // A live rig's clock always runs (TimeInfo).
        time.flags = vst2::timeFlag::kTransportPlaying | vst2::timeFlag::kPpqPosValid | vst2::timeFlag::kTempoValid
                     | vst2::timeFlag::kBarsValid | vst2::timeFlag::kTimeSigValid;
    }

    // Audio thread: one message for the plugin this block (counted when there is no room).
    void add(uint8_t status, uint8_t data1, uint8_t data2, int offset)
    {
        const auto at = static_cast<std::size_t>(eventList.numEvents);
        if (at >= midi.size()) {
            droppedEvents.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        vst2::MidiEvent& event = midi.at(at);
        event = vst2::MidiEvent{};
        event.type = vst2::kMidiType;
        event.byteSize = static_cast<int32_t>(sizeof(vst2::MidiEvent));
        event.deltaFrames = std::clamp(offset, 0, std::max(maxBlock - 1, 0));
        event.midiData.at(0) = static_cast<char>(status);
        event.midiData.at(1) = static_cast<char>(data1);
        event.midiData.at(2) = static_cast<char>(data2);
        eventList.events.at(at) = &event;
        ++eventList.numEvents;
    }
};

namespace {

intptr_t hostCallback(vst2::Effect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float /*opt*/)
{
    // While the library starts, before it is ours, the plugin may already ask.
    auto* impl = effect != nullptr ? static_cast<Vst2Node::Impl*>(effect->user) : nullptr;
    switch (opcode) {
    case vst2::host::kVersion: return kVstVersion;
    case vst2::host::kWantMidi: return 1;
    // (kCurrentId: 0, as no shell plugins are played yet; kIdle: nothing to do. Both: default.)
    case vst2::host::kGetTime:
        return impl != nullptr ? reinterpret_cast<intptr_t>(&impl->time) : 0; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): VST2 returns pointers as integers
    case vst2::host::kGetSampleRate: return impl != nullptr ? static_cast<intptr_t>(impl->sampleRate) : 48000;
    case vst2::host::kGetBlockSize: return impl != nullptr ? impl->maxBlock : 512;
    // (inProcess() is this thread's flag, set by process(): not always false, as a static reading takes it.)
    // cppcheck-suppress knownConditionTrueFalse
    case vst2::host::kGetCurrentProcessLevel: return inProcess() ? 2 : 1; // 2: the audio thread, 1: the user's
    case vst2::host::kGetVendorString: toPlugin(ptr, vendorName(), 64); return 1;
    case vst2::host::kGetProductString: toPlugin(ptr, productName(), 64); return 1;
    case vst2::host::kGetVendorVersion:
    case vst2::host::kGetLanguage: return 1; // version 1; English
    case vst2::host::kCanDo: {
        if (ptr == nullptr) return 0;
        const std::string_view asked(static_cast<const char*>(ptr));
        for (const std::string_view can : {"sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "sizeWindow", "supplyIdle"}) {
            if (asked == can) return 1;
        }
        return 0;
    }
    case vst2::host::kAutomate:
    case vst2::host::kBeginEdit:
    case vst2::host::kEndEdit:
    case vst2::host::kUpdateDisplay: // (a preset chosen in its window)
        if (impl != nullptr) {
            // A parameter moved in its window ("learn"); end and update say no which.
            if (opcode == vst2::host::kAutomate || opcode == vst2::host::kBeginEdit) impl->touched.store(index, std::memory_order_relaxed);
            impl->edited.store(true, std::memory_order_relaxed);
        }
        return 1;
    case vst2::host::kSizeWindow:
        // (Not from the audio thread: the window is the main thread's.)
        // cppcheck-suppress knownConditionTrueFalse
        if (impl == nullptr || !impl->resized || inProcess()) return 0;
        impl->resized(index, static_cast<int>(value)); // width, height
        return 1;
    default: return 0; // not supported: the plugin carries on without it
    }
}

} // namespace

// ---------------------------------------------------------------- loading

namespace {

// Opens the library and starts the plugin in it (not yet opened).
core::Result<vst2::Effect*> start(QLibrary& library, const QString& path)
{
    const QString name = QFileInfo(path).completeBaseName();
    if (!QFileInfo::exists(path)) return core::fail(core::ErrorCode::FileNotFound, u"%1: no such plugin (%2)"_s.arg(name, path));
    const platform::SilentLoaderErrors silent; // no system dialog about a missing library
    library.setFileName(platform::vst2LibraryFile(path));
    if (!library.load()) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 could not be loaded: %2"_s.arg(name, library.errorString()));
    }
    QFunctionPointer entry = library.resolve("VSTPluginMain");
    if (entry == nullptr) entry = library.resolve("main_macho"); // old Mac plugins
    if (entry == nullptr) entry = library.resolve("main");       // old plugins
    if (entry == nullptr) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is not a VST2 plugin (no VSTPluginMain)"_s.arg(name));
    }
    auto* main = reinterpret_cast<vst2::EntryPoint>(entry); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): a plugin's entry point, by its documented type
    vst2::Effect* effect = main(&hostCallback);
    if (effect == nullptr) return core::fail(core::ErrorCode::InvalidData, u"%1 did not start"_s.arg(name));
    if (effect->magic != vst2::kMagic) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is not a VST2 plugin (wrong signature)"_s.arg(name));
    }
    if (effect->dispatcher(effect, vst2::op::kGetPlugCategory, 0, 0, nullptr, 0.0F) == vst2::kCategoryShell) {
        effect->dispatcher(effect, vst2::op::kClose, 0, 0, nullptr, 0.0F);
        return core::fail(core::ErrorCode::InvalidData,
                          u"%1 holds several plugins in one file (a \"shell\", as Waves'): not supported yet"_s.arg(name));
    }
    if ((effect->flags & vst2::flag::kCanReplacing) == 0 && effect->processAccumulating == nullptr) {
        effect->dispatcher(effect, vst2::op::kClose, 0, 0, nullptr, 0.0F);
        return core::fail(core::ErrorCode::InvalidData, u"%1 cannot process audio"_s.arg(name));
    }
    return effect;
}

} // namespace

core::Result<Vst2Node::Info> Vst2Node::readInfo(const QString& path)
{
    try {
        QLibrary library;
        auto started = start(library, path);
        if (!started) return tl::unexpected(started.error());
        vst2::Effect* effect = *started;
        Impl probe; // only for the plugin's questions while it is read
        probe.effect = effect;
        effect->user = &probe;
        probe.call(vst2::op::kOpen);
        Info info;
        info.name = probe.text(vst2::op::kGetEffectName);
        if (info.name.isEmpty()) info.name = probe.text(vst2::op::kGetProductString);
        if (info.name.isEmpty()) info.name = QFileInfo(path).completeBaseName();
        info.vendor = probe.text(vst2::op::kGetVendorString);
        const intptr_t version = probe.call(vst2::op::kGetVendorVersion);
        info.version = version > 0 ? QString::number(version) : QString::number(effect->version);
        info.instrument = (effect->flags & vst2::flag::kIsSynth) != 0
                          || probe.call(vst2::op::kGetPlugCategory) == vst2::kCategorySynth;
        info.uniqueId = effect->uniqueId;
        probe.call(vst2::op::kClose);
        // (The library stays loaded: some plugins crash when unloaded; the scanner ends anyway.)
        return info;
    } catch (const std::exception& e) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 failed while being read: %2"_s.arg(QFileInfo(path).completeBaseName(), QString::fromUtf8(e.what())));
    } catch (...) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 failed while being read"_s.arg(QFileInfo(path).completeBaseName()));
    }
}

core::Result<std::shared_ptr<Vst2Node>> Vst2Node::loadUnlogged(const QString& path, double sampleRate, int maxBlock)
{
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    auto started = start(impl->library, path);
    if (!started) return tl::unexpected(started.error());
    impl->effect = *started;
    impl->effect->user = impl.get();
    // Rate and block size, opened, and again (some plugins ignore them before
    // opening): Audacity 4's order.
    impl->call(vst2::op::kSetSampleRate, 0, 0, nullptr, static_cast<float>(sampleRate));
    impl->call(vst2::op::kSetBlockSize, 0, maxBlock);
    impl->call(vst2::op::kOpen);
    impl->name = impl->text(vst2::op::kGetEffectName);
    if (impl->name.isEmpty()) impl->name = impl->text(vst2::op::kGetProductString);
    if (impl->name.isEmpty()) impl->name = QFileInfo(path).completeBaseName();
    impl->instrument = (impl->effect->flags & vst2::flag::kIsSynth) != 0
                       || impl->call(vst2::op::kGetPlugCategory) == vst2::kCategorySynth;
    impl->activate(sampleRate, maxBlock);
    return std::make_shared<Vst2Node>(Token{}, std::move(impl));
}

core::Result<std::shared_ptr<Vst2Node>> Vst2Node::open(const QString& path, double sampleRate, int maxBlock)
{
    GC_ONLY_MAIN_THREAD();
    core::Result<std::shared_ptr<Vst2Node>> node = core::fail(core::ErrorCode::InvalidData, QString());
    try {
        node = loadUnlogged(path, sampleRate, maxBlock);
    } catch (const std::exception& e) {
        node = core::fail(core::ErrorCode::InvalidData, u"%1 failed while loading: %2"_s.arg(QFileInfo(path).completeBaseName(), QString::fromUtf8(e.what())));
    } catch (...) {
        node = core::fail(core::ErrorCode::InvalidData, u"%1 failed while loading"_s.arg(QFileInfo(path).completeBaseName()));
    }
    if (node) {
        qCInfo(lcEngine).noquote() << "Loaded VST2 plugin" << (*node)->name() << "from" << path;
    } else {
        qCWarning(lcEngine).noquote() << node.error().message;
    }
    return node;
}

Vst2Node::Vst2Node(Token, std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

Vst2Node::~Vst2Node()
{
    Impl& impl = *m_impl;
    if (impl.effect == nullptr) return;
    try {
        impl.deactivate();
        impl.call(vst2::op::kClose); // the plugin frees itself
    } catch (...) {
        qCWarning(lcEngine).noquote() << impl.name << "failed while closing";
    }
    impl.effect = nullptr;
    // The library is not unloaded: some plugins crash when they are (their
    // threads, their atexit handlers). It stays until the app ends.
}

core::Result<void> Vst2Node::prepare(double sampleRate, int maxBlock)
{
    GC_ONLY_MAIN_THREAD();
    m_impl->deactivate();
    m_impl->activate(sampleRate, maxBlock);
    return {};
}

// ---------------------------------------------------------------- audio

void Vst2Node::process(std::span<const MidiEvent> events, AudioBlock io, const TimeInfo& time)
{
    Impl& impl = *m_impl;
    const auto frames = static_cast<std::size_t>(std::max(io.frames, 0));
    if (!impl.active || io.frames <= 0 || io.frames > impl.maxBlock) {
        if (io.frames > impl.maxBlock) impl.oversizedBlocks.fetch_add(1, std::memory_order_relaxed);
        std::fill_n(io.left, frames, 0.0F);
        std::fill_n(io.right, frames, 0.0F);
        return;
    }
    inProcess() = true;
    impl.setTime(time);
    impl.eventList.numEvents = 0;
    if (impl.releaseRequested.exchange(false, std::memory_order_acquire) && impl.heldNotes.any()) {
        for (std::size_t i = 0; i < impl.heldNotes.size(); ++i) {
            if (impl.heldNotes.test(i)) impl.add(static_cast<uint8_t>(0x80 | (i / 128)), static_cast<uint8_t>(i % 128), 0, 0);
        }
        impl.heldNotes.reset();
    }
    for (const MidiEvent& e : events) {
        const int type = e.status & 0xF0;
        if (type < 0x80 || type == 0xF0) continue; // clock and system messages are not for instruments
        const auto key = (static_cast<std::size_t>(e.status & 0x0F) * 128) + (e.data1 & 0x7F);
        if (type == 0x90 && e.data2 > 0) impl.heldNotes.set(key);
        else if (type == 0x80 || type == 0x90) impl.heldNotes.reset(key);
        impl.add(e.status, e.data1, e.data2, e.sampleOffset);
    }
    if (impl.eventList.numEvents > 0) impl.call(vst2::op::kProcessEvents, 0, 0, &impl.eventList);

    // Effects read their input; instruments get silence there.
    if (!impl.inputs.empty()) {
        std::copy_n(io.left, frames, impl.inputs.front().begin());
        if (impl.inputs.size() > 1) std::copy_n(io.right, frames, impl.inputs.at(1).begin());
    }
    for (auto& output : impl.outputs) std::fill_n(output.begin(), frames, 0.0F); // (an old plugin adds to them)
    const auto n = static_cast<int32_t>(io.frames);
    if ((impl.effect->flags & vst2::flag::kCanReplacing) != 0 && impl.effect->processReplacing != nullptr) {
        impl.effect->processReplacing(impl.effect, impl.inputPointers.data(), impl.outputPointers.data(), n);
    } else {
        impl.effect->processAccumulating(impl.effect, impl.inputPointers.data(), impl.outputPointers.data(), n);
    }
    if (impl.outputs.empty()) {
        std::fill_n(io.left, frames, 0.0F);
        std::fill_n(io.right, frames, 0.0F);
    } else {
        std::copy_n(impl.outputs.front().begin(), frames, io.left);
        std::copy_n((impl.outputs.size() > 1 ? impl.outputs.at(1) : impl.outputs.front()).begin(), frames, io.right);
    }
    inProcess() = false;
}

void Vst2Node::queueParameter(uint32_t id, double value, int32_t /*sampleOffset*/) noexcept
{
    Impl& impl = *m_impl;
    if (std::cmp_greater_equal(id, impl.effect->numParams)) return;
    // (VST2 has no sample-accurate parameters: it takes effect for the next block.)
    impl.effect->setParameter(impl.effect, static_cast<int32_t>(id), static_cast<float>(std::clamp(value, 0.0, 1.0)));
}

bool Vst2Node::holdsNotes() const noexcept
{
    return m_impl->heldNotes.any();
}

double Vst2Node::parameterValue(uint32_t id) const
{
    GC_ONLY_MAIN_THREAD();
    const Impl& impl = *m_impl;
    if (std::cmp_greater_equal(id, impl.effect->numParams)) return -1.0;
    return std::clamp(static_cast<double>(impl.effect->getParameter(impl.effect, static_cast<int32_t>(id))), 0.0, 1.0);
}

std::vector<PluginNode::Parameter> Vst2Node::parameters() const
{
    GC_ONLY_MAIN_THREAD();
    const Impl& impl = *m_impl;
    std::vector<Parameter> list;
    list.reserve(static_cast<std::size_t>(std::max(impl.effect->numParams, 0)));
    for (int32_t i = 0; i < impl.effect->numParams; ++i) {
        QString title = impl.text(vst2::op::kGetParamName, i);
        if (title.isEmpty()) title = u"Parameter %1"_s.arg(i + 1);
        list.push_back(Parameter{.id = static_cast<uint32_t>(i), .name = title});
    }
    return list;
}

std::optional<uint32_t> Vst2Node::takeTouchedParameter()
{
    GC_ONLY_MAIN_THREAD();
    const int32_t index = m_impl->touched.exchange(-1, std::memory_order_relaxed);
    if (index < 0) return std::nullopt;
    return static_cast<uint32_t>(index);
}

PluginNode::Problems Vst2Node::takeProblems()
{
    return Problems{.processFailures = 0,
                    .droppedEvents = m_impl->droppedEvents.exchange(0),
                    .oversizedBlocks = m_impl->oversizedBlocks.exchange(0)};
}

void Vst2Node::releaseAllNotes()
{
    m_impl->releaseRequested.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------- settings

namespace {

constexpr QByteArrayView kStateMagic = "GCV2";
constexpr quint32 kMaxRawStateBytes = 256U * 1024 * 1024;
constexpr quint8 kChunk = 1;  // the plugin's own blob
constexpr quint8 kValues = 2; // every parameter's value

} // namespace

core::Result<QByteArray> Vst2Node::saveEncodedState() const
{
    GC_ONLY_MAIN_THREAD();
    const Impl& impl = *m_impl;
    try {
        QByteArray raw;
        QDataStream out(&raw, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        if ((impl.effect->flags & vst2::flag::kProgramChunks) != 0) {
            void* data = nullptr;
            const intptr_t size = impl.call(vst2::op::kGetChunk, 0, 0, static_cast<void*>(&data)); // the whole bank
            if (size <= 0 || data == nullptr || std::cmp_greater(size, kMaxRawStateBytes)) {
                return core::fail(core::ErrorCode::InvalidData, u"%1 did not give its settings"_s.arg(impl.name));
            }
            out << kChunk << QByteArray(static_cast<const char*>(data), static_cast<qsizetype>(size));
        } else {
            QList<float> values;
            for (int32_t i = 0; i < impl.effect->numParams; ++i) values << impl.effect->getParameter(impl.effect, i);
            out << kValues << values;
        }
        return kStateMagic.toByteArray() + qCompress(raw, 9);
    } catch (const std::exception& e) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 failed giving its settings: %2"_s.arg(impl.name, QString::fromUtf8(e.what())));
    } catch (...) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 failed giving its settings"_s.arg(impl.name));
    }
}

core::Result<void> Vst2Node::restoreEncodedState(const QByteArray& bytes)
{
    GC_ONLY_MAIN_THREAD();
    Impl& impl = *m_impl;
    const auto damaged = [](const QString& why) {
        return core::fail(core::ErrorCode::InvalidData, u"The saved plugin settings are damaged (%1)"_s.arg(why));
    };
    if (!bytes.startsWith(kStateMagic)) {
        return damaged(bytes.startsWith("GCS1") ? u"they are a VST3 plugin's"_s : u"not in the expected format"_s);
    }
    const QByteArray packed = bytes.mid(kStateMagic.size());
    if (packed.size() < 5) return damaged(u"too short"_s);
    const auto rawSize = qFromBigEndian<quint32>(packed.constData());
    if (rawSize == 0 || rawSize > kMaxRawStateBytes) return damaged(u"impossible size"_s);
    const QByteArray raw = qUncompress(packed);
    if (raw.size() != static_cast<qsizetype>(rawSize)) return damaged(u"could not be unpacked"_s);
    QDataStream in(raw);
    in.setVersion(QDataStream::Qt_6_0);
    quint8 kind = 0;
    in >> kind;
    try {
        if (kind == kChunk) {
            QByteArray chunk;
            in >> chunk;
            if (in.status() != QDataStream::Ok || !in.atEnd() || chunk.isEmpty()) return damaged(u"unreadable contents"_s);
            // The plugin may keep the pointer while it reads: its own copy.
            QByteArray copy(chunk.constData(), chunk.size());
            impl.call(vst2::op::kSetChunk, 0, copy.size(), copy.data());
        } else if (kind == kValues) {
            QList<float> values;
            in >> values;
            if (in.status() != QDataStream::Ok || !in.atEnd()) return damaged(u"unreadable contents"_s);
            const auto count = std::min<qsizetype>(values.size(), impl.effect->numParams);
            for (qsizetype i = 0; i < count; ++i) {
                impl.effect->setParameter(impl.effect, static_cast<int32_t>(i), std::clamp(values.at(i), 0.0F, 1.0F));
            }
        } else {
            return damaged(u"unknown kind %1"_s.arg(kind));
        }
    } catch (const std::exception& e) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 failed taking its saved settings: %2"_s.arg(impl.name, QString::fromUtf8(e.what())));
    } catch (...) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 failed taking its saved settings"_s.arg(impl.name));
    }
    return {};
}

bool Vst2Node::takeEdited()
{
    GC_ONLY_MAIN_THREAD();
    return m_impl->edited.exchange(false, std::memory_order_relaxed);
}

QString Vst2Node::bundlePath() const
{
    return m_impl->path;
}

QString Vst2Node::name() const
{
    return m_impl->name;
}

bool Vst2Node::isInstrument() const
{
    return m_impl->instrument;
}

// ---------------------------------------------------------------- editor

namespace {

// A VST2 plugin's own window, in the native window the UI gives. The plugin
// draws when it is idled (effEditIdle, about 30 times a second, as hosts do)
// and asks for another size through the host (sizeWindow).
class Vst2Editor final : public IPluginEditor
{
public:
    Vst2Editor(std::shared_ptr<Vst2Node> node, Vst2Node::Impl& impl) : m_node(std::move(node)), m_impl(impl)
    {
        m_idle.setInterval(33);
        m_idle.callOnTimeout([this] { m_impl.call(vst2::op::kEditIdle); });
    }
    ~Vst2Editor() override { detach(); }
    Vst2Editor(const Vst2Editor&) = delete;
    Vst2Editor& operator=(const Vst2Editor&) = delete;
    Vst2Editor(Vst2Editor&&) = delete;
    Vst2Editor& operator=(Vst2Editor&&) = delete;

    [[nodiscard]] QString title() const override { return m_impl.name; }

    [[nodiscard]] QSize preferredSize() const override
    {
        if (!m_size.isEmpty()) return m_size;
        return askedSize();
    }

    [[nodiscard]] bool isAttached() const override { return m_attached; }

    core::Result<void> attach(NativeParent parent) override
    {
        if (m_attached) return {};
        if (parent.handle == 0) return logged(core::fail(core::ErrorCode::InvalidData, u"%1: no window to attach the editor to"_s.arg(m_impl.name)));
        if (parent.kind == platform::NativeWindowKind::None) {
            return logged(core::fail(core::ErrorCode::InvalidData,
                                     u"%1's window cannot open here: plugin windows need X11 (the app runs without it, "
                                     u"on Wayland). Start the app with QT_QPA_PLATFORM=xcb"_s.arg(m_impl.name)));
        }
        // Some plugins only know their size once opened; asking first is what hosts do.
        (void)askedSize();
        // VST2 takes every kind of window as a void*: a Windows handle, an NSView, an X11 window id.
        m_impl.call(vst2::op::kEditOpen, 0, 0, reinterpret_cast<void*>(parent.handle)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): VST2 takes the window as a void*
        m_attached = true;
        m_size = askedSize();
        m_impl.resized = [this](int width, int height) { pluginResized(width, height); };
        m_idle.start();
        qCInfo(lcEngine).noquote() << "Editor opened:" << m_impl.name << m_size.width() << "x" << m_size.height();
        return {};
    }

    void detach() override
    {
        if (!m_attached) return;
        m_idle.stop();
        m_impl.resized = nullptr;
        m_impl.call(vst2::op::kEditClose);
        m_attached = false;
    }

    void setFitter(Fitter fitter) override { m_fitter = std::move(fitter); }
    void setContentScale(double /*scale*/) override {} // VST2 has no scaling of its own
    void updateGeometry() override
    {
        if (m_fitter && m_attached && !m_size.isEmpty()) (void)m_fitter(m_size);
    }

private:
    static core::Result<void> logged(core::Result<void> result)
    {
        if (!result) qCWarning(lcEngine).noquote() << result.error().message;
        return result;
    }

    [[nodiscard]] QSize askedSize() const
    {
        vst2::Rect* rect = nullptr;
        m_impl.call(vst2::op::kEditGetRect, 0, 0, static_cast<void*>(&rect));
        if (rect == nullptr) return {};
        return {rect->right - rect->left, rect->bottom - rect->top};
    }

    // The plugin asks for another size (its own size menu): the window follows.
    void pluginResized(int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        qCInfo(lcEngine).noquote() << m_impl.name << "asks for" << width << "x" << height;
        m_size = QSize(width, height);
        if (m_fitter) (void)m_fitter(m_size);
    }

    std::shared_ptr<Vst2Node> m_node; // keeps the plugin alive while its editor exists
    Vst2Node::Impl& m_impl;
    QTimer m_idle;
    Fitter m_fitter;
    QSize m_size;
    bool m_attached = false;
};

} // namespace

core::Result<std::unique_ptr<IPluginEditor>> Vst2Node::makeEditor(const std::shared_ptr<PluginNode>& self)
{
    GC_ONLY_MAIN_THREAD();
    if ((m_impl->effect->flags & vst2::flag::kHasEditor) == 0) {
        qCInfo(lcEngine).noquote() << m_impl->name << "has no editor";
        return std::unique_ptr<IPluginEditor>();
    }
    return std::unique_ptr<IPluginEditor>(std::make_unique<Vst2Editor>(std::static_pointer_cast<Vst2Node>(self), *m_impl));
}

} // namespace gigchain::engine
