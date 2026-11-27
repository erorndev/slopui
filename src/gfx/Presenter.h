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

#include <cstdint>

namespace lale {

class Presenter {
 public:
  bool Initialize(HWND hwnd, int width, int height);
  bool Resize(int width, int height);
  void Present(const uint32_t* bgra_pixels, int width, int height);

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
