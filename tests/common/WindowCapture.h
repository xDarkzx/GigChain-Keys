#pragma once

// A picture of what a window shows, including what a plugin draws in it with
// the graphics card (PrintWindow only gets such a window's first frame):
// Windows.Graphics.Capture, as screen recorders use. The window must be shown
// (it may be off every screen) and not minimised. Windows only; tests only.
#ifdef Q_OS_WIN

#include <QImage>
#include <QtTest>

#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace windowcapture {

inline QImage captureWindow(HWND window, int timeoutMs = 2000)
{
    namespace capture = winrt::Windows::Graphics::Capture;
    namespace d3d = winrt::Windows::Graphics::DirectX::Direct3D11;

    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put()))) {
        return {};
    }
    const auto dxgi = device.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectable;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()))) return {};
    const auto direct3d = inspectable.as<d3d::IDirect3DDevice>();

    const auto interop = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    capture::GraphicsCaptureItem item{nullptr};
    if (const HRESULT made = interop->CreateForWindow(window, winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item));
        FAILED(made) || !item) {
        qWarning("captureWindow: no capture item for the window (0x%08lx)", static_cast<unsigned long>(made));
        return {};
    }

    const auto pool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        direct3d, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
    const auto session = pool.CreateCaptureSession(item);
    try {
        session.IsBorderRequired(false); // (no yellow border on the user's screen, where Windows allows it)
    } catch (...) {
    }
    session.IsCursorCaptureEnabled(false);
    session.StartCapture();

    capture::Direct3D11CaptureFrame frame{nullptr};
    // (The second frame: the first may be the one drawn before capturing started.)
    for (int got = 0; got < 2;) {
        if (!QTest::qWaitFor([&] { return static_cast<bool>(frame = pool.TryGetNextFrame()); }, timeoutMs)) break;
        ++got;
    }
    session.Close();
    if (!frame) {
        qWarning("captureWindow: no frame came in %d ms (item %dx%d)", timeoutMs, item.Size().Width, item.Size().Height);
        return {};
    }

    const auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    if (FAILED(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()))) return {};
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    winrt::com_ptr<ID3D11Texture2D> readable;
    if (FAILED(device->CreateTexture2D(&description, nullptr, readable.put()))) return {};
    context->CopyResource(readable.get(), texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(readable.get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
    QImage image(static_cast<int>(description.Width), static_cast<int>(description.Height), QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = static_cast<const uchar*>(mapped.pData) + (static_cast<std::size_t>(y) * mapped.RowPitch); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): the texture's rows
        std::memcpy(image.scanLine(y), row, static_cast<std::size_t>(image.width()) * 4);
    }
    context->Unmap(readable.get(), 0);
    return image;
}

} // namespace windowcapture

#endif
