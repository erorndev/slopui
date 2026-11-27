#pragma once

#include <cstdint>

namespace lale {

struct AccentShades {
  uint32_t lighter_rgb = 0;
  uint32_t base_rgb = 0;
  uint32_t darker_rgb = 0;
};

struct SystemAppearance {
  bool prefers_dark_mode = true;
  bool has_accent = false;
  AccentShades accent;
};

SystemAppearance ReadSystemAppearance();

}
