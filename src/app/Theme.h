#pragma once

#include <vector>

#include "platform/SystemAppearance.h"
#include "ui/Node.h"

namespace lale::theme {

struct Palette {
  bool is_dark = true;

  ui::Color background_primary;
  ui::Color background_secondary;

  ui::Color component_background;
  ui::Color component_hover;
  ui::Color component_active;

  ui::Color border;
  ui::Color border_hover;
  ui::Color border_focus;
  ui::Color focus_halo;

  ui::Color overlay_hover;
  ui::Color overlay_selected;
  ui::Color overlay_pressed;

  ui::Color high_contrast;
  ui::Color high_contrast_hover;
  ui::Color text_on_high_contrast;

  ui::Color text_primary;
  ui::Color text_secondary;
  ui::Color text_tertiary;

  ui::Color accent;
  ui::Color accent_text;

  ui::Color warning_background;
  ui::Color warning_border;
  ui::Color warning_text;
  ui::Color warning_indicator;
  ui::Color close_button_hover;
  ui::Color text_selection;
  ui::Color close_button_pressed;
  ui::Color text_on_close_button;
  ui::Color success_indicator;

  ui::Color glass_fill;
  ui::Color glass_border;

  ui::Color toggle_off;
  ui::Color toggle_knob;

  std::vector<ui::Shadow> shadow_small;
  std::vector<ui::Shadow> shadow_medium;
  std::vector<ui::Shadow> shadow_large;
};

Palette MakePalette(const SystemAppearance& appearance);

inline constexpr ui::Color kTransparent{0, 0, 0, 0};

inline constexpr float kSpacingUnit = 4;
constexpr float Spacing(float units) { return kSpacingUnit * units; }

inline constexpr float kSidebarWidth = 264;
inline constexpr float kSettingsNavigationWidth = 232;
inline constexpr float kSettingsColumnMaximumWidth = 680;
inline constexpr float kTopBarHeight = 56;
inline constexpr float kTitleBarHeight = 40;
inline constexpr float kCaptionButtonWidth = 46;
inline constexpr float kListRowHeight = 32;
inline constexpr float kSettingsRowHeight = 48;
inline constexpr float kControlHeight = 32;
inline constexpr float kMessageColumnMaximumWidth = 720;
inline constexpr float kComposerMaximumTextHeight = 200;
inline constexpr float kListMarkerWidth = 20;
inline constexpr float kQuoteBarWidth = 3;
inline constexpr float kWheelScrollDistance = 56;

inline constexpr float kGlassBlur = 24;

inline constexpr float kBaseCornerRadius = 6;
inline constexpr float kRaisedCornerRadius = 12;
inline constexpr float kKeyHintCornerRadius = 4;

constexpr float PointsFromPixels(float pixels) { return pixels * 0.75f; }

inline constexpr float kCaptionPointSize = PointsFromPixels(12);
inline constexpr float kLabelPointSize = PointsFromPixels(13);
inline constexpr float kBodyPointSize = PointsFromPixels(14);
inline constexpr float kHeadlinePointSize = PointsFromPixels(16);
inline constexpr float kTitlePointSize = PointsFromPixels(24);
inline constexpr float kLargeHeadingPointSize = PointsFromPixels(20);
inline constexpr float kMediumHeadingPointSize = PointsFromPixels(17);
inline constexpr float kMonospaceCaptionPointSize = PointsFromPixels(11);
inline constexpr float kMonospaceBodyPointSize = PointsFromPixels(13);

}
