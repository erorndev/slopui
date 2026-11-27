#include "platform/sdl/SdlWindow.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace lale {

namespace {

constexpr Uint32 kCaretBlinkMilliseconds = 530;
constexpr int kMinimumClientWidth = 720;
constexpr int kMinimumClientHeight = 480;
constexpr float kResizeBorderThickness = 6;

std::vector<SdlWindow*>& OpenWindows() {
  static std::vector<SdlWindow*> open_windows;
  return open_windows;
}

SDL_SystemCursor SystemCursorFor(CursorKind cursor) {
  switch (cursor) {
    case CursorKind::kHand:
      return SDL_SYSTEM_CURSOR_POINTER;
    case CursorKind::kIBeam:
      return SDL_SYSTEM_CURSOR_TEXT;
    case CursorKind::kArrow:
      break;
  }
  return SDL_SYSTEM_CURSOR_DEFAULT;
}

SDL_Cursor* CursorFor(CursorKind cursor) {
  static SDL_Cursor* cursors[SDL_SYSTEM_CURSOR_COUNT] = {};
  const SDL_SystemCursor system_cursor = SystemCursorFor(cursor);
  if (!cursors[system_cursor]) {
    cursors[system_cursor] = SDL_CreateSystemCursor(system_cursor);
  }
  return cursors[system_cursor];
}

SDL_Keycode NavigationKeycode(SDL_Keycode keycode, SDL_Keymod modifiers) {
  if (modifiers & SDL_KMOD_NUM) return keycode;
  switch (keycode) {
    case SDLK_KP_4:
      return SDLK_LEFT;
    case SDLK_KP_6:
      return SDLK_RIGHT;
    case SDLK_KP_8:
      return SDLK_UP;
    case SDLK_KP_2:
      return SDLK_DOWN;
    case SDLK_KP_7:
      return SDLK_HOME;
    case SDLK_KP_1:
      return SDLK_END;
    case SDLK_KP_PERIOD:
      return SDLK_DELETE;
    default:
      return keycode;
  }
}

Key KeyFromKeycode(SDL_Keycode keycode) {
  switch (keycode) {
    case SDLK_LEFT:
      return Key::kLeft;
    case SDLK_RIGHT:
      return Key::kRight;
    case SDLK_UP:
      return Key::kUp;
    case SDLK_DOWN:
      return Key::kDown;
    case SDLK_HOME:
      return Key::kHome;
    case SDLK_END:
      return Key::kEnd;
    case SDLK_BACKSPACE:
      return Key::kBackspace;
    case SDLK_DELETE:
      return Key::kDelete;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      return Key::kEnter;
    case SDLK_ESCAPE:
      return Key::kEscape;
    case SDLK_COMMA:
      return Key::kComma;
    case SDLK_A:
      return Key::kA;
    case SDLK_C:
      return Key::kC;
    case SDLK_N:
      return Key::kN;
    case SDLK_V:
      return Key::kV;
    case SDLK_X:
      return Key::kX;
    case SDLK_Y:
      return Key::kY;
    case SDLK_Z:
      return Key::kZ;
    default:
      return Key::kUnknown;
  }
}

}

Uint32 SdlUserEventType() {
  static const Uint32 user_event_type = SDL_RegisterEvents(1);
  return user_event_type;
}

std::unique_ptr<Window> CreatePlatformWindow() {
  return std::make_unique<SdlWindow>();
}

SdlWindow::~SdlWindow() { Close(); }

bool SdlWindow::Create(std::string_view title, int logical_client_width,
                       int logical_client_height, WindowHandler* handler,
                       WindowKind kind) {
  kind_ = kind;
  SDL_WindowFlags flags =
      SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS;
  if (kind_ == WindowKind::kMain) flags |= SDL_WINDOW_RESIZABLE;
  const std::string title_text(title);
  window_ = SDL_CreateWindow(title_text.c_str(), logical_client_width,
                             logical_client_height, flags);
  if (!window_) return false;
  window_id_ = SDL_GetWindowID(window_);
  dots_per_inch_ = SystemDotsPerInch();
  ApplyLogicalSize(logical_client_width, logical_client_height);
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED,
                        SDL_WINDOWPOS_CENTERED);
  if (kind_ == WindowKind::kMain) {
    const float window_units_per_logical = static_cast<float>(dots_per_inch_) /
                                           kDefaultDotsPerInch / PixelDensity();
    SDL_SetWindowMinimumSize(
        window_,
        static_cast<int>(kMinimumClientWidth * window_units_per_logical),
        static_cast<int>(kMinimumClientHeight * window_units_per_logical));
    SDL_StartTextInput(window_);
    caret_timer_ =
        SDL_AddTimer(kCaretBlinkMilliseconds, &SdlWindow::PushCaretTimerEvent,
                     WindowIdAsUserData());
  }
  SDL_SetWindowHitTest(window_, &SdlWindow::HitTest, this);
  UpdateClientSize();
  handler_ = handler;
  OpenWindows().push_back(this);
  return true;
}

