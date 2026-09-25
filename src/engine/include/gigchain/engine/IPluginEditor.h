#pragma once

#include "gigchain/core/Error.h"

#include <QSize>
#include <QString>
#include <QtGlobal>


namespace gigchain::engine {

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

    // `nativeParent` is a window handle (HWND on Windows). Failures are
    // returned with the precise cause and logged.
    virtual core::Result<void> attach(quintptr nativeParent) = 0;
    virtual void detach() = 0;

protected:
    IPluginEditor() = default;
};

} // namespace gigchain::engine
