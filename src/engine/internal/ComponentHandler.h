#pragma once

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <atomic>

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

    // IComponentHandler
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) override
    {
        markEdited();
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
};

} // namespace gigchain::engine
