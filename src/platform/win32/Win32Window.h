#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <string_view>

#include "platform/Window.h"

namespace lale {

class Win32Window : public Window {
 public:
  bool Create(std::string_view title, int logical_client_width,
              int logical_client_height, WindowHandler* handler,
              WindowKind kind) override;
  void Show() override;
  void StartAnimationTimer(int interval_milliseconds) override;
  void BringToFront() override;
  void OverrideDotsPerInch(int dots_per_inch, int logical_client_width,
                           int logical_client_height) override;
  void Close() override;
  void UseDarkTitleBar(bool dark_title_bar) override;
  void PostWake() const override;
  void Minimize() override;
  void ToggleMaximize() override;
  bool is_maximized() const override;
  void* native_handle() const override { return hwnd_; }
  int client_width() const override { return client_width_; }
  int client_height() const override { return client_height_; }
  int dots_per_inch() const override {
    return static_cast<int>(dots_per_inch_);
  }
  std::string ClipboardText() const override;
  void SetClipboardText(std::string_view utf8_text) const override;

 private:
  static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  SIZE OuterSizeForLogicalClient(int logical_width, int logical_height) const;
  int ResizeBorderThickness() const;
  LRESULT CalculateClientArea(WPARAM wparam, LPARAM lparam);
  LRESULT HitTestFrame(WPARAM wparam, LPARAM lparam);

  DWORD window_style() const;

  HWND hwnd_ = nullptr;
  WindowKind kind_ = WindowKind::kMain;
  UINT dots_per_inch_ = USER_DEFAULT_SCREEN_DPI;
  WindowHandler* handler_ = nullptr;
  int client_width_ = 0;
  int client_height_ = 0;
  bool tracking_mouse_leave_ = false;
  wchar_t pending_high_surrogate_ = 0;
};

std::string WideToUtf8(const wchar_t* wide_text, int wide_length);
std::wstring Utf8ToWide(std::string_view utf8_text);

}
