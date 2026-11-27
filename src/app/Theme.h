#pragma once

#include "platform/SystemAppearance.h"
#include "ui/Node.h"

namespace lale::theme {

struct Palette {
  bool is_dark = true;
  ui::Color window_background;
  ui::Color sidebar_background;
  ui::Color card_background;
  ui::Color control_background;
  ui::Color control_hover;
  ui::Color control_pressed;
  ui::Color selected_background;
  ui::Color separator;
  ui::Color text_primary;
  ui::Color text_secondary;
  ui::Color text_tertiary;
  ui::Color accent;
  ui::Color accent_hover;
  ui::Color accent_pressed;
  ui::Color text_on_accent;
  ui::Color accent_disabled;
  ui::Color text_on_accent_disabled;
  ui::Color toggle_off;
  ui::Color toggle_knob;
};

Palette MakePalette(const SystemAppearance& appearance);

inline constexpr ui::Color kTransparent{0, 0, 0, 0};

inline constexpr float kSpacingUnit = 4;
constexpr float Spacing(float units) { return kSpacingUnit * units; }

inline constexpr float kSidebarWidth = 260;
inline constexpr float kSettingsPanelWidth = 340;
inline constexpr float kMinimumChatWidth = 420;
inline constexpr float kTopBarHeight = 52;
inline constexpr float kListRowHeight = 34;
inline constexpr float kSettingsRowHeight = 44;
inline constexpr float kMessageColumnMaximumWidth = 720;
inline constexpr float kComposerMaximumTextHeight = 200;
inline constexpr float kWheelScrollDistance = 56;

inline constexpr float kControlCornerRadius = 6;
inline constexpr float kCardCornerRadius = 10;
inline constexpr float kBubbleCornerRadius = 18;
inline constexpr float kComposerCornerRadius = 14;

constexpr float PointsFromPixels(float pixels) { return pixels * 0.75f; }

inline constexpr float kCaptionPointSize = PointsFromPixels(12);
inline constexpr float kBodyPointSize = PointsFromPixels(14);
inline constexpr float kHeadlinePointSize = PointsFromPixels(15);
inline constexpr float kTitlePointSize = PointsFromPixels(22);

}
