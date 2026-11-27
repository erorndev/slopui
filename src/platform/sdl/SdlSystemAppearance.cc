#include <SDL3/SDL.h>

#include "platform/SystemAppearance.h"

namespace lale {

SystemAppearance ReadSystemAppearance() {
  SystemAppearance appearance;
  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    appearance.prefers_dark_mode =
        SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
  }
  return appearance;
}

}
