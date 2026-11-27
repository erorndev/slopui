#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "platform/Window.h"

namespace lale {

enum class SdlUserEventCode : Sint32 { kWake, kCaretTimer, kAnimationTimer };

Uint32 SdlUserEventType();

class SdlWindow : public Window {
 public:
  ~SdlWindow() override;

  bool Create(std::string_view title, int logical_client_width,
              int logical_client_height, WindowHandler* handler,
              WindowKind kind) override;
  void Show() override;
  void StartAnimationTimer(int interval_milliseconds) override;
  void BringToFront() override;
  void OverrideDotsPerInch(int dots_per_inch, int logical_client_width,
                           int logical_client_height) override;
  void Close() override;
  void UseDarkTitleBar(bool) override {}
  void PostWake() const override;
  void Minimize() override;
  void ToggleMaximize() override;
  bool is_maximized() const override;
  void* native_handle() const override { return window_; }
  int client_width() const override { return client_width_; }
  int client_height() const override { return client_height_; }
  int dots_per_inch() const override { return dots_per_inch_; }
  std::string ClipboardText() const override;
  void SetClipboardText(std::string_view utf8_text) const override;

  static void DispatchEvent(const SDL_Event& event);
  static bool SDLCALL DispatchLiveEvent(void* user_data, SDL_Event* event);
  static bool IsLiveEvent(const SDL_Event& event);

 private:
  static SdlWindow* FromWindowId(SDL_WindowID window_id);
  static SDL_HitTestResult SDLCALL HitTest(SDL_Window* window,
                                           const SDL_Point* point,
                                           void* user_data);
  static Uint32 SDLCALL PushCaretTimerEvent(void* user_data, SDL_TimerID,
                                            Uint32 interval_milliseconds);
  static Uint32 SDLCALL PushAnimationTimerEvent(void* user_data, SDL_TimerID,
                                                Uint32 interval_milliseconds);
  static void PushUserEvent(SDL_WindowID window_id, SdlUserEventCode code);

  void* WindowIdAsUserData() const {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(window_id_));
  }
  void HandleEvent(const SDL_Event& event);
  float PixelDensity() const;
  int SystemDotsPerInch() const;
  void ApplyLogicalSize(int logical_width, int logical_height);
  void UpdateClientSize();
  void UpdateCursor(float x, float y);

  SDL_Window* window_ = nullptr;
  SDL_WindowID window_id_ = 0;
  WindowKind kind_ = WindowKind::kMain;
  WindowHandler* handler_ = nullptr;
  int client_width_ = 0;
  int client_height_ = 0;
  int dots_per_inch_ = kDefaultDotsPerInch;
  int overridden_dots_per_inch_ = 0;
  SDL_TimerID caret_timer_ = 0;
  SDL_TimerID animation_timer_ = 0;
  CursorKind current_cursor_ = CursorKind::kArrow;
};

}
