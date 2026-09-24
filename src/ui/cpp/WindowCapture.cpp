#include "WindowCapture.h"

#include <QLoggingCategory>
#include <QWindow>

#include <windows.h>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace openstage::ui {
namespace {

// PW_RENDERFULLCONTENT (Windows 8.1+): capture DirectX/OpenGL content too.
constexpr UINT kRenderFullContent = 0x00000002;

// RAII for the GDI objects of one capture.
struct GdiCapture
{
    HDC screen = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;

    GdiCapture(const GdiCapture&) = delete;
    GdiCapture& operator=(const GdiCapture&) = delete;
    GdiCapture(GdiCapture&&) = delete;
    GdiCapture& operator=(GdiCapture&&) = delete;

    GdiCapture(int width, int height)
        : screen(GetDC(nullptr)),
          memory(CreateCompatibleDC(screen)),
          bitmap(CreateCompatibleBitmap(screen, width, height))
    {
        if (memory != nullptr && bitmap != nullptr) previous = SelectObject(memory, bitmap);
    }
    ~GdiCapture()
    {
        if (previous != nullptr) SelectObject(memory, previous);
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (memory != nullptr) DeleteDC(memory);
        if (screen != nullptr) ReleaseDC(nullptr, screen);
    }
    [[nodiscard]] bool ok() const { return screen != nullptr && memory != nullptr && bitmap != nullptr; }
};

} // namespace

QImage captureWindow(QWindow* topLevel, QRect region)
{
    if (topLevel == nullptr) return {};
    const auto hwnd = reinterpret_cast<HWND>(topLevel->winId());
    RECT bounds{};
    if (!GetWindowRect(hwnd, &bounds)) {
        qCWarning(lcUi) << "Window capture: no window bounds, error" << GetLastError();
        return {};
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return {};

    GdiCapture gdi(width, height);
    if (!gdi.ok()) {
        qCWarning(lcUi) << "Window capture: could not create a bitmap of" << width << "x" << height;
        return {};
    }
    if (!PrintWindow(hwnd, gdi.memory, kRenderFullContent)) {
        qCWarning(lcUi) << "Window capture: PrintWindow failed, error" << GetLastError();
        return {};
    }
    SelectObject(gdi.memory, gdi.previous); // the bitmap must not be selected while converted
    gdi.previous = nullptr;
    QImage image = QImage::fromHBITMAP(gdi.bitmap);
    if (image.isNull()) {
        qCWarning(lcUi) << "Window capture: could not convert the bitmap";
        return {};
    }
    // GetWindowRect includes the frame; the client area starts at the offset
    // between the frame and the client origin.
    POINT clientOrigin{0, 0};
    ClientToScreen(hwnd, &clientOrigin);
    const QPoint clientOffset(clientOrigin.x - bounds.left, clientOrigin.y - bounds.top);
    if (!region.isEmpty()) return image.copy(region.translated(clientOffset));
    return image;
}

} // namespace openstage::ui
