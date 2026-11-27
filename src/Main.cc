#include <thorvg.h>

#include "app/App.h"
#include "platform/FileDialog.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show_command) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (!lale::InitializeComApartment()) return 1;
  if (tvg::Initializer::init(0) != tvg::Result::Success) return 1;
  int exit_code = 1;
  {
    lale::App app;
    if (app.Initialize(show_command)) exit_code = app.Run();
  }
  tvg::Initializer::term();
  lale::ShutdownComApartment();
  return exit_code;
}
