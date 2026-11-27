#include "gfx/sdl/SdlPresenter.h"

#include <memory>

namespace lale {

std::unique_ptr<Presenter> CreatePlatformPresenter() {
  return std::make_unique<SdlPresenter>();
}

SdlPresenter::~SdlPresenter() {
  if (!RendererAlive()) return;
  if (streaming_texture_) SDL_DestroyTexture(streaming_texture_);
  SDL_DestroyRenderer(renderer_);
}

bool SdlPresenter::RendererAlive() const {
  return renderer_ && SDL_GetRenderer(window_) == renderer_;
}

bool SdlPresenter::Initialize(const Window& window, int width, int height) {
  window_ = static_cast<SDL_Window*>(window.native_handle());
  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (!renderer_) return false;
  SDL_SetRenderVSync(renderer_, 1);
  return CreateStreamingTexture(width, height);
}

bool SdlPresenter::CreateStreamingTexture(int width, int height) {
  if (streaming_texture_) SDL_DestroyTexture(streaming_texture_);
  streaming_texture_ =
      SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                        SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!streaming_texture_) return false;
  SDL_SetTextureBlendMode(streaming_texture_, SDL_BLENDMODE_NONE);
  SDL_SetTextureScaleMode(streaming_texture_, SDL_SCALEMODE_NEAREST);
  texture_width_ = width;
  texture_height_ = height;
  return true;
}

bool SdlPresenter::Resize(int width, int height) {
  if (!RendererAlive()) return false;
  if (width == texture_width_ && height == texture_height_) return true;
  return CreateStreamingTexture(width, height);
}

void SdlPresenter::Present(const uint32_t* bgra_pixels, int width, int height) {
  if (!RendererAlive() || !streaming_texture_) return;
  if (width != texture_width_ || height != texture_height_) return;
  SDL_UpdateTexture(streaming_texture_, nullptr, bgra_pixels, width * 4);
  SDL_RenderTexture(renderer_, streaming_texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
}

}
