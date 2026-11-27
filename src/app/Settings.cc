#include "app/Settings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <windows.h>

#include <charconv>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

namespace lale {

namespace {

constexpr std::string_view kThemeSystem = "system";
constexpr std::string_view kThemeLight = "light";
constexpr std::string_view kThemeDark = "dark";

std::string EscapeValue(std::string_view value) {
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

std::string UnescapeValue(std::string_view value) {
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

std::string FormatFloat(float value) {
  char buffer[32];
  auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                              std::chars_format::fixed, 2);
  return std::string(buffer, result.ptr);
}

template <typename Number>
void ParseNumber(std::string_view text, Number* destination) {
  Number parsed{};
  auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec == std::errc() && result.ptr == text.data() + text.size()) {
    *destination = parsed;
  }
}

void ApplyEntry(std::string_view key, std::string_view value,
                StoredSettings* settings) {
  GenerationSettings& generation = settings->generation;
  if (key == "model_path") {
    generation.model_path = UnescapeValue(value);
  } else if (key == "temperature") {
    ParseNumber(value, &generation.temperature);
  } else if (key == "top_p") {
    ParseNumber(value, &generation.top_p);
  } else if (key == "top_k") {
    ParseNumber(value, &generation.top_k);
  } else if (key == "maximum_tokens") {
    ParseNumber(value, &generation.maximum_tokens);
  } else if (key == "repeat_penalty") {
    ParseNumber(value, &generation.repeat_penalty);
  } else if (key == "context_size") {
    ParseNumber(value, &generation.context_size);
  } else if (key == "gpu_layer_count") {
    ParseNumber(value, &generation.gpu_layer_count);
  } else if (key == "thread_count") {
    ParseNumber(value, &generation.thread_count);
  } else if (key == "stream_tokens") {
    generation.stream_tokens = value == "true";
  } else if (key == "system_prompt") {
    generation.system_prompt = UnescapeValue(value);
  } else if (key == "theme") {
    if (value == kThemeSystem) settings->theme = ThemePreference::kSystem;
    if (value == kThemeLight) settings->theme = ThemePreference::kLight;
    if (value == kThemeDark) settings->theme = ThemePreference::kDark;
  }
}

std::string_view ThemeKey(ThemePreference theme) {
  switch (theme) {
    case ThemePreference::kLight:
      return kThemeLight;
    case ThemePreference::kDark:
      return kThemeDark;
    case ThemePreference::kSystem:
      break;
  }
  return kThemeSystem;
}

}

std::filesystem::path SettingsFilePath() {
  PWSTR roaming_folder = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT,
                                  nullptr, &roaming_folder))) {
    return {};
  }
  std::filesystem::path file_path =
      std::filesystem::path(roaming_folder) / L"Lale" / L"settings.ini";
  CoTaskMemFree(roaming_folder);
  return file_path;
}

StoredSettings LoadStoredSettings(const std::filesystem::path& file_path) {
  StoredSettings settings;
  if (file_path.empty()) return settings;
  std::ifstream file(file_path, std::ios::binary);
  if (!file) return settings;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t separator = line.find('=');
    if (separator == std::string::npos) continue;
    ApplyEntry(std::string_view(line).substr(0, separator),
               std::string_view(line).substr(separator + 1), &settings);
  }
  return settings;
}

bool SaveStoredSettings(const StoredSettings& settings,
                        const std::filesystem::path& file_path) {
  if (file_path.empty()) return false;
  const GenerationSettings& generation = settings.generation;
  std::ostringstream contents;
  contents << "model_path=" << EscapeValue(generation.model_path) << '\n'
           << "temperature=" << FormatFloat(generation.temperature) << '\n'
           << "top_p=" << FormatFloat(generation.top_p) << '\n'
           << "top_k=" << generation.top_k << '\n'
           << "maximum_tokens=" << generation.maximum_tokens << '\n'
           << "repeat_penalty=" << FormatFloat(generation.repeat_penalty)
           << '\n'
           << "context_size=" << generation.context_size << '\n'
           << "gpu_layer_count=" << generation.gpu_layer_count << '\n'
           << "thread_count=" << generation.thread_count << '\n'
           << "stream_tokens=" << (generation.stream_tokens ? "true" : "false")
           << '\n'
           << "system_prompt=" << EscapeValue(generation.system_prompt) << '\n'
           << "theme=" << ThemeKey(settings.theme) << '\n';

  std::error_code error;
  std::filesystem::create_directories(file_path.parent_path(), error);
  std::filesystem::path temporary_path = file_path;
  temporary_path += L".tmp";
  {
    std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << contents.str();
    if (!file) return false;
  }
  std::filesystem::rename(temporary_path, file_path, error);
  return !error;
}

}
