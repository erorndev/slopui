#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace lale {

constexpr int kDefaultDotsPerInch = 96;

enum class CursorKind { kArrow, kHand, kIBeam };

enum class WindowRegion { kClient, kCaption };

enum class WindowKind { kMain, kSplash };

enum class Key {
  kUnknown,
  kLeft,
  kRight,
  kUp,
  kDown,
  kHome,
  kEnd,
  kBackspace,
  kDelete,
  kEnter,
  kEscape,
  kComma,
  kA,
  kC,
  kN,
  kV,
  kX,
  kY,
  kZ,
};

class WindowHandler {
 public:
  virtual ~WindowHandler() = default;
  virtual void OnResize(int client_width, int client_height) = 0;
  virtual void OnDpiChanged(int dots_per_inch) = 0;
  virtual void OnPaint() = 0;
  virtual void OnMouseMove(float x, float y) = 0;
  virtual void OnMouseLeave() = 0;
  virtual void OnMouseDown(float x, float y, bool shift_down) = 0;
  virtual void OnMouseUp(float x, float y) = 0;
  virtual void OnSystemAppearanceChanged() = 0;
  virtual void OnMouseWheel(float x, float y, float wheel_steps) = 0;
  virtual void OnText(const std::string& utf8_text) = 0;
  virtual void OnKey(Key key, bool control_down, bool shift_down) = 0;
  virtual void OnCaretTimer() = 0;
  virtual CursorKind CursorAt(float x, float y) = 0;
  virtual WindowRegion RegionAt(float x, float y) = 0;
  virtual void OnActivationChanged(bool active) = 0;
  virtual void OnWake() = 0;
  virtual void OnAnimationFrame() {}
};

class Window {
 public:
  virtual ~Window() = default;
  virtual bool Create(std::string_view title, int logical_client_width,
                      int logical_client_height, WindowHandler* handler,
                      WindowKind kind) = 0;
  virtual void Show() = 0;
  virtual void StartAnimationTimer(int interval_milliseconds) = 0;
  virtual void BringToFront() = 0;
  virtual void OverrideDotsPerInch(int dots_per_inch, int logical_client_width,
                                   int logical_client_height) = 0;
  virtual void Close() = 0;
  virtual void UseDarkTitleBar(bool dark_title_bar) = 0;
  virtual void PostWake() const = 0;
  virtual void Minimize() = 0;
  virtual void ToggleMaximize() = 0;
  virtual bool is_maximized() const = 0;
  virtual void* native_handle() const = 0;
  virtual int client_width() const = 0;
  virtual int client_height() const = 0;
  virtual int dots_per_inch() const = 0;
  virtual std::string ClipboardText() const = 0;
  virtual void SetClipboardText(std::string_view utf8_text) const = 0;
};

std::unique_ptr<Window> CreatePlatformWindow();

}
