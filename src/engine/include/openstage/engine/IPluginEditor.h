#pragma once

#include "openstage/core/Error.h"

#include <QSize>
#include <QString>
#include <QtGlobal>

#include <functional>

namespace openstage::engine {

// A plugin's own editor window, embedded in a native window the UI provides.
// Main thread only. Sizes are in physical pixels (what plugins use on
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
    [[nodiscard]] virtual bool canResize() const = 0;
    [[nodiscard]] virtual bool isAttached() const = 0;

    // `nativeParent` is a window handle (HWND on Windows). Failures are
    // returned with the precise cause and logged.
    virtual core::Result<void> attach(quintptr nativeParent) = 0;
    virtual void detach() = 0;

    // Asks a resizable editor to take this size; returns the size it accepted.
    virtual QSize setSize(QSize size) = 0;
    // Screen scaling (1.0 = 100 %), for plugins that support it.
    virtual void setContentScale(double scale) = 0;
    // Called when the plugin itself asks for a new size (e.g. it opens a panel).
    virtual void setResizeHandler(std::function<void(QSize)> handler) = 0;

protected:
    IPluginEditor() = default;
};

} // namespace openstage::engine