void SdlWindow::Show() { SDL_ShowWindow(window_); }

void SdlWindow::StartAnimationTimer(int interval_milliseconds) {
  if (animation_timer_) SDL_RemoveTimer(animation_timer_);
  animation_timer_ =
      SDL_AddTimer(static_cast<Uint32>(interval_milliseconds),
                   &SdlWindow::PushAnimationTimerEvent, WindowIdAsUserData());
}

void SdlWindow::BringToFront() { SDL_RaiseWindow(window_); }

void SdlWindow::OverrideDotsPerInch(int dots_per_inch, int logical_client_width,
                                    int logical_client_height) {
  overridden_dots_per_inch_ = dots_per_inch;
  dots_per_inch_ = dots_per_inch;
  if (handler_) handler_->OnDpiChanged(dots_per_inch_);
  ApplyLogicalSize(logical_client_width, logical_client_height);
  UpdateClientSize();
}

void SdlWindow::Close() {
  if (!window_) return;
  if (caret_timer_) SDL_RemoveTimer(caret_timer_);
  if (animation_timer_) SDL_RemoveTimer(animation_timer_);
  caret_timer_ = 0;
  animation_timer_ = 0;
  std::erase(OpenWindows(), this);
  handler_ = nullptr;
  SDL_DestroyWindow(window_);
  window_ = nullptr;
  if (kind_ == WindowKind::kMain) {
    SDL_Event quit_event{};
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
  }
}

void SdlWindow::PostWake() const {
  PushUserEvent(window_id_, SdlUserEventCode::kWake);
}

void SdlWindow::Minimize() { SDL_MinimizeWindow(window_); }

void SdlWindow::ToggleMaximize() {
  if (is_maximized()) {
    SDL_RestoreWindow(window_);
  } else {
    SDL_MaximizeWindow(window_);
  }
}

bool SdlWindow::is_maximized() const {
  return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
}

std::string SdlWindow::ClipboardText() const {
  char* clipboard_text = SDL_GetClipboardText();
  std::string text = clipboard_text ? clipboard_text : "";
  SDL_free(clipboard_text);
  return text;
}

void SdlWindow::SetClipboardText(std::string_view utf8_text) const {
  const std::string text(utf8_text);
  SDL_SetClipboardText(text.c_str());
}

float SdlWindow::PixelDensity() const {
  const float density = SDL_GetWindowPixelDensity(window_);
  return density > 0 ? density : 1;
}

int SdlWindow::SystemDotsPerInch() const {
  if (overridden_dots_per_inch_) return overridden_dots_per_inch_;
  const float display_scale = SDL_GetWindowDisplayScale(window_);
  return static_cast<int>(std::lround(kDefaultDotsPerInch *
                                      (display_scale > 0 ? display_scale : 1)));
}

void SdlWindow::ApplyLogicalSize(int logical_width, int logical_height) {
  const float window_units_per_logical =
      static_cast<float>(dots_per_inch_) / kDefaultDotsPerInch / PixelDensity();
  SDL_SetWindowSize(
      window_,
      static_cast<int>(std::lround(logical_width * window_units_per_logical)),
      static_cast<int>(std::lround(logical_height * window_units_per_logical)));
  SDL_SyncWindow(window_);
}

void SdlWindow::UpdateClientSize() {
  int pixel_width = 0;
  int pixel_height = 0;
  SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height);
  client_width_ = pixel_width;
  client_height_ = pixel_height;
}

void SdlWindow::UpdateCursor(float x, float y) {
  const CursorKind cursor = handler_->CursorAt(x, y);
  if (cursor == current_cursor_) return;
  current_cursor_ = cursor;
  SDL_SetCursor(CursorFor(cursor));
}

void SdlWindow::PushUserEvent(SDL_WindowID window_id, SdlUserEventCode code) {
  SDL_Event user_event{};
  user_event.type = SdlUserEventType();
  user_event.user.windowID = window_id;
  user_event.user.code = static_cast<Sint32>(code);
  SDL_PushEvent(&user_event);
}

Uint32 SDLCALL SdlWindow::PushCaretTimerEvent(void* user_data, SDL_TimerID,
                                              Uint32 interval_milliseconds) {
  PushUserEvent(
      static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(user_data)),
      SdlUserEventCode::kCaretTimer);
  return interval_milliseconds;
}

Uint32 SDLCALL SdlWindow::PushAnimationTimerEvent(
    void* user_data, SDL_TimerID, Uint32 interval_milliseconds) {
  PushUserEvent(
      static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(user_data)),
      SdlUserEventCode::kAnimationTimer);
  return interval_milliseconds;
}

