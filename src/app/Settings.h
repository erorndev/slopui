#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lale {

struct GenerationSettings {
  std::string model_path;
  float temperature = 0.8f;
  float top_p = 0.95f;
  int top_k = 40;
  int maximum_tokens = 512;
  float repeat_penalty = 1.1f;
  int context_size = 4096;
  int gpu_layer_count = kAllGpuLayers;
  int thread_count = kAutomaticThreadCount;
  bool stream_tokens = true;
  std::string system_prompt = "You are a helpful assistant.";

  static constexpr int kAllGpuLayers = 100;
  static constexpr int kAutomaticThreadCount = 0;

  bool operator==(const GenerationSettings&) const = default;
};

enum class ThemePreference { kSystem, kLight, kDark };

struct StoredSettings {
  GenerationSettings generation;
  ThemePreference theme = ThemePreference::kSystem;
  std::vector<std::string> recent_model_paths;

  static constexpr size_t kMaximumRecentModels = 5;

  bool operator==(const StoredSettings&) const = default;
};

std::filesystem::path SettingsFilePath();
StoredSettings LoadStoredSettings(const std::filesystem::path& file_path);
bool SaveStoredSettings(const StoredSettings& settings,
                        const std::filesystem::path& file_path);

}
