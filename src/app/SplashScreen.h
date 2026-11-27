#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "app/Theme.h"
#include "gfx/Presenter.h"
#include "platform/Window.h"
#include "ui/Font.h"
#include "ui/Node.h"
#include "ui/Renderer.h"

namespace lale {

struct SplashFonts {
  const ui::Font* brand = nullptr;
  const ui::Font* status = nullptr;
};

class SplashScreen : public WindowHandler {
 public:
  bool Open(const theme::Palette& palette, SplashFonts fonts);
  void SetStatus(std::string status_text, float progress);
  void Close();
  bool is_open() const { return is_open_; }

  void OnResize(int client_width, int client_height) override;
  void OnDpiChanged(int dots_per_inch) override;
  void OnPaint() override;
  void OnMouseMove(float, float) override {}
  void OnMouseLeave() override {}
  void OnMouseDown(float, float, bool) override {}
  void OnMouseUp(float, float) override {}
  void OnSystemAppearanceChanged() override {}
  void OnMouseWheel(float, float, float) override {}
  void OnText(const std::string&) override {}
  void OnKey(Key, bool, bool) override {}
  void OnCaretTimer() override {}
  CursorKind CursorAt(float, float) override { return CursorKind::kArrow; }
  WindowRegion RegionAt(float, float) override {
    return WindowRegion::kCaption;
  }
  void OnActivationChanged(bool) override {}
  void OnWake() override {}
  void OnAnimationFrame() override;

 private:
  std::unique_ptr<ui::Node> BuildInterface(float animation_seconds) const;
  void RenderFrame();

  std::unique_ptr<Window> window_ = CreatePlatformWindow();
  std::unique_ptr<Presenter> presenter_ = CreatePlatformPresenter();
  ui::Renderer renderer_;
  theme::Palette palette_;
  SplashFonts fonts_;
  std::string status_text_ = "Startingâ€¦";
  float progress_ = -1;
  float device_pixels_per_point_ = 1;
  std::chrono::steady_clock::time_point opened_at_;
  bool is_open_ = false;
};

}
