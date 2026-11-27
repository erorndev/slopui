#include "app/Settings.h"

#include <charconv>
#include <sstream>
#include <string_view>
#include <system_error>

#include "app/DataFile.h"

namespace lale {

namespace {

constexpr std::string_view kThemeSystem = "system";
constexpr std::string_view kThemeLight = "light";
constexpr std::string_view kThemeDark = "dark";

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
    generation.model_path = UnescapeLineValue(value);
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
    generation.system_prompt = UnescapeLineValue(value);
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
  return AppDataFilePath(L"settings.ini");
}

StoredSettings LoadStoredSettings(const std::filesystem::path& file_path) {
  StoredSettings settings;
  for (const KeyValueLine& entry : ReadKeyValueLines(file_path)) {
    ApplyEntry(entry.first, entry.second, &settings);
  }
  return settings;
}

bool SaveStoredSettings(const StoredSettings& settings,
                        const std::filesystem::path& file_path) {
  const GenerationSettings& generation = settings.generation;
  std::ostringstream contents;
  contents << "model_path=" << EscapeLineValue(generation.model_path) << '\n'
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
           << "system_prompt=" << EscapeLineValue(generation.system_prompt)
           << '\n'
           << "theme=" << ThemeKey(settings.theme) << '\n';
  return WriteFileAtomically(file_path, contents.str());
}

}
