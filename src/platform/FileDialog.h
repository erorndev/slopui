#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "platform/Window.h"

namespace lale {

using FileDialogCallback =
    std::function<void(std::optional<std::string> chosen_path)>;

void ShowOpenFileDialog(const Window& owner, std::string_view filter_name,
                        std::string_view file_extension,
                        FileDialogCallback on_finished);

}
