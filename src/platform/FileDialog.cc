#include "platform/FileDialog.h"

#include <shobjidl.h>

#include <cwchar>

namespace lale {

bool InitializeComApartment() {
  return SUCCEEDED(CoInitializeEx(
      nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
}

void ShutdownComApartment() { CoUninitialize(); }

std::optional<std::string> ShowOpenFileDialog(HWND owner,
                                              const wchar_t* filter_name,
                                              const wchar_t* filter_pattern) {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return std::nullopt;
  }
  COMDLG_FILTERSPEC filter{filter_name, filter_pattern};
  dialog->SetFileTypes(1, &filter);
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);

  std::optional<std::string> chosen_path;
  IShellItem* result = nullptr;
  if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&result))) {
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
