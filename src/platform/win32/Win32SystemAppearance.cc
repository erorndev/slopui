#include "platform/SystemAppearance.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace lale {

namespace {

constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kAccentKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";
constexpr int kAccentPaletteEntryCount = 8;
constexpr int kLighterShadeIndex = 1;
constexpr int kBaseShadeIndex = 3;
constexpr int kDarkerShadeIndex = 4;

uint32_t RgbFromPaletteEntry(const BYTE* entry) {
  return (static_cast<uint32_t>(entry[0]) << 16) |
         (static_cast<uint32_t>(entry[1]) << 8) | entry[2];
}

}

SystemAppearance ReadSystemAppearance() {
  SystemAppearance appearance;

  DWORD apps_use_light_theme = 0;
  DWORD value_size = sizeof(apps_use_light_theme);
  if (RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, L"AppsUseLightTheme",
                   RRF_RT_REG_DWORD, nullptr, &apps_use_light_theme,
                   &value_size) == ERROR_SUCCESS) {
    appearance.prefers_dark_mode = apps_use_light_theme == 0;
  }

  BYTE accent_palette[kAccentPaletteEntryCount * 4] = {};
  DWORD palette_size = sizeof(accent_palette);
  if (RegGetValueW(HKEY_CURRENT_USER, kAccentKey, L"AccentPalette",
                   RRF_RT_REG_BINARY, nullptr, accent_palette,
                   &palette_size) == ERROR_SUCCESS &&
      palette_size == sizeof(accent_palette)) {
    appearance.has_accent = true;
    appearance.accent.lighter_rgb =
        RgbFromPaletteEntry(accent_palette + kLighterShadeIndex * 4);
    appearance.accent.base_rgb =
        RgbFromPaletteEntry(accent_palette + kBaseShadeIndex * 4);
    appearance.accent.darker_rgb =
        RgbFromPaletteEntry(accent_palette + kDarkerShadeIndex * 4);
  }
  return appearance;
}

}
