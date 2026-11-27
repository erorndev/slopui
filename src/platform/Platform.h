#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lale {

bool InitializePlatform();
void ShutdownPlatform();

std::optional<int> PumpEvents();
void WaitForEvents();

std::filesystem::path UserDataDirectory();
std::filesystem::path ExecutableDirectory();
std::vector<std::filesystem::path> SystemFontDirectories();
std::optional<std::string> EnvironmentVariable(const char* name);
void LowerCurrentThreadPriority();
std::filesystem::path LoadedLibraryDirectory(std::string_view library_name);

}
