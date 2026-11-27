#pragma once

#include <SDL3/SDL.h>

#include "gfx/Presenter.h"

namespace lale {

class SdlPresenter : public Presenter {
 public:
  ~SdlPresenter() override;

  bool Initialize(const Window& window, int width, int height) override;
  bool Resize(int width, int height) override;
  void Present(const uint32_t* bgra_pixels, int width, int height) override;

 private:
  bool RendererAlive() const;
  bool CreateStreamingTexture(int width, int height);

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* streaming_texture_ = nullptr;
  int texture_width_ = 0;
  int texture_height_ = 0;
};

}
