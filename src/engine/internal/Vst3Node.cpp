#include "Vst3Node.h"

#include "EngineLog.h"
#include "LoaderErrors.h"

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
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <bitset>
#include <exception>
#include <string>

using namespace Qt::StringLiterals;
using namespace Steinberg;

namespace openstage::engine {
namespace {

// One host context for the whole process, installed before the first plugin
// loads (the SDK's PlugProvider reads it from PluginContextFactory).
Vst::HostApplication& hostContext()
{
    static Vst::HostApplication host;
    static const bool installed = [] {
        Vst::PluginContextFactory::instance().setPluginContext(&host);
        return true;
    }();
    (void)installed;
    return host;
}


// The host side of IComponentHandler: plugins report parameter edits and
// ask for restarts through it. Some plugins (e.g. FabFilter Pro-DS) call it
// during setup and crash when a host has not provided one. v1 accepts the
// calls; forwarding edits to automation comes later.
class ComponentHandler final : public Vst::IComponentHandler
{
public:
    tresult PLUGIN_API beginEdit(Vst::ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(Vst::ParamID, Vst::ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(Vst::ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

    tresult PLUGIN_API queryInterface(const TUID requested, void** object) override
    {
        QUERY_INTERFACE(requested, object, FUnknown::iid, Vst::IComponentHandler)
        QUERY_INTERFACE(requested, object, Vst::IComponentHandler::iid, Vst::IComponentHandler)
        *object = nullptr;
        return kNoInterface;
    }
    // Owned by the node (lives as long as the controller that uses it).
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

// kNotImplemented means "nothing to do" for optional calls; anything else
// that is not kResultOk is a real failure.
bool succeeded(tresult result)
{
    return result == kResultOk || result == kNotImplemented;
}

QString directionName(Vst::BusDirection direction)
{
    return direction == Vst::kInput ? u"input"_s : u"output"_s;
}

// Activates the main audio buses in one direction (bus 0 when none is marked
// main). A plugin that refuses is a load error, never ignored.
core::Result<void> activateMainBuses(Vst::IComponent& component, Vst::BusDirection direction, const QString& name)
{
    const int32 count = component.getBusCount(Vst::kAudio, direction);
    bool any = false;
    for (int32 i = 0; i < count; ++i) {
        Vst::BusInfo info{};
        if (component.getBusInfo(Vst::kAudio, direction, i, info) != kResultOk || info.busType != Vst::kMain) continue;
        if (!succeeded(component.activateBus(Vst::kAudio, direction, i, true))) {
            return core::fail(core::ErrorCode::InvalidData,
                              u"%1 refused to activate audio %2 bus %3"_s.arg(name, directionName(direction)).arg(i));
        }
        any = true;
    }
    if (!any && count > 0 && !succeeded(component.activateBus(Vst::kAudio, direction, 0, true))) {
        return core::fail(core::ErrorCode::InvalidData,
                          u"%1 refused to activate audio %2 bus 0"_s.arg(name, directionName(direction)));
    }
    return {};
}

} // namespace

struct Vst3Node::Impl
{
    VST3::Hosting::Module::Ptr module;
    IPtr<Vst::PlugProvider> provider;
    IPtr<Vst::IComponent> component;
    IPtr<Vst::IAudioProcessor> processor;
    IPtr<Vst::IEditController> controller;
    ComponentHandler componentHandler;
    Vst::HostProcessData data;
    Vst::EventList events{kMaxEventsPerBlock};
    Vst::ParameterChanges parameterChanges;
    Vst::ProcessContext context{};
    QString name;
    QString bundlePath;
    bool instrument = false;
    bool active = false;
    int maxBlock = 0;
    // The audio thread cannot log; it counts problems here and the main
    // thread reports them (Vst3Node::takeProblems()).
    std::atomic<uint64_t> processFailures{0};
    std::atomic<uint64_t> droppedEvents{0};
    std::atomic<uint64_t> oversizedBlocks{0};
    // Notes currently held, per MIDI channel (audio thread only), and a
    // main-thread request to release them on the next block.
    std::bitset<16 * 128> heldNotes;
    std::atomic<bool> releaseRequested{false};

    core::Result<void> activate(double sampleRate, int block)
    {
        // Steinberg's host order: activate buses, then setupProcessing, then
        // setActive / setProcessing.
        if (auto r = activateMainBuses(*component, Vst::kInput, name); !r) return r;
        if (auto r = activateMainBuses(*component, Vst::kOutput, name); !r) return r;
        if (component->getBusCount(Vst::kEvent, Vst::kInput) > 0 &&
            !succeeded(component->activateBus(Vst::kEvent, Vst::kInput, 0, true))) {
            return core::fail(core::ErrorCode::InvalidData, u"%1 refused to activate its MIDI input"_s.arg(name));
        }
        Vst::ProcessSetup setup{Vst::kRealtime, Vst::kSample32, block, sampleRate};
        if (processor->setupProcessing(setup) != kResultOk) {
            return core::fail(core::ErrorCode::InvalidData,
                              u"%1 does not support %2 Hz / %3-sample blocks"_s.arg(name).arg(sampleRate).arg(block));
        }

        data.prepare(*component, block, Vst::kSample32);
        context.sampleRate = sampleRate;
        context.tempo = 120.0;
        context.state = Vst::ProcessContext::kTempoValid;
        data.inputEvents = &events;
        data.inputParameterChanges = &parameterChanges;
        data.processContext = &context;

        if (!succeeded(component->setActive(true))) {
            data.unprepare();
            return core::fail(core::ErrorCode::InvalidData, u"%1 failed to activate"_s.arg(name));
        }
        if (!succeeded(processor->setProcessing(true))) {
            if (!succeeded(component->setActive(false))) {
                qCWarning(lcEngine).noquote() << name << "also failed to deactivate after a failed start";
            }
            data.unprepare();
            return core::fail(core::ErrorCode::InvalidData, u"%1 failed to start processing"_s.arg(name));
        }
        active = true;
        maxBlock = block;
        return {};
    }

    // Main thread. Failures here cannot be undone, but they are logged.
    void deactivate()
    {
        if (!active) return;
        if (!succeeded(processor->setProcessing(false))) {
            qCWarning(lcEngine).noquote() << name << "reported an error when stopping processing";
        }
        if (!succeeded(component->setActive(false))) {
            qCWarning(lcEngine).noquote() << name << "reported an error when deactivating";
        }
        data.unprepare();
        active = false;
    }
};

core::Result<std::shared_ptr<Vst3Node>> Vst3Node::loadUnlogged(const QString& bundlePath, double sampleRate, int maxBlock)
{
    if (!QFileInfo::exists(bundlePath)) {
        return core::fail(core::ErrorCode::FileNotFound, u"Plugin not found: %1"_s.arg(bundlePath));
    }
    if (!bundlePath.endsWith(u".vst3"_s, Qt::CaseInsensitive)) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 is not a .vst3 plugin"_s.arg(bundlePath));
    }
    try {
        const SilentLoaderErrors silent;
        hostContext();
        auto impl = std::make_unique<Impl>();

        std::string error;
        impl->module = VST3::Hosting::Module::create(QFileInfo(bundlePath).absoluteFilePath().toStdString(), error);
        if (!impl->module) {
            return core::fail(core::ErrorCode::InvalidData,
                              u"Not a loadable VST3 plugin: %1 (%2)"_s.arg(bundlePath, QString::fromStdString(error)));
        }

        const auto factory = impl->module->getFactory();
        for (const auto& info : factory.classInfos()) {
            if (info.category() != kVstAudioEffectClass) continue;
            impl->name = QString::fromStdString(info.name());
            impl->bundlePath = bundlePath;
            impl->instrument = QString::fromStdString(info.subCategoriesString()).contains(u"Instrument"_s);
            impl->provider = owned(new Vst::PlugProvider(factory, info, true));
            break;
        }
        if (!impl->provider) {
            return core::fail(core::ErrorCode::InvalidData, u"%1 contains no audio processor"_s.arg(bundlePath));
        }
        if (!impl->provider->initialize()) {
            return core::fail(core::ErrorCode::InvalidData, u"%1 failed to initialise"_s.arg(impl->name));
        }

        impl->component = impl->provider->getComponentPtr();
        impl->controller = impl->provider->getControllerPtr();
        impl->processor = FUnknownPtr<Vst::IAudioProcessor>(impl->component);
        if (!impl->component || !impl->processor) {
            return core::fail(core::ErrorCode::InvalidData, u"%1 has no audio processor interface"_s.arg(impl->name));
        }

        // Some plugins only finish initialising once the controller has seen
        // the component's state (Muse notes crashes in editors without this).
        if (impl->controller) {
            if (impl->controller->setComponentHandler(&impl->componentHandler) != kResultOk) {
                qCInfo(lcEngine).noquote() << impl->name << "did not accept a component handler";
            }
            MemoryStream state;
            if (impl->component->getState(&state) == kResultOk && state.getSize() > 0) {
                state.seek(0, IBStream::kIBSeekSet, nullptr);
                if (!succeeded(impl->controller->setComponentState(&state))) {
                    // Not fatal (the plugin still plays), but recorded.
                    qCWarning(lcEngine).noquote() << impl->name << "rejected its own default state";
                }
            }
        }

        if (auto activated = impl->activate(sampleRate, maxBlock); !activated) {
            return tl::unexpected(activated.error());
        }
        return std::make_shared<Vst3Node>(Token{}, std::move(impl));
    } catch (const std::exception& e) {
        return core::fail(core::ErrorCode::InvalidData,
                          u"Loading %1 failed: %2"_s.arg(bundlePath, QString::fromUtf8(e.what())));
    } catch (...) {
        return core::fail(core::ErrorCode::InvalidData, u"Loading %1 failed"_s.arg(bundlePath));
    }
}

core::Result<std::shared_ptr<Vst3Node>> Vst3Node::load(const QString& bundlePath, double sampleRate, int maxBlock)
{
    auto node = loadUnlogged(bundlePath, sampleRate, maxBlock);
    if (node) {
        qCInfo(lcEngine).noquote() << "Loaded plugin" << (*node)->name() << "from" << bundlePath;
    } else {
        qCWarning(lcEngine).noquote() << node.error().message;
    }
    return node;
}

Vst3Node::Vst3Node(Token, std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

Vst3Node::~Vst3Node()
{
    // Reverse order of creation; provider termination releases component and
    // controller, and the module is unloaded last.
    m_impl->deactivate();
    m_impl->processor = nullptr;
    m_impl->controller = nullptr;
    m_impl->component = nullptr;
    m_impl->provider = nullptr;
    m_impl->module = nullptr;
}

core::Result<void> Vst3Node::prepare(double sampleRate, int maxBlock)
{
    m_impl->deactivate();
    auto activated = m_impl->activate(sampleRate, maxBlock);
    if (!activated) {
        // The node stays inactive and process() outputs silence.
        qCWarning(lcEngine).noquote() << activated.error().message;
    }
    return activated;
}

void Vst3Node::process(std::span<const MidiEvent> events, AudioBlock io)
{
    Impl& impl = *m_impl;
    const auto frames = static_cast<std::size_t>(std::max(io.frames, 0));
    if (!impl.active || io.frames <= 0 || io.frames > impl.maxBlock) {
        if (io.frames > impl.maxBlock) impl.oversizedBlocks.fetch_add(1, std::memory_order_relaxed);
        std::fill_n(io.left, frames, 0.0F);
        std::fill_n(io.right, frames, 0.0F);
        return;
    }

    impl.events.clear();
    if (impl.releaseRequested.exchange(false, std::memory_order_acquire) && impl.heldNotes.any()) {
        for (std::size_t i = 0; i < impl.heldNotes.size(); ++i) {
            if (!impl.heldNotes.test(i)) continue;
            Vst::Event off{};
            off.type = Vst::Event::kNoteOffEvent;
            off.noteOff.channel = static_cast<int16>(i / 128);
            off.noteOff.pitch = static_cast<int16>(i % 128);
            off.noteOff.noteId = -1;
            if (impl.events.addEvent(off) != kResultOk) impl.droppedEvents.fetch_add(1, std::memory_order_relaxed);
        }
        impl.heldNotes.reset();
    }
    for (const MidiEvent& e : events) {
        const int type = e.status & 0xF0;
        Vst::Event event{};
        event.busIndex = 0;
        event.sampleOffset = e.sampleOffset;
        if (type == 0x90 && e.data2 > 0) {
            event.type = Vst::Event::kNoteOnEvent;
            event.noteOn.channel = static_cast<int16>(e.status & 0x0F);
            event.noteOn.pitch = e.data1;
            event.noteOn.velocity = static_cast<float>(e.data2) / 127.0F;
            event.noteOn.noteId = -1;
            impl.heldNotes.set(static_cast<std::size_t>((e.status & 0x0F) * 128 + e.data1));
        } else if (type == 0x80 || type == 0x90) {
            impl.heldNotes.reset(static_cast<std::size_t>((e.status & 0x0F) * 128 + e.data1));
            event.type = Vst::Event::kNoteOffEvent;
            event.noteOff.channel = static_cast<int16>(e.status & 0x0F);
            event.noteOff.pitch = e.data1;
            event.noteOff.velocity = static_cast<float>(e.data2) / 127.0F;
            event.noteOff.noteId = -1;
        } else if (type == 0xA0) {
            event.type = Vst::Event::kPolyPressureEvent;
            event.polyPressure.channel = static_cast<int16>(e.status & 0x0F);
            event.polyPressure.pitch = e.data1;
            event.polyPressure.pressure = static_cast<float>(e.data2) / 127.0F;
            event.polyPressure.noteId = -1;
        } else {
            continue; // controllers need IMidiMapping (not in v1)
        }
        if (impl.events.addEvent(event) != kResultOk) impl.droppedEvents.fetch_add(1, std::memory_order_relaxed);
    }

    // Effects read their input from the main input bus.
    if (impl.data.numInputs > 0 && impl.data.inputs[0].numChannels > 0) {
        Vst::AudioBusBuffers& in = impl.data.inputs[0];
        std::copy_n(io.left, frames, in.channelBuffers32[0]);
        if (in.numChannels > 1) std::copy_n(io.right, frames, in.channelBuffers32[1]);
        in.silenceFlags = 0;
    }

    impl.data.numSamples = static_cast<int32>(io.frames);
    if (impl.processor->process(impl.data) != kResultOk) {
        impl.processFailures.fetch_add(1, std::memory_order_relaxed);
    }

    if (impl.data.numOutputs > 0 && impl.data.outputs[0].numChannels > 0) {
        const Vst::AudioBusBuffers& out = impl.data.outputs[0];
        std::copy_n(out.channelBuffers32[0], frames, io.left);
        std::copy_n(out.channelBuffers32[out.numChannels > 1 ? 1 : 0], frames, io.right);
    } else {
        std::fill_n(io.left, frames, 0.0F);
        std::fill_n(io.right, frames, 0.0F);
    }
}

void Vst3Node::releaseAllNotes()
{
    m_impl->releaseRequested.store(true, std::memory_order_release);
}

Vst3Node::Problems Vst3Node::takeProblems()
{
    return Problems{m_impl->processFailures.exchange(0), m_impl->droppedEvents.exchange(0),
                    m_impl->oversizedBlocks.exchange(0)};
}

namespace {

QByteArray streamBytes(MemoryStream& stream)
{
    return QByteArray(stream.getData(), static_cast<qsizetype>(stream.getSize()));
}

} // namespace

core::Result<Vst3Node::State> Vst3Node::saveState() const
{
    State state;
    MemoryStream component;
    if (m_impl->component->getState(&component) != kResultOk) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 did not give its state"_s.arg(m_impl->name));
    }
    state.component = streamBytes(component);
    if (m_impl->controller) {
        MemoryStream controller;
        // Controllers without their own state say so; that is not a failure.
        if (m_impl->controller->getState(&controller) == kResultOk) state.controller = streamBytes(controller);
    }
    return state;
}

core::Result<void> Vst3Node::restoreState(const State& state)
{
    if (state.component.isEmpty()) return {};
    MemoryStream component(const_cast<char*>(state.component.constData()), state.component.size());
    if (m_impl->component->setState(&component) != kResultOk) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 rejected its saved state"_s.arg(m_impl->name));
    }
    if (m_impl->controller) {
        component.seek(0, IBStream::kIBSeekSet, nullptr);
        if (!succeeded(m_impl->controller->setComponentState(&component))) {
            return core::fail(core::ErrorCode::InvalidData, u"%1's editor rejected its saved state"_s.arg(m_impl->name));
        }
        if (!state.controller.isEmpty()) {
            MemoryStream controller(const_cast<char*>(state.controller.constData()), state.controller.size());
            if (!succeeded(m_impl->controller->setState(&controller))) {
                return core::fail(core::ErrorCode::InvalidData,
                                  u"%1's editor rejected its saved settings"_s.arg(m_impl->name));
            }
        }
    }
    return {};
}

QString Vst3Node::bundlePath() const
{
    return m_impl->bundlePath;
}

QString Vst3Node::name() const
{
    return m_impl->name;
}

bool Vst3Node::isInstrument() const
{
    return m_impl->instrument;
}

// ---------------------------------------------------------------- editor

namespace {

// Hosts one IPlugView. Implements IPlugFrame so the plugin can ask for a
// resize. Not reference-counted by the plugin: this object is owned by the UI
// (unique_ptr) and clears the frame pointer before it goes away.
class Vst3Editor final : public IPluginEditor, public IPlugFrame
{
public:
    Vst3Editor(std::shared_ptr<Vst3Node> node, IPtr<IPlugView> view, QString title)
        : m_node(std::move(node)), m_view(std::move(view)), m_title(std::move(title))
    {
    }

