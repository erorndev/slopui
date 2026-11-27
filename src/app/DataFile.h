#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lale {

using KeyValueLine = std::pair<std::string, std::string>;

std::filesystem::path AppDataFilePath(std::wstring_view file_name);
std::string EscapeLineValue(std::string_view value);
std::string UnescapeLineValue(std::string_view value);
std::vector<KeyValueLine> ReadKeyValueLines(
    const std::filesystem::path& file_path);
bool WriteFileAtomically(const std::filesystem::path& file_path,
                         std::string_view contents);

}
