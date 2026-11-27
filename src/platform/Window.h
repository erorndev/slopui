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

namespace lale {

enum class CursorKind { kArrow, kHand, kIBeam };

enum class WindowRegion { kClient, kCaption };

enum class WindowKind { kMain, kSplash };

class WindowHandler {
 public:
  virtual ~WindowHandler() = default;
  virtual void OnResize(int client_width, int client_height) = 0;
  virtual void OnDpiChanged(UINT dots_per_inch) = 0;
  virtual void OnPaint() = 0;
  virtual void OnMouseMove(float x, float y) = 0;
  virtual void OnMouseLeave() = 0;
  virtual void OnMouseDown(float x, float y, bool shift_down) = 0;
  virtual void OnMouseUp(float x, float y) = 0;
  virtual void OnSystemAppearanceChanged() = 0;
  virtual void OnMouseWheel(float x, float y, float wheel_steps) = 0;
  virtual void OnText(const std::string& utf8_text) = 0;
  virtual void OnKey(UINT virtual_key, bool control_down, bool shift_down) = 0;
  virtual void OnCaretTimer() = 0;
  virtual CursorKind CursorAt(float x, float y) = 0;
  virtual WindowRegion RegionAt(float x, float y) = 0;
  virtual void OnActivationChanged(bool active) = 0;
  virtual void OnWake() = 0;
  virtual void OnAnimationFrame() {}
};

class Window {
 public:
  bool Create(const wchar_t* title, int logical_client_width,
              int logical_client_height, WindowHandler* handler,
              WindowKind kind = WindowKind::kMain);
  void Show(int show_command);
  void StartAnimationTimer(UINT interval_milliseconds);
  void BringToFront();
  void OverrideDotsPerInch(UINT dots_per_inch, int logical_client_width,
                           int logical_client_height);
  void Close();
  void UseDarkTitleBar(bool dark_title_bar);
  void PostWake() const;
  void Minimize();
  void ToggleMaximize();
  bool is_maximized() const;
  HWND hwnd() const { return hwnd_; }
  int client_width() const { return client_width_; }
  int client_height() const { return client_height_; }
  UINT dots_per_inch() const { return dots_per_inch_; }
  std::string ClipboardText() const;
  void SetClipboardText(std::string_view utf8_text) const;

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

}
