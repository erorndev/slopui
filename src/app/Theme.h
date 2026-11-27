#pragma once

#include "ui/Node.h"

namespace lale::theme {

inline constexpr ui::Color kTransparent{0, 0, 0, 0};
inline constexpr ui::Color kAppBackground{15, 17, 21, 255};
inline constexpr ui::Color kSidebarBackground{21, 24, 30, 255};
inline constexpr ui::Color kPanelBackground{27, 31, 39, 255};
inline constexpr ui::Color kInputBackground{27, 31, 39, 255};
inline constexpr ui::Color kHoverBackground{34, 39, 51, 255};
inline constexpr ui::Color kSelectedBackground{38, 44, 57, 255};
inline constexpr ui::Color kBorder{38, 43, 53, 255};
inline constexpr ui::Color kTextPrimary{230, 232, 238, 255};
inline constexpr ui::Color kTextMuted{139, 147, 163, 255};
inline constexpr ui::Color kAccent{79, 140, 255, 255};
inline constexpr ui::Color kAccentHover{104, 157, 255, 255};
inline constexpr ui::Color kAccentDisabled{43, 63, 102, 255};
inline constexpr ui::Color kUserBubble{42, 63, 102, 255};
inline constexpr ui::Color kAssistantBubble{27, 31, 39, 255};
inline constexpr ui::Color kToggleOff{58, 64, 78, 255};
inline constexpr ui::Color kToggleKnob{240, 242, 246, 255};

inline constexpr float kSidebarWidth = 264;
inline constexpr float kSettingsPanelWidth = 320;
inline constexpr float kTopBarHeight = 52;
inline constexpr float kMessageColumnMaximumWidth = 760;
inline constexpr float kComposerMaximumHeight = 200;
inline constexpr float kWheelScrollDistance = 56;

inline constexpr float kMinimumChatWidth = 420;

constexpr float PointsFromPixels(float pixels) { return pixels * 0.75f; }

inline constexpr float kBodyPointSize = PointsFromPixels(14);
inline constexpr float kSmallPointSize = PointsFromPixels(12);
inline constexpr float kTitlePointSize = PointsFromPixels(17);

}
