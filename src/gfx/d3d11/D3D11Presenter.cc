#include "gfx/d3d11/D3D11Presenter.h"

namespace lale {

std::unique_ptr<Presenter> CreatePlatformPresenter() {
  return std::make_unique<D3D11Presenter>();
}

bool D3D11Presenter::Initialize(const Window& window, int width, int height) {
  HWND hwnd = static_cast<HWND>(window.native_handle());
  DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
  swap_chain_desc.BufferCount = 2;
  swap_chain_desc.BufferDesc.Width = static_cast<UINT>(width);
  swap_chain_desc.BufferDesc.Height = static_cast<UINT>(height);
  swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.OutputWindow = hwnd;
  swap_chain_desc.SampleDesc.Count = 1;
  swap_chain_desc.Windowed = TRUE;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  UINT device_flags = 0;
#ifdef _DEBUG
  device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
  HRESULT result = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_flags, &feature_level,
      1, D3D11_SDK_VERSION, &swap_chain_desc, &swap_chain_, &device_, nullptr,
      &device_context_);
  if (FAILED(result)) return false;

  buffer_width_ = width;
  buffer_height_ = height;
  return CreateUploadTexture(width, height);
}

bool D3D11Presenter::CreateUploadTexture(int width, int height) {
  upload_texture_.Reset();
  D3D11_TEXTURE2D_DESC texture_desc{};
  texture_desc.Width = static_cast<UINT>(width);
  texture_desc.Height = static_cast<UINT>(height);
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  return SUCCEEDED(
      device_->CreateTexture2D(&texture_desc, nullptr, &upload_texture_));
}

bool D3D11Presenter::Resize(int width, int height) {
  if (width == buffer_width_ && height == buffer_height_) return true;
  device_context_->ClearState();
  if (FAILED(swap_chain_->ResizeBuffers(0, static_cast<UINT>(width),
                                        static_cast<UINT>(height),
                                        DXGI_FORMAT_UNKNOWN, 0))) {
    return false;
  }
  buffer_width_ = width;
  buffer_height_ = height;
  return CreateUploadTexture(width, height);
}

void D3D11Presenter::Present(const uint32_t* bgra_pixels, int width,
                             int height) {
  if (width != buffer_width_ || height != buffer_height_ || !upload_texture_) {
    return;
  }
  device_context_->UpdateSubresource(upload_texture_.Get(), 0, nullptr,
                                     bgra_pixels, static_cast<UINT>(width) * 4,
                                     0);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
  if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) return;
  device_context_->CopyResource(back_buffer.Get(), upload_texture_.Get());
  swap_chain_->Present(1, 0);
}

}
