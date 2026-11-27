#include "app/SplashScreen.h"

#include <algorithm>
#include <utility>

namespace lale {

namespace {

constexpr int kSplashWidth = 360;
constexpr int kSplashHeight = 220;
constexpr int kAnimationIntervalMilliseconds = 16;
constexpr float kBrandMarkSize = 40;
constexpr float kLoaderWidth = 168;
constexpr float kLoaderHeight = 4;
constexpr float kWashSpeed = 70;
constexpr float kShimmerSpeed = 180;
constexpr float kShimmerPeriod = 140;
constexpr float kSplashCornerRadius = 8;

ui::Color WithAlpha(ui::Color color, uint8_t alpha) {
  color.alpha = alpha;
  return color;
}

ui::Node* AddLabel(ui::Node* parent, std::string text, const ui::Font* font,
                   ui::Color color) {
  ui::Node* label = parent->AddChild();
  label->style().text = std::move(text);
  label->style().font = font;
  label->style().text_color = color;
  label->EnableTextMeasurement();
  return label;
}

void AddSpace(ui::Node* parent, float height) {
  ui::Node* space = parent->AddChild();
  YGNodeStyleSetHeight(space->yoga_node(), height);
}

}

bool SplashScreen::Open(const theme::Palette& palette, SplashFonts fonts) {
  palette_ = palette;
  fonts_ = fonts;
  if (!window_->Create("Lale", kSplashWidth, kSplashHeight, this,
                       WindowKind::kSplash)) {
    return false;
  }
  window_->UseDarkTitleBar(palette_.is_dark);
  OnDpiChanged(window_->dots_per_inch());
  if (!presenter_->Initialize(*window_, window_->client_width(),
                              window_->client_height())) {
    window_->Close();
    return false;
  }
  opened_at_ = std::chrono::steady_clock::now();
  is_open_ = true;
  RenderFrame();
  window_->Show();
  window_->StartAnimationTimer(kAnimationIntervalMilliseconds);
  return true;
}

void SplashScreen::SetStatus(std::string status_text, float progress) {
  status_text_ = std::move(status_text);
  progress_ = progress;
}

void SplashScreen::Close() {
  if (!is_open_) return;
  is_open_ = false;
  window_->Close();
}

void SplashScreen::OnResize(int client_width, int client_height) {
  presenter_->Resize(client_width, client_height);
  if (is_open_) RenderFrame();
}

void SplashScreen::OnDpiChanged(int dots_per_inch) {
  device_pixels_per_point_ = static_cast<float>(dots_per_inch) /
                             static_cast<float>(kDefaultDotsPerInch);
}

void SplashScreen::OnPaint() {
  if (is_open_) RenderFrame();
}

void SplashScreen::OnAnimationFrame() {
  if (is_open_) RenderFrame();
}

std::unique_ptr<ui::Node> SplashScreen::BuildInterface(
    float animation_seconds) const {
  auto root = std::make_unique<ui::Node>();
  YGNodeRef root_layout = root->yoga_node();
  YGNodeStyleSetAlignItems(root_layout, YGAlignCenter);
  YGNodeStyleSetJustifyContent(root_layout, YGJustifyCenter);
  root->style().background = palette_.background_primary;
  root->style().corner_radius = kSplashCornerRadius;
  const ui::Color wash = WithAlpha(palette_.accent, 0x2E);
  root->style().background_gradient = {
      {0.0f, WithAlpha(wash, 0)},
      {0.5f, wash},
      {1.0f, WithAlpha(wash, 0)},
  };
  root->style().gradient_period = kSplashWidth * 1.5f;
  root->style().gradient_offset = animation_seconds * kWashSpeed;
  root->style().border = palette_.glass_border;
  root->style().border_width = 1;

  ui::Node* brand = root->AddChild();
  YGNodeStyleSetFlexDirection(brand->yoga_node(), YGFlexDirectionRow);
  YGNodeStyleSetAlignItems(brand->yoga_node(), YGAlignCenter);
  YGNodeStyleSetGap(brand->yoga_node(), YGGutterColumn, theme::Spacing(3));
  ui::Node* mark = brand->AddChild();
  YGNodeStyleSetWidth(mark->yoga_node(), kBrandMarkSize);
  YGNodeStyleSetHeight(mark->yoga_node(), kBrandMarkSize);
  mark->style().icon = ui::IconKind::kTulip;
  mark->style().icon_color = palette_.high_contrast;
  AddLabel(brand, "Lale", fonts_.brand, palette_.text_primary);

  AddSpace(root.get(), theme::Spacing(5));
  ui::Node* track = root->AddChild();
  YGNodeStyleSetWidth(track->yoga_node(), kLoaderWidth);
  YGNodeStyleSetHeight(track->yoga_node(), kLoaderHeight);
  YGNodeStyleSetFlexDirection(track->yoga_node(), YGFlexDirectionRow);
  track->style().corner_radius = kLoaderHeight / 2;
  track->style().background = palette_.border;
  ui::Node* fill = track->AddChild();
  const bool knows_progress = progress_ > 0 && progress_ < 1;
  YGNodeStyleSetWidthPercent(
      fill->yoga_node(),
      knows_progress ? std::clamp(progress_, 0.0f, 1.0f) * 100 : 100);
  fill->style().corner_radius = kLoaderHeight / 2;
  const ui::Color shine = palette_.high_contrast;
  fill->style().background_gradient = {
      {0.0f, WithAlpha(shine, knows_progress ? 0x99 : 0x00)},
      {0.5f, shine},
      {1.0f, WithAlpha(shine, knows_progress ? 0x99 : 0x00)},
  };
  fill->style().gradient_period = kShimmerPeriod;
  fill->style().gradient_offset = animation_seconds * kShimmerSpeed;

  AddSpace(root.get(), theme::Spacing(3));
  AddLabel(root.get(), status_text_, fonts_.status, palette_.text_secondary);
  return root;
}

void SplashScreen::RenderFrame() {
  const float animation_seconds =
      std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                   opened_at_)
          .count();
  std::unique_ptr<ui::Node> root = BuildInterface(animation_seconds);
  const float logical_width =
      static_cast<float>(window_->client_width()) / device_pixels_per_point_;
  const float logical_height =
      static_cast<float>(window_->client_height()) / device_pixels_per_point_;
  YGNodeStyleSetWidth(root->yoga_node(), logical_width);
  YGNodeStyleSetHeight(root->yoga_node(), logical_height);
  YGNodeCalculateLayout(root->yoga_node(), logical_width, logical_height,
                        YGDirectionLTR);
  root->ResolveBounds(0, 0, {0, 0, logical_width, logical_height});
  renderer_.Resize(window_->client_width(), window_->client_height());
  renderer_.Render(*root, false, device_pixels_per_point_);
  presenter_->Present(renderer_.pixels(), renderer_.width(),
                      renderer_.height());
}

}
