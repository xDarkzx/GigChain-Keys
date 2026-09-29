#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/platform/Windows.h"

#include <QSize>
#include <QString>
#include <QtGlobal>

#include <functional>

namespace gigchain::engine {

// A native window a plugin editor is embedded in.
struct NativeParent
{
    quintptr handle = 0;
    platform::NativeWindowKind kind = platform::NativeWindowKind::Win32;
};

// A plugin's own editor window, embedded in a native window the UI provides.
// Main thread only, and that thread must have OLE initialised (every Qt GUI
// app does; some plugins crash in attach() otherwise). Sizes are in physical pixels (what plugins use on
// Windows); the UI converts with the screen's device pixel ratio.
//
// The editor keeps its plugin alive; detach() (or destruction) must happen
// before the native parent window is destroyed.
class IPluginEditor
{
public:
    virtual ~IPluginEditor() = default;
    IPluginEditor(const IPluginEditor&) = delete;
    IPluginEditor& operator=(const IPluginEditor&) = delete;
    IPluginEditor(IPluginEditor&&) = delete;
    IPluginEditor& operator=(IPluginEditor&&) = delete;

    [[nodiscard]] virtual QString title() const = 0;
    [[nodiscard]] virtual QSize preferredSize() const = 0;
    [[nodiscard]] virtual bool isAttached() const = 0;

    // The native window the editor goes into: its handle and which kind it is
    // (platform::nativeWindowKind() for a Qt window's winId()). Failures are
    // returned with the precise cause and logged.
    virtual core::Result<void> attach(NativeParent parent) = 0;
    virtual void detach() = 0;

    // Sizing, as Audacity 4's VstView (muse/framework/vst/qml/Muse/Vst/vstview.cpp).
    // The fitter is the window showing the plugin: given the size the plugin
    // wants (its own size, physical pixels), it sizes the window as big as
    // its room allows with the same shape, never bigger than the plugin's
    // own size, and returns that size; the plugin is then told it (onSize).
    // Empty when there is no room yet: the plugin is fitted when there is.
    using Fitter = std::function<QSize(QSize wanted)>;
    virtual void setFitter(Fitter fitter) = 0;
    // The screen's scaling, given before sizing (setContentScaleFactor).
    virtual void setContentScale(double scale) = 0;
    // The plugin's own size, through the fitter (VstView::updateViewGeometry):
    // after attach, when the room changes, on another screen.
    virtual void updateGeometry() = 0;

protected:
    IPluginEditor() = default;
};

} // namespace gigchain::engine
