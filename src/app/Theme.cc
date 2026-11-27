#include "app/Theme.h"

#include <cstdint>

namespace lale::theme {

namespace {

constexpr uint32_t kFallbackAccentForDarkMode = 0x60CDFF;
constexpr uint32_t kFallbackAccentForLightMode = 0x005FB8;

ui::Color Opaque(uint32_t rgb) {
  return {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
          static_cast<uint8_t>(rgb), 255};
}

ui::Color Mix(const ui::Color& from, const ui::Color& to, float amount) {
  auto blend = [amount](uint8_t a, uint8_t b) {
    return static_cast<uint8_t>(a + (b - a) * amount + 0.5f);
  };
  return {blend(from.red, to.red), blend(from.green, to.green),
          blend(from.blue, to.blue), 255};
}

float RelativeLuminance(const ui::Color& color) {
  return (0.2126f * color.red + 0.7152f * color.green + 0.0722f * color.blue) /
         255.0f;
}

}

Palette MakePalette(const SystemAppearance& appearance) {
  Palette palette;
  palette.is_dark = appearance.prefers_dark_mode;
  uint32_t accent_rgb = 0;
  if (appearance.has_accent) {
    accent_rgb = palette.is_dark ? appearance.accent.lighter_rgb
                                 : appearance.accent.darker_rgb;
  } else {
    accent_rgb = palette.is_dark ? kFallbackAccentForDarkMode
                                 : kFallbackAccentForLightMode;
  }
  palette.accent = Opaque(accent_rgb);

  if (palette.is_dark) {
    palette.window_background = Opaque(0x202020);
    palette.sidebar_background = Opaque(0x1A1A1A);
    palette.card_background = Opaque(0x2B2B2B);
    palette.control_background = Opaque(0x2D2D2D);
    palette.control_hover = Opaque(0x333333);
    palette.control_pressed = Opaque(0x282828);
    palette.selected_background = Opaque(0x2F2F2F);
    palette.separator = Opaque(0x383838);
    palette.text_primary = Opaque(0xF2F2F2);
    palette.text_secondary = Opaque(0xB8B8B8);
    palette.text_tertiary = Opaque(0x8A8A8A);
    palette.toggle_off = Opaque(0x4A4A4A);
    palette.toggle_knob = Opaque(0xFFFFFF);
    palette.accent_hover = Mix(palette.accent, Opaque(0xFFFFFF), 0.12f);
    palette.accent_pressed = Mix(palette.accent, Opaque(0x000000), 0.12f);
    palette.accent_disabled = Opaque(0x3A3A3A);
    palette.text_on_accent_disabled = Opaque(0x7A7A7A);
  } else {
    palette.window_background = Opaque(0xFAFAFA);
    palette.sidebar_background = Opaque(0xF0F0F0);
    palette.card_background = Opaque(0xFFFFFF);
    palette.control_background = Opaque(0xFFFFFF);
    palette.control_hover = Opaque(0xE8E8E8);
    palette.control_pressed = Opaque(0xDEDEDE);
    palette.selected_background = Opaque(0xE3E3E3);
    palette.separator = Opaque(0xE0E0E0);
    palette.text_primary = Opaque(0x1B1B1B);
    palette.text_secondary = Opaque(0x5C5C5C);
    palette.text_tertiary = Opaque(0x8A8A8A);
    palette.toggle_off = Opaque(0xC8C8C8);
    palette.toggle_knob = Opaque(0xFFFFFF);
    palette.accent_hover = Mix(palette.accent, Opaque(0xFFFFFF), 0.10f);
    palette.accent_pressed = Mix(palette.accent, Opaque(0x000000), 0.10f);
    palette.accent_disabled = Opaque(0xDADADA);
    palette.text_on_accent_disabled = Opaque(0x9A9A9A);
  }
  palette.text_on_accent = RelativeLuminance(palette.accent) > 0.55f
                               ? Opaque(0x000000)
                               : Opaque(0xFFFFFF);
  return palette;
}

}
