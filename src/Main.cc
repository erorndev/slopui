#include <thorvg.h>

#include "app/App.h"
#include "platform/Platform.h"

int main() {
  if (!lale::InitializePlatform()) return 1;
  if (tvg::Initializer::init(0) != tvg::Result::Success) return 1;
  int exit_code = 1;
  {
    lale::App app;
    if (app.Initialize()) exit_code = app.Run();
  }
  tvg::Initializer::term();
  lale::ShutdownPlatform();
  return exit_code;
}
