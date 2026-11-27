// One frame of UI: Yoga computes the layout, ThorVG rasterizes it into a
// CPU buffer, and that buffer is uploaded to a D3D11 texture and presented.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <yoga/Yoga.h>
#include <thorvg.h>

#include <cstdint>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11Texture2D> uploadTex;
};

bool InitGpu(HWND hwnd, Gpu& gpu) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = kWidth;
    sd.BufferDesc.Height = kHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
#ifdef _DEBUG
        D3D11_CREATE_DEVICE_DEBUG,
#else
        0,
#endif
        &level, 1, D3D11_SDK_VERSION, &sd,
        &gpu.swapChain, &gpu.device, nullptr, &gpu.context);
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = kWidth;
    td.Height = kHeight;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    hr = gpu.device->CreateTexture2D(&td, nullptr, &gpu.uploadTex);
    return SUCCEEDED(hr);
}

// Lays out a root with one flex-grow child using Yoga.
YGNodeRef BuildLayout(YGNodeRef& child) {
    YGNodeRef root = YGNodeNew();
    YGNodeStyleSetWidth(root, kWidth);
    YGNodeStyleSetHeight(root, kHeight);
    YGNodeStyleSetPadding(root, YGEdgeAll, 40);

    child = YGNodeNew();
    YGNodeStyleSetFlexGrow(child, 1);
    YGNodeInsertChild(root, child, 0);

    YGNodeCalculateLayout(root, YGUndefined, YGUndefined, YGDirectionLTR);
    return root;
}

// Rasterizes a filled rectangle with ThorVG into a BGRA buffer.
void RenderFrame(std::vector<uint32_t>& pixels, float x, float y, float w, float h) {
    tvg::SwCanvas* canvas = tvg::SwCanvas::gen();
    canvas->target(pixels.data(), kWidth, kWidth, kHeight, tvg::ColorSpace::ARGB8888);

    // Background
    tvg::Shape* bg = tvg::Shape::gen();
    bg->appendRect(0, 0, kWidth, kHeight, 0, 0);
    bg->fill(24, 26, 33, 255);
    canvas->add(bg);

    // Yoga-positioned panel
    tvg::Shape* panel = tvg::Shape::gen();
    panel->appendRect(x, y, w, h, 12, 12);
    panel->fill(66, 133, 244, 255);
    canvas->add(panel);

    canvas->draw();
    canvas->sync();
    // Canvas and its paints are released once the frame is rasterized.
    delete canvas;
}

void Present(Gpu& gpu, const std::vector<uint32_t>& pixels) {
    gpu.context->UpdateSubresource(gpu.uploadTex.Get(), 0, nullptr,
                                   pixels.data(), kWidth * 4, 0);

    ComPtr<ID3D11Texture2D> backBuffer;
    gpu.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    gpu.context->CopyResource(backBuffer.Get(), gpu.uploadTex.Get());
    gpu.swapChain->Present(1, 0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"LaleWindow";
    RegisterClassExW(&wc);

    RECT rc{0, 0, kWidth, kHeight};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Lale",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    Gpu gpu;
    if (!InitGpu(hwnd, gpu)) return 1;

    tvg::Initializer::init(0);

    YGNodeRef child = nullptr;
    YGNodeRef root = BuildLayout(child);
    const float x = YGNodeLayoutGetLeft(child);
    const float y = YGNodeLayoutGetTop(child);
    const float w = YGNodeLayoutGetWidth(child);
    const float h = YGNodeLayoutGetHeight(child);

    std::vector<uint32_t> pixels(static_cast<size_t>(kWidth) * kHeight);
    RenderFrame(pixels, x, y, w, h);

    ShowWindow(hwnd, nCmdShow);

    MSG msg{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (running) Present(gpu, pixels);
    }

    YGNodeFreeRecursive(root);
    tvg::Initializer::term();
    return static_cast<int>(msg.wParam);
}
