#pragma once

#include <cstdint>
#include <memory>

#include "platform/Window.h"

namespace lale {

class Presenter {
 public:
  virtual ~Presenter() = default;
  virtual bool Initialize(const Window& window, int width, int height) = 0;
  virtual bool Resize(int width, int height) = 0;
  virtual void Present(const uint32_t* bgra_pixels, int width, int height) = 0;
};

std::unique_ptr<Presenter> CreatePlatformPresenter();

}
