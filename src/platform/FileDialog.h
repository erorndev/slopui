#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "platform/Window.h"

namespace lale {

std::optional<std::string> ShowOpenFileDialog(const Window& owner,
                                              std::string_view filter_name,
                                              std::string_view file_extension);

}
