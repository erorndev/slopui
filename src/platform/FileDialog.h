#pragma once

#include <optional>
#include <string>

#include "platform/Window.h"

namespace lale {

bool InitializeComApartment();
void ShutdownComApartment();

std::optional<std::string> ShowOpenFileDialog(HWND owner,
                                              const wchar_t* filter_name,
                                              const wchar_t* filter_pattern);

}
