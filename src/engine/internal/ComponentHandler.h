#pragma once

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>

namespace gigchain::engine {

// The host side of IComponentHandler(2): plugins report parameter edits and
// ask for restarts through it. Some plugins (e.g. FabFilter Pro-DS) call it
// during setup and crash when a host has not provided one.
//
// Settings changed in the plugin (a knob turned, a preset chosen in its own
// browser, the plugin marking itself dirty) are noted, so the setlist can
// show it has unsaved changes. This is how DAWs notice: comparing saved
// states does not work, because many plugins (Pro-Q 3, most Arturia
// instruments) give back different bytes right after being restored.
class ComponentHandler final : public Steinberg::Vst::IComponentHandler, public Steinberg::Vst::IComponentHandler2
{
public:
    // True once after the plugin reported a change of its settings.
    bool takeEdited() { return m_edited.exchange(false, std::memory_order_acq_rel); }
    // The last parameter moved in the plugin's window since the previous
    // call (for learning a knob), or kNoParamId.
    Steinberg::Vst::ParamID takeTouched()
    {
        const uint64_t touched = m_touched.exchange(0, std::memory_order_acq_rel);
        return touched == 0 ? Steinberg::Vst::kNoParamId : static_cast<Steinberg::Vst::ParamID>(touched & 0xFFFFFFFFU);
    }
    // Audio thread: an edit made in the plugin's window for the processor
    // (VST3: the host carries the controller's edits to the processor), one
    // per slot; false when the slot is empty.
    bool takeEditForProcessor(std::size_t slot, Steinberg::Vst::ParamID& id, Steinberg::Vst::ParamValue& value)
    {
        const uint64_t packed = m_toProcessor.at(slot).exchange(0, std::memory_order_acquire);
        if (packed == 0) return false;
        id = static_cast<Steinberg::Vst::ParamID>(packed >> 32U);
        value = static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(packed & 0xFFFFFFFFU)));
        return true;
    }
    static constexpr std::size_t kEditSlots = 64;

    // IComponentHandler
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) override
    {
        markEdited();
        m_touched.store((uint64_t{1} << 32U) | id, std::memory_order_release);
        // Id and value (as a float) in one atomic, per slot; a later edit of
        // a parameter replaces an earlier one not yet taken. Never 0: the
        // value's bits are at least the float 0's.
        const uint64_t packed = (uint64_t{id} << 32U) | std::bit_cast<uint32_t>(static_cast<float>(value));
        m_toProcessor.at(id % kEditSlots).store(packed == 0 ? 1 : packed, std::memory_order_release);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
    {
        // A preset loaded inside the plugin arrives as new parameter values
        // or a full reload; latency or bus changes are not settings.
        if ((flags & (Steinberg::Vst::kParamValuesChanged | Steinberg::Vst::kReloadComponent)) != 0) markEdited();
        return Steinberg::kResultOk;
    }

    // IComponentHandler2
    Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool state) override
    {
        if (state) markEdited();
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString) override
    {
        return Steinberg::kNotImplemented; // editors open from the Instrument tab
    }
    Steinberg::tresult PLUGIN_API startGroupEdit() override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API finishGroupEdit() override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID requested, void** object) override
    {
        using namespace Steinberg;
        if (FUnknownPrivate::iidEqual(requested, FUnknown::iid)
            || FUnknownPrivate::iidEqual(requested, Vst::IComponentHandler::iid)) {
            *object = static_cast<Vst::IComponentHandler*>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(requested, Vst::IComponentHandler2::iid)) {
            *object = static_cast<Vst::IComponentHandler2*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    // Owned by the node (lives as long as the controller that uses it).
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

private:
    void markEdited() { m_edited.store(true, std::memory_order_release); }

    std::atomic<bool> m_edited{false};
    std::atomic<uint64_t> m_touched{0}; // 1 << 32 | id; 0 = none
    std::array<std::atomic<uint64_t>, kEditSlots> m_toProcessor{};
};

} // namespace gigchain::engine