    ~Vst3Editor() override { detach(); }

    Vst3Editor(const Vst3Editor&) = delete;
    Vst3Editor& operator=(const Vst3Editor&) = delete;
    Vst3Editor(Vst3Editor&&) = delete;
    Vst3Editor& operator=(Vst3Editor&&) = delete;

    [[nodiscard]] QString title() const override { return m_title; }

    [[nodiscard]] QSize preferredSize() const override
    {
        ViewRect rect{};
        if (m_view->getSize(&rect) != kResultOk) return {};
        return {rect.getWidth(), rect.getHeight()};
    }

    [[nodiscard]] bool canResize() const override { return m_view->canResize() == kResultTrue; }
    [[nodiscard]] bool isAttached() const override { return m_attached; }

    core::Result<void> attach(quintptr nativeParent) override
    {
        if (m_attached) return {};
        if (nativeParent == 0) {
            return logged(core::fail(core::ErrorCode::InvalidData, u"%1: no window to attach the editor to"_s.arg(m_title)));
        }
        if (m_view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
            return logged(core::fail(core::ErrorCode::InvalidData, u"%1's editor does not support Windows windows"_s.arg(m_title)));
        }
        m_view->setFrame(this);
        if (m_view->attached(reinterpret_cast<void*>(nativeParent), kPlatformTypeHWND) != kResultOk) {
            m_view->setFrame(nullptr);
            return logged(core::fail(core::ErrorCode::InvalidData, u"%1's editor failed to open"_s.arg(m_title)));
        }
        m_attached = true;
        qCInfo(lcEngine).noquote() << "Editor opened:" << m_title;
        return {};
    }

