#include "app/DataFile.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <windows.h>

#include <fstream>
#include <system_error>

namespace lale {

std::filesystem::path AppDataFilePath(std::wstring_view file_name) {
  PWSTR roaming_folder = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT,
                                  nullptr, &roaming_folder))) {
    return {};
  }
  std::filesystem::path file_path =
      std::filesystem::path(roaming_folder) / L"Lale" / file_name;
  CoTaskMemFree(roaming_folder);
  return file_path;
}

std::string EscapeLineValue(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char character : value) {
    if (character == '\\') {
      escaped += "\\\\";
    } else if (character == '\n') {
      escaped += "\\n";
    } else if (character != '\r') {
      escaped.push_back(character);
    }
  }
  return escaped;
}

std::string UnescapeLineValue(std::string_view value) {
  std::string unescaped;
  unescaped.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '\\' && index + 1 < value.size()) {
      ++index;
      unescaped.push_back(value[index] == 'n' ? '\n' : value[index]);
    } else {
      unescaped.push_back(value[index]);
    }
  }
  return unescaped;
}

std::vector<KeyValueLine> ReadKeyValueLines(
    const std::filesystem::path& file_path) {
  std::vector<KeyValueLine> entries;
  if (file_path.empty()) return entries;
  std::ifstream file(file_path, std::ios::binary);
  if (!file) return entries;
  constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";
  std::string line;
  bool is_first_line = true;
  while (std::getline(file, line)) {
    if (is_first_line && line.starts_with(kByteOrderMark)) {
      line.erase(0, kByteOrderMark.size());
    }
    is_first_line = false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t separator = line.find('=');
    if (separator == std::string::npos) continue;
    entries.emplace_back(line.substr(0, separator), line.substr(separator + 1));
  }
  return entries;
}

bool WriteFileAtomically(const std::filesystem::path& file_path,
                         std::string_view contents) {
  if (file_path.empty()) return false;
  std::error_code error;
  std::filesystem::create_directories(file_path.parent_path(), error);
  std::filesystem::path temporary_path = file_path;
  temporary_path += L".tmp";
  {
    std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << contents;
    if (!file) return false;
  }
  std::filesystem::rename(temporary_path, file_path, error);
  return !error;
}

}
