#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include "gfx/Presenter.h"

namespace lale {

class D3D11Presenter : public Presenter {
 public:
  bool Initialize(const Window& window, int width, int height) override;
  bool Resize(int width, int height) override;
  void Present(const uint32_t* bgra_pixels, int width, int height) override;

 private:
  bool CreateUploadTexture(int width, int height);

  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> upload_texture_;
  int buffer_width_ = 0;
  int buffer_height_ = 0;
};

}
