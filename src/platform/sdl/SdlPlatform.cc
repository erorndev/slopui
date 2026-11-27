#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <system_error>

#include "platform/Platform.h"
#include "platform/sdl/SdlWindow.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <link.h>
#endif

namespace lale {

namespace {

std::filesystem::path PathFromUtf8(const char* utf8_path) {
  if (!utf8_path) return {};
  const std::string text(utf8_path);
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

#ifndef _WIN32
void AddFontDirectoryTree(const std::filesystem::path& root,
                          std::vector<std::filesystem::path>* directories) {
  std::error_code error;
  if (!std::filesystem::is_directory(root, error)) return;
  directories->push_back(root);
  for (std::filesystem::recursive_directory_iterator iterator(
           root, std::filesystem::directory_options::skip_permission_denied,
           error),
       end;
       !error && iterator != end; iterator.increment(error)) {
    if (iterator->is_directory(error)) directories->push_back(iterator->path());
  }
}
#endif

}

bool InitializePlatform() {
  SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
  if (!SDL_Init(SDL_INIT_VIDEO)) return false;
  SdlUserEventType();
  SDL_AddEventWatch(&SdlWindow::DispatchLiveEvent, nullptr);
  return true;
}

void ShutdownPlatform() {
  SDL_RemoveEventWatch(&SdlWindow::DispatchLiveEvent, nullptr);
  SDL_Quit();
}

std::optional<int> PumpEvents() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) return 0;
    SdlWindow::DispatchEvent(event);
  }
  return std::nullopt;
}

void WaitForEvents() { SDL_WaitEvent(nullptr); }

std::filesystem::path UserDataDirectory() {
  char* preference_path = SDL_GetPrefPath(nullptr, "Lale");
  std::filesystem::path directory = PathFromUtf8(preference_path);
  SDL_free(preference_path);
  return directory;
}

std::filesystem::path ExecutableDirectory() {
  return PathFromUtf8(SDL_GetBasePath());
}

std::vector<std::filesystem::path> SystemFontDirectories() {
  std::vector<std::filesystem::path> directories;
#ifdef _WIN32
  for (const char* name : {"windir", "WINDIR", "SystemRoot"}) {
    if (const char* windows_directory = SDL_getenv(name)) {
      directories.push_back(PathFromUtf8(windows_directory) / "Fonts");
      break;
    }
  }
#else
  if (const char* data_home = SDL_getenv("XDG_DATA_HOME")) {
    AddFontDirectoryTree(PathFromUtf8(data_home) / "fonts", &directories);
  }
  if (const char* home = SDL_getenv("HOME")) {
    AddFontDirectoryTree(PathFromUtf8(home) / ".local/share/fonts",
                         &directories);
    AddFontDirectoryTree(PathFromUtf8(home) / ".fonts", &directories);
  }
  AddFontDirectoryTree("/usr/local/share/fonts", &directories);
  AddFontDirectoryTree("/usr/share/fonts", &directories);
#endif
  return directories;
}

std::optional<std::string> EnvironmentVariable(const char* name) {
  const char* value = SDL_getenv(name);
  if (!value || !*value) return std::nullopt;
  return std::string(value);
}

void LowerCurrentThreadPriority() {
  SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
}

std::filesystem::path LoadedLibraryDirectory(std::string_view library_name) {
#ifdef _WIN32
  const std::string file_name = std::string(library_name) + ".dll";
  HMODULE module = GetModuleHandleA(file_name.c_str());
  if (!module) return {};
  wchar_t module_path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, module_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  return std::filesystem::path(module_path, module_path + length).parent_path();
#else
  struct SearchState {
    std::string prefix;
    std::filesystem::path directory;
  };
  SearchState state{"lib" + std::string(library_name) + ".so", {}};
  dl_iterate_phdr(
      [](dl_phdr_info* info, size_t, void* data) -> int {
        if (!info->dlpi_name || !*info->dlpi_name) return 0;
        auto* search = static_cast<SearchState*>(data);
        const std::filesystem::path path(info->dlpi_name);
        const std::string file_name = path.filename().string();
        if (file_name == search->prefix ||
            file_name.starts_with(search->prefix + ".")) {
          search->directory = path.parent_path();
          return 1;
        }
        return 0;
      },
      &state);
  return state.directory;
#endif
}

}
