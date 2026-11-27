#include "app/Theme.h"

#include <cstdint>

namespace lale::theme {

namespace {

ui::Color Opaque(uint32_t rgb) {
  return {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
          static_cast<uint8_t>(rgb), 255};
}

ui::Color Translucent(uint32_t rgb, uint8_t alpha) {
  ui::Color color = Opaque(rgb);
  color.alpha = alpha;
  return color;
}

ui::Shadow MakeShadow(float offset_y, float blur, float spread,
                      uint8_t black_alpha) {
  return {offset_y, blur, spread, Translucent(0x000000, black_alpha)};
}

Palette MakeDarkPalette() {
  Palette palette;
  palette.is_dark = true;
  palette.background_primary = Opaque(0x0A0A0A);
  palette.background_secondary = Opaque(0x000000);
  palette.component_background = Opaque(0x1A1A1A);
  palette.component_hover = Opaque(0x1F1F1F);
  palette.component_active = Opaque(0x292929);
  palette.border = Translucent(0xFFFFFF, 0x24);
  palette.border_hover = Translucent(0xFFFFFF, 0x3D);
  palette.border_focus = Translucent(0xFFFFFF, 0x82);
  palette.focus_halo = Translucent(0xFFFFFF, 0x21);
  palette.overlay_hover = Translucent(0xFFFFFF, 0x0F);
  palette.overlay_selected = Translucent(0xFFFFFF, 0x17);
  palette.overlay_pressed = Translucent(0xFFFFFF, 0x21);
  palette.high_contrast = Opaque(0xEDEDED);
  palette.high_contrast_hover = Opaque(0xCCCCCC);
  palette.text_on_high_contrast = Opaque(0x0A0A0A);
  palette.text_primary = Opaque(0xEDEDED);
  palette.text_secondary = Opaque(0xA1A1A1);
  palette.text_tertiary = Opaque(0x8F8F8F);
  palette.accent = Opaque(0x0072F5);
  palette.accent_text = Opaque(0x52A8FF);
  palette.warning_background = Opaque(0x291800);
  palette.warning_border = Opaque(0x573300);
  palette.warning_text = Opaque(0xF2A20D);
  palette.warning_indicator = Opaque(0xFFB224);
  palette.close_button_hover = Opaque(0xE5484D);
  palette.close_button_pressed = Opaque(0xC93C41);
  palette.text_on_close_button = Opaque(0xFFFFFF);
  palette.success_indicator = Opaque(0x45A557);
  palette.toggle_off = Opaque(0x454545);
  palette.toggle_knob = Opaque(0xFFFFFF);
  palette.shadow_small = {MakeShadow(1, 2, 0, 0x29)};
  palette.shadow_medium = {MakeShadow(2, 2, 0, 0x52),
                           MakeShadow(8, 8, -8, 0x29)};
  palette.shadow_large = {MakeShadow(2, 2, 0, 0x52),
                          MakeShadow(8, 16, -4, 0x3D),
                          MakeShadow(24, 32, -8, 0x3D)};
  return palette;
}

Palette MakeLightPalette() {
  Palette palette;
  palette.is_dark = false;
  palette.background_primary = Opaque(0xFFFFFF);
  palette.background_secondary = Opaque(0xFAFAFA);
  palette.component_background = Opaque(0xF2F2F2);
  palette.component_hover = Opaque(0xEBEBEB);
  palette.component_active = Opaque(0xE6E6E6);
  palette.border = Translucent(0x000000, 0x14);
  palette.border_hover = Translucent(0x000000, 0x36);
  palette.border_focus = Translucent(0x000000, 0x57);
  palette.focus_halo = Translucent(0x000000, 0x1A);
  palette.overlay_hover = Translucent(0x000000, 0x0D);
  palette.overlay_selected = Translucent(0x000000, 0x14);
  palette.overlay_pressed = Translucent(0x000000, 0x1A);
  palette.high_contrast = Opaque(0x171717);
  palette.high_contrast_hover = Opaque(0x383838);
  palette.text_on_high_contrast = Opaque(0xFFFFFF);
  palette.text_primary = Opaque(0x171717);
  palette.text_secondary = Opaque(0x4D4D4D);
  palette.text_tertiary = Opaque(0x8F8F8F);
  palette.accent = Opaque(0x0072F5);
  palette.accent_text = Opaque(0x0068D6);
  palette.warning_background = Opaque(0xFFF6E6);
  palette.warning_border = Opaque(0xFFDD8F);
  palette.warning_text = Opaque(0xA35200);
  palette.warning_indicator = Opaque(0xFFB224);
  palette.close_button_hover = Opaque(0xE5484D);
  palette.close_button_pressed = Opaque(0xC93C41);
  palette.text_on_close_button = Opaque(0xFFFFFF);
  palette.success_indicator = Opaque(0x45A557);
  palette.toggle_off = Opaque(0xC9C9C9);
  palette.toggle_knob = Opaque(0xFFFFFF);
  palette.shadow_small = {MakeShadow(2, 2, 0, 0x0A)};
  palette.shadow_medium = {MakeShadow(2, 2, 0, 0x0A),
                           MakeShadow(8, 8, -8, 0x0A)};
  palette.shadow_large = {MakeShadow(1, 1, 0, 0x05), MakeShadow(4, 8, -4, 0x0A),
                          MakeShadow(16, 24, -8, 0x0F)};
  return palette;
}

}

Palette MakePalette(const SystemAppearance& appearance) {
  return appearance.prefers_dark_mode ? MakeDarkPalette() : MakeLightPalette();
}

}