    void detach() override
    {
        if (m_attached) {
            if (m_view->removed() != kResultOk) {
                qCWarning(lcEngine).noquote() << m_title << "reported an error when closing its editor";
            }
            m_attached = false;
        }
        m_view->setFrame(nullptr);
    }

    QSize setSize(QSize size) override
    {
        ViewRect rect{0, 0, size.width(), size.height()};
        if (canResize() && m_view->checkSizeConstraint(&rect) != kResultTrue) {
            qCInfo(lcEngine).noquote() << m_title << "adjusted the requested editor size";
        }
        if (m_view->onSize(&rect) != kResultOk) {
            qCWarning(lcEngine).noquote() << m_title << "rejected editor size" << size;
        }
        return {rect.getWidth(), rect.getHeight()};
    }

    bool setContentScale(double scale) override
    {
        FUnknownPtr<IPlugViewContentScaleSupport> scaling(m_view);
        if (!scaling || scaling->setContentScaleFactor(static_cast<float>(scale)) != kResultTrue) {
            if (!m_scaleWarned) {
                qCInfo(lcEngine).noquote() << m_title << "cannot scale its editor; it keeps its own size";
                m_scaleWarned = true;
            }
            return false;
        }
        return true;
    }

    void setResizeHandler(std::function<void(QSize)> handler) override { m_onResize = std::move(handler); }

