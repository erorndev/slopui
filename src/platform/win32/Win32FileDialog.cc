#include <shobjidl.h>

#include <cwchar>

#include "platform/FileDialog.h"
#include "platform/win32/Win32Window.h"

namespace lale {

namespace {

std::optional<std::string> RunOpenFileDialog(const Window& owner,
                                             std::string_view filter_name,
                                             std::string_view file_extension) {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return std::nullopt;
  }
  const std::wstring wide_filter_name = Utf8ToWide(filter_name);
  const std::wstring wide_filter_pattern = L"*." + Utf8ToWide(file_extension);
  COMDLG_FILTERSPEC filter{wide_filter_name.c_str(),
                           wide_filter_pattern.c_str()};
  dialog->SetFileTypes(1, &filter);
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);

  std::optional<std::string> chosen_path;
  IShellItem* result = nullptr;
  HWND owner_handle = static_cast<HWND>(owner.native_handle());
  if (SUCCEEDED(dialog->Show(owner_handle)) &&
      SUCCEEDED(dialog->GetResult(&result))) {
    PWSTR wide_path = nullptr;
    if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &wide_path))) {
      chosen_path =
          WideToUtf8(wide_path, static_cast<int>(std::wcslen(wide_path)));
      CoTaskMemFree(wide_path);
    }
    result->Release();
  }
  dialog->Release();
  return chosen_path;
}

}

void ShowOpenFileDialog(const Window& owner, std::string_view filter_name,
                        std::string_view file_extension,
                        FileDialogCallback on_finished) {
  on_finished(RunOpenFileDialog(owner, filter_name, file_extension));
}

}
