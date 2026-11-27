#include "platform/Platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <shlobj.h>
#include <windows.h>

namespace lale {

bool InitializePlatform() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  return SUCCEEDED(CoInitializeEx(
      nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
}

void ShutdownPlatform() { CoUninitialize(); }

std::optional<int> PumpEvents() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return std::nullopt;
}

void WaitForEvents() { WaitMessage(); }

std::filesystem::path UserDataDirectory() {
  PWSTR roaming_folder = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT,
                                  nullptr, &roaming_folder))) {
    return {};
  }
  std::filesystem::path directory =
      std::filesystem::path(roaming_folder) / L"Lale";
  CoTaskMemFree(roaming_folder);
  return directory;
}

namespace {

std::filesystem::path ModulePath(HMODULE module) {
  wchar_t module_path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, module_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  return std::filesystem::path(module_path, module_path + length);
}

}

std::filesystem::path ExecutableDirectory() {
  return ModulePath(nullptr).parent_path();
}

std::vector<std::filesystem::path> SystemFontDirectories() {
  wchar_t windows_directory[MAX_PATH] = {};
  GetWindowsDirectoryW(windows_directory, MAX_PATH);
  return {std::filesystem::path(windows_directory) / L"Fonts"};
}

std::optional<std::string> EnvironmentVariable(const char* name) {
  char value[256] = {};
  const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
  if (length == 0 || length >= sizeof(value)) return std::nullopt;
  return std::string(value, length);
}

void LowerCurrentThreadPriority() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
}

std::filesystem::path LoadedLibraryDirectory(std::string_view library_name) {
  std::wstring file_name(library_name.begin(), library_name.end());
  file_name += L".dll";
  HMODULE module = GetModuleHandleW(file_name.c_str());
  if (!module) return {};
  return ModulePath(module).parent_path();
}

}
