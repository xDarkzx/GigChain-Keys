#include "gigchain/platform/Windows.h"

#include "PlatformLog.h"

#include <windows.h>

namespace gigchain::platform {
namespace {

// As Audacity 4's VstView::nativeEventFilter: Windows is told the plugin's
// window (and the plugin's own windows inside it) are already erased, so they
// do not flicker while moved or sized.
class EraseFilter final : public QAbstractNativeEventFilter
{
public:
    explicit EraseFilter(HWND window) : m_window(window) {}

    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override
    {
        if (type != "windows_generic_MSG") return false;
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message != WM_ERASEBKGND || msg->hwnd == nullptr) return false;
        if (msg->hwnd != m_window && IsChild(m_window, msg->hwnd) == FALSE) return false;
        *result = 1; // "already erased"
        return true;
    }

private:
    HWND m_window;
};

} // namespace

NativeWindowKind nativeWindowKind()
{
    return NativeWindowKind::Win32;
}

void prepareGuiPlatform() {}

void bringToFront(QWindow& window)
{
    // Windows refuses a plain "activate" from an app that is not in front (it
    // flashes the taskbar button instead), so the window is first made
    // always-on-top for a moment, which puts it above everything, then set back.
    const auto hwnd = reinterpret_cast<HWND>(window.winId()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): a window id is an HWND on Windows
    constexpr UINT kKeep = SWP_NOMOVE | SWP_NOSIZE;
    if (!SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, kKeep | SWP_SHOWWINDOW) ||
        !SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, kKeep | SWP_SHOWWINDOW)) {
        qCWarning(lcPlatform) << "Could not bring the main window to the front: SetWindowPos failed, error" << GetLastError();
    }
    // Keyboard focus: granted while this app is the one the user started
    // (the splash took the focus at launch); otherwise Windows flashes the
    // taskbar button, which is its rule, not an error.
    if (!SetForegroundWindow(hwnd)) qCInfo(lcPlatform) << "Windows kept keyboard focus where it was";
    window.requestActivate();
}

std::unique_ptr<QAbstractNativeEventFilter> makeNoFlickerFilter(WId pluginWindow)
{
    return std::make_unique<EraseFilter>(reinterpret_cast<HWND>(pluginWindow)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): a window id is an HWND on Windows
}

} // namespace gigchain::platform