SDL_HitTestResult SDLCALL SdlWindow::HitTest(SDL_Window*,
                                             const SDL_Point* point,
                                             void* user_data) {
  auto* window = static_cast<SdlWindow*>(user_data);
  if (!window->handler_) return SDL_HITTEST_NORMAL;
  if (window->kind_ == WindowKind::kMain && !window->is_maximized()) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window->window_, &width, &height);
    const bool left = point->x < kResizeBorderThickness;
    const bool right = point->x >= width - kResizeBorderThickness;
    const bool top = point->y < kResizeBorderThickness;
    const bool bottom = point->y >= height - kResizeBorderThickness;
    if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (top) return SDL_HITTEST_RESIZE_TOP;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    if (left) return SDL_HITTEST_RESIZE_LEFT;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;
  }
  const float density = window->PixelDensity();
  const WindowRegion region =
      window->handler_->RegionAt(static_cast<float>(point->x) * density,
                                 static_cast<float>(point->y) * density);
  return region == WindowRegion::kCaption ? SDL_HITTEST_DRAGGABLE
                                          : SDL_HITTEST_NORMAL;
}

SdlWindow* SdlWindow::FromWindowId(SDL_WindowID window_id) {
  for (SdlWindow* window : OpenWindows()) {
    if (window->window_id_ == window_id) return window;
  }
  return nullptr;
}

bool SdlWindow::IsLiveEvent(const SDL_Event& event) {
  return event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
         event.type == SDL_EVENT_WINDOW_EXPOSED;
}

bool SDLCALL SdlWindow::DispatchLiveEvent(void*, SDL_Event* event) {
  if (IsLiveEvent(*event)) {
    if (SdlWindow* window = FromWindowId(event->window.windowID)) {
      window->HandleEvent(*event);
    }
  }
  return true;
}

void SdlWindow::DispatchEvent(const SDL_Event& event) {
  if (IsLiveEvent(event)) return;
  if (event.type == SDL_EVENT_SYSTEM_THEME_CHANGED) {
    const std::vector<SdlWindow*> windows = OpenWindows();
    for (SdlWindow* window : windows) {
      if (window->handler_) window->handler_->OnSystemAppearanceChanged();
    }
    return;
  }
  SDL_Window* sdl_window = SDL_GetWindowFromEvent(&event);
  if (!sdl_window) return;
  if (SdlWindow* window = FromWindowId(SDL_GetWindowID(sdl_window))) {
    window->HandleEvent(event);
  }
}

void SdlWindow::HandleEvent(const SDL_Event& event) {
  if (!handler_) return;
  const float density = PixelDensity();
  if (event.type == SdlUserEventType()) {
    switch (static_cast<SdlUserEventCode>(event.user.code)) {
      case SdlUserEventCode::kWake:
        handler_->OnWake();
        break;
      case SdlUserEventCode::kCaretTimer:
        handler_->OnCaretTimer();
        break;
      case SdlUserEventCode::kAnimationTimer:
        handler_->OnAnimationFrame();
        break;
    }
    return;
  }
  switch (event.type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      if (event.window.data1 <= 0 || event.window.data2 <= 0) return;
      client_width_ = event.window.data1;
      client_height_ = event.window.data2;
      handler_->OnResize(client_width_, client_height_);
      return;
    case SDL_EVENT_WINDOW_EXPOSED:
      handler_->OnPaint();
      return;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
      const int new_dots_per_inch = SystemDotsPerInch();
      if (new_dots_per_inch == dots_per_inch_) return;
      dots_per_inch_ = new_dots_per_inch;
      handler_->OnDpiChanged(dots_per_inch_);
      return;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      handler_->OnActivationChanged(true);
      return;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      handler_->OnActivationChanged(false);
      return;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      handler_->OnMouseLeave();
      return;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      if (kind_ == WindowKind::kSplash) {
        SDL_Event quit_event{};
        quit_event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit_event);
      } else {
        Close();
      }
      return;
    case SDL_EVENT_MOUSE_MOTION: {
      const float x = event.motion.x * density;
      const float y = event.motion.y * density;
      handler_->OnMouseMove(x, y);
      if (handler_) UpdateCursor(x, y);
      return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      if (event.button.button != SDL_BUTTON_LEFT) return;
      const float x = event.button.x * density;
      const float y = event.button.y * density;
      if (event.button.clicks == 2 && kind_ == WindowKind::kMain &&
          handler_->RegionAt(x, y) == WindowRegion::kCaption) {
        ToggleMaximize();
        return;
      }
      handler_->OnMouseDown(x, y, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
      return;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button != SDL_BUTTON_LEFT) return;
      handler_->OnMouseUp(event.button.x * density, event.button.y * density);
      return;
    case SDL_EVENT_MOUSE_WHEEL:
      handler_->OnMouseWheel(event.wheel.mouse_x * density,
                             event.wheel.mouse_y * density, event.wheel.y);
      return;
    case SDL_EVENT_TEXT_INPUT:
      handler_->OnText(event.text.text);
      return;
    case SDL_EVENT_KEY_DOWN:
      handler_->OnKey(
          KeyFromKeycode(NavigationKeycode(event.key.key, event.key.mod)),
          (event.key.mod & SDL_KMOD_CTRL) != 0,
          (event.key.mod & SDL_KMOD_SHIFT) != 0);
      return;
    default:
      return;
  }
}

}