    // IPlugFrame: the plugin asks the host for a new size.
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override
    {
        if (view == nullptr || newSize == nullptr) return kInvalidArgument;
        if (m_inResize) return kResultTrue; // the plugin re-entered while we resize
        m_inResize = true;
        if (m_onResize) m_onResize(QSize(newSize->getWidth(), newSize->getHeight()));
        const tresult result = view->onSize(newSize);
        m_inResize = false;
        return result == kResultOk ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API queryInterface(const TUID requested, void** object) override
    {
        QUERY_INTERFACE(requested, object, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(requested, object, IPlugFrame::iid, IPlugFrame)
        *object = nullptr;
        return kNoInterface;
    }
    // Lifetime is managed by the owner, not by reference counting.
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    static core::Result<void> logged(core::Result<void> result)
    {
        if (!result) qCWarning(lcEngine).noquote() << result.error().message;
        return result;
    }

    std::shared_ptr<Vst3Node> m_node; // keeps the plugin alive while its editor exists
    IPtr<IPlugView> m_view;
    QString m_title;
    std::function<void(QSize)> m_onResize;
    bool m_attached = false;
    bool m_inResize = false;
    bool m_scaleWarned = false;
};

} // namespace

core::Result<std::unique_ptr<IPluginEditor>> Vst3Node::createEditor(const std::shared_ptr<Vst3Node>& node)
{
    if (!node) return core::fail(core::ErrorCode::InvalidData, u"No plugin to open an editor for"_s);
    Impl& impl = *node->m_impl;
    if (!impl.controller) {
        qCInfo(lcEngine).noquote() << impl.name << "has no editor";
        return std::unique_ptr<IPluginEditor>();
    }
    IPtr<IPlugView> view = owned(impl.controller->createView(Vst::ViewType::kEditor));
    if (!view) {
        qCInfo(lcEngine).noquote() << impl.name << "has no editor";
        return std::unique_ptr<IPluginEditor>();
    }
    return std::unique_ptr<IPluginEditor>(std::make_unique<Vst3Editor>(node, std::move(view), impl.name));
}

} // namespace openstage::engine
