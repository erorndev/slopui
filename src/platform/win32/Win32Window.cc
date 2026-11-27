#include "platform/win32/Win32Window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <cstring>
#include <cwchar>

namespace lale {

namespace {

constexpr UINT_PTR kCaretTimerId = 1;
constexpr UINT_PTR kAnimationTimerId = 2;
constexpr UINT kWakeMessage = WM_APP + 1;
constexpr UINT kCaretBlinkMilliseconds = 530;
constexpr int kMinimumClientWidth = 720;
constexpr int kMinimumClientHeight = 480;

}

DWORD Win32Window::window_style() const {
  return kind_ == WindowKind::kSplash ? WS_POPUP : WS_OVERLAPPEDWINDOW;
}

SIZE Win32Window::OuterSizeForLogicalClient(int logical_width,
                                            int logical_height) const {
  RECT rect{0, 0,
            MulDiv(logical_width, static_cast<int>(dots_per_inch_),
                   USER_DEFAULT_SCREEN_DPI),
            MulDiv(logical_height, static_cast<int>(dots_per_inch_),
                   USER_DEFAULT_SCREEN_DPI)};
  if (kind_ == WindowKind::kSplash) return {rect.right, rect.bottom};
  AdjustWindowRectExForDpi(&rect, window_style(), FALSE, 0, dots_per_inch_);
  return {rect.right - rect.left, rect.bottom};
}

int Win32Window::ResizeBorderThickness() const {
  return GetSystemMetricsForDpi(SM_CYFRAME, dots_per_inch_) +
         GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dots_per_inch_);
}

LRESULT Win32Window::CalculateClientArea(WPARAM wparam, LPARAM lparam) {
  if (!wparam) return DefWindowProcW(hwnd_, WM_NCCALCSIZE, wparam, lparam);
  auto* parameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
  const LONG original_top = parameters->rgrc[0].top;
  LRESULT result = DefWindowProcW(hwnd_, WM_NCCALCSIZE, wparam, lparam);
  if (result != 0) return result;
  parameters->rgrc[0].top = original_top;
  if (is_maximized()) parameters->rgrc[0].top += ResizeBorderThickness();
  return 0;
}

LRESULT Win32Window::HitTestFrame(WPARAM wparam, LPARAM lparam) {
  LRESULT frame_hit = DefWindowProcW(hwnd_, WM_NCHITTEST, wparam, lparam);
  if (frame_hit != HTCLIENT) return frame_hit;
  POINT cursor{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
  ScreenToClient(hwnd_, &cursor);
  if (kind_ == WindowKind::kMain && !is_maximized() &&
      cursor.y < ResizeBorderThickness()) {
    return HTTOP;
  }
  if (handler_->RegionAt(static_cast<float>(cursor.x),
                         static_cast<float>(cursor.y)) ==
      WindowRegion::kCaption) {
    return HTCAPTION;
  }
  return HTCLIENT;
}

Key KeyFromVirtualKey(WPARAM virtual_key) {
  switch (virtual_key) {
    case VK_LEFT:
      return Key::kLeft;
    case VK_RIGHT:
      return Key::kRight;
    case VK_UP:
      return Key::kUp;
    case VK_DOWN:
      return Key::kDown;
    case VK_HOME:
      return Key::kHome;
    case VK_END:
      return Key::kEnd;
    case VK_BACK:
      return Key::kBackspace;
    case VK_DELETE:
      return Key::kDelete;
    case VK_RETURN:
      return Key::kEnter;
    case VK_ESCAPE:
      return Key::kEscape;
    case VK_OEM_COMMA:
      return Key::kComma;
    case 'A':
      return Key::kA;
    case 'C':
      return Key::kC;
    case 'N':
      return Key::kN;
    case 'V':
      return Key::kV;
    case 'X':
      return Key::kX;
    case 'Y':
      return Key::kY;
    case 'Z':
      return Key::kZ;
    default:
      return Key::kUnknown;
  }
}

std::unique_ptr<Window> CreatePlatformWindow() {
  return std::make_unique<Win32Window>();
}

std::wstring Utf8ToWide(std::string_view utf8_text) {
  if (utf8_text.empty()) return {};
  const int source_length = static_cast<int>(utf8_text.size());
  const int wide_length = MultiByteToWideChar(CP_UTF8, 0, utf8_text.data(),
                                              source_length, nullptr, 0);
  std::wstring wide_text(static_cast<size_t>(wide_length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8_text.data(), source_length,
                      wide_text.data(), wide_length);
  return wide_text;
}

std::string WideToUtf8(const wchar_t* wide_text, int wide_length) {
  if (wide_length <= 0) return {};
  int byte_count = WideCharToMultiByte(CP_UTF8, 0, wide_text, wide_length,
                                       nullptr, 0, nullptr, nullptr);
  std::string utf8_text(static_cast<size_t>(byte_count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide_text, wide_length, utf8_text.data(),
                      byte_count, nullptr, nullptr);
  return utf8_text;
}

bool Win32Window::Create(std::string_view utf8_title, int logical_client_width,
                         int logical_client_height, WindowHandler* handler,
                         WindowKind kind) {
  const std::wstring title = Utf8ToWide(utf8_title);
  kind_ = kind;
  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW window_class{sizeof(window_class)};
  window_class.lpfnWndProc = &Win32Window::WindowProcedure;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = L"LaleWindow";
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(0, window_class.lpszClassName, title.c_str(),
                          window_style(), CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr,
                          instance, this);
  if (!hwnd_) return false;

  dots_per_inch_ = GetDpiForWindow(hwnd_);
  SIZE outer_size =
      OuterSizeForLogicalClient(logical_client_width, logical_client_height);
  if (kind_ == WindowKind::kSplash) {
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor_info{sizeof(monitor_info)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY),
                    &monitor_info);
    const RECT& work_area = monitor_info.rcWork;
    SetWindowPos(
        hwnd_, nullptr,
        work_area.left + (work_area.right - work_area.left - outer_size.cx) / 2,
        work_area.top + (work_area.bottom - work_area.top - outer_size.cy) / 2,
        outer_size.cx, outer_size.cy,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner_preference, sizeof(corner_preference));
  } else {
    SetWindowPos(hwnd_, nullptr, 0, 0, outer_size.cx, outer_size.cy,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  client_width_ = client_rect.right;
  client_height_ = client_rect.bottom;
  handler_ = handler;
  if (kind_ == WindowKind::kMain) {
    SetTimer(hwnd_, kCaretTimerId, kCaretBlinkMilliseconds, nullptr);
  }
  return true;
}

void Win32Window::Show() {
  ShowWindow(hwnd_,
             kind_ == WindowKind::kMain ? SW_SHOWDEFAULT : SW_SHOWNORMAL);
}

void Win32Window::StartAnimationTimer(int interval_milliseconds) {
  SetTimer(hwnd_, kAnimationTimerId, static_cast<UINT>(interval_milliseconds),
           nullptr);
}

void Win32Window::BringToFront() { SetForegroundWindow(hwnd_); }

void Win32Window::OverrideDotsPerInch(int dots_per_inch,
                                      int logical_client_width,
                                      int logical_client_height) {
  dots_per_inch_ = static_cast<UINT>(dots_per_inch);
  if (handler_) handler_->OnDpiChanged(dots_per_inch);
  SIZE outer_size =
      OuterSizeForLogicalClient(logical_client_width, logical_client_height);
  SetWindowPos(hwnd_, nullptr, 0, 0, outer_size.cx, outer_size.cy,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Win32Window::Close() { DestroyWindow(hwnd_); }

void Win32Window::UseDarkTitleBar(bool dark_title_bar) {
  if (!hwnd_) return;
  BOOL immersive_dark_mode = dark_title_bar ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                        &immersive_dark_mode, sizeof(immersive_dark_mode));
}

void Win32Window::PostWake() const { PostMessageW(hwnd_, kWakeMessage, 0, 0); }

void Win32Window::Minimize() { ShowWindow(hwnd_, SW_MINIMIZE); }

void Win32Window::ToggleMaximize() {
  ShowWindow(hwnd_, is_maximized() ? SW_RESTORE : SW_MAXIMIZE);
}

bool Win32Window::is_maximized() const { return IsZoomed(hwnd_) != FALSE; }

std::string Win32Window::ClipboardText() const {
  std::string clipboard_text;
  if (!OpenClipboard(hwnd_)) return clipboard_text;
  if (HANDLE clipboard_data = GetClipboardData(CF_UNICODETEXT)) {
    if (auto* wide_text =
            static_cast<const wchar_t*>(GlobalLock(clipboard_data))) {
      clipboard_text =
          WideToUtf8(wide_text, static_cast<int>(std::wcslen(wide_text)));
      GlobalUnlock(clipboard_data);
    }
  }
  CloseClipboard();
  return clipboard_text;
}

void Win32Window::SetClipboardText(std::string_view utf8_text) const {
  std::string with_carriage_returns;
  with_carriage_returns.reserve(utf8_text.size());
  for (char character : utf8_text) {
    if (character == '\n') with_carriage_returns.push_back('\r');
    if (character != '\r') with_carriage_returns.push_back(character);
  }
  const std::wstring wide_source = Utf8ToWide(with_carriage_returns);
  const size_t byte_count = (wide_source.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
  if (!memory) return;
  std::memcpy(GlobalLock(memory), wide_source.c_str(), byte_count);
  GlobalUnlock(memory);
  if (!OpenClipboard(hwnd_)) {
    GlobalFree(memory);
    return;
  }
  EmptyClipboard();
  if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
  CloseClipboard();
}

LRESULT CALLBACK Win32Window::WindowProcedure(HWND hwnd, UINT message,
                                              WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* window = static_cast<Win32Window*>(create_struct->lpCreateParams);
    window->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
  }
  auto* window =
      reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (window && message == WM_NCCALCSIZE) {
    return window->CalculateClientArea(wparam, lparam);
  }
  if (window && window->handler_) {
    return window->HandleMessage(message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Win32Window::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_GETMINMAXINFO: {
      if (kind_ == WindowKind::kSplash) break;
      auto* min_max_info = reinterpret_cast<MINMAXINFO*>(lparam);
      SIZE minimum_size =
          OuterSizeForLogicalClient(kMinimumClientWidth, kMinimumClientHeight);
      min_max_info->ptMinTrackSize = {minimum_size.cx, minimum_size.cy};
      return 0;
    }
    case WM_DPICHANGED: {
      dots_per_inch_ = HIWORD(wparam);
      handler_->OnDpiChanged(static_cast<int>(dots_per_inch_));
      const auto* suggested_rect = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd_, nullptr, suggested_rect->left, suggested_rect->top,
                   suggested_rect->right - suggested_rect->left,
                   suggested_rect->bottom - suggested_rect->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SIZE: {
      int new_width = LOWORD(lparam);
      int new_height = HIWORD(lparam);
      if (wparam == SIZE_MINIMIZED || new_width == 0 || new_height == 0) {
        return 0;
      }
      client_width_ = new_width;
      client_height_ = new_height;
      handler_->OnResize(new_width, new_height);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint_struct;
      BeginPaint(hwnd_, &paint_struct);
      EndPaint(hwnd_, &paint_struct);
      handler_->OnPaint();
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_NCHITTEST:
      return HitTestFrame(wparam, lparam);
    case kWakeMessage:
      handler_->OnWake();
      return 0;
    case WM_ACTIVATE:
      handler_->OnActivationChanged(LOWORD(wparam) != WA_INACTIVE);
      break;
    case WM_MOUSEMOVE:
      if (!tracking_mouse_leave_) {
        TRACKMOUSEEVENT track_event{sizeof(track_event), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track_event);
        tracking_mouse_leave_ = true;
      }
      handler_->OnMouseMove(static_cast<float>(GET_X_LPARAM(lparam)),
                            static_cast<float>(GET_Y_LPARAM(lparam)));
      return 0;
    case WM_MOUSELEAVE:
      tracking_mouse_leave_ = false;
      handler_->OnMouseLeave();
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd_);
      SetCapture(hwnd_);
      handler_->OnMouseDown(static_cast<float>(GET_X_LPARAM(lparam)),
                            static_cast<float>(GET_Y_LPARAM(lparam)),
                            (wparam & MK_SHIFT) != 0);
      return 0;
    case WM_LBUTTONUP:
      ReleaseCapture();
      handler_->OnMouseUp(static_cast<float>(GET_X_LPARAM(lparam)),
                          static_cast<float>(GET_Y_LPARAM(lparam)));
      return 0;
    case WM_SETTINGCHANGE:
      if (lparam && std::wcscmp(reinterpret_cast<const wchar_t*>(lparam),
                                L"ImmersiveColorSet") == 0) {
        handler_->OnSystemAppearanceChanged();
      }
      break;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
      handler_->OnSystemAppearanceChanged();
      break;
    case WM_MOUSEWHEEL: {
      POINT cursor{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd_, &cursor);
      float wheel_steps =
          static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
      handler_->OnMouseWheel(static_cast<float>(cursor.x),
                             static_cast<float>(cursor.y), wheel_steps);
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT) {
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(hwnd_, &cursor);
        LPCWSTR cursor_id = IDC_ARROW;
        switch (handler_->CursorAt(static_cast<float>(cursor.x),
                                   static_cast<float>(cursor.y))) {
          case CursorKind::kHand:
            cursor_id = IDC_HAND;
            break;
          case CursorKind::kIBeam:
            cursor_id = IDC_IBEAM;
            break;
          case CursorKind::kArrow:
            break;
        }
        SetCursor(LoadCursorW(nullptr, cursor_id));
        return TRUE;
      }
      break;
    case WM_CHAR: {
      auto character = static_cast<wchar_t>(wparam);
      if (character >= 0xD800 && character <= 0xDBFF) {
        pending_high_surrogate_ = character;
        return 0;
      }
      if (character < 0x20 || character == 0x7F) return 0;
      if (character >= 0xDC00 && character <= 0xDFFF &&
          pending_high_surrogate_) {
        wchar_t surrogate_pair[2] = {pending_high_surrogate_, character};
        pending_high_surrogate_ = 0;
        handler_->OnText(WideToUtf8(surrogate_pair, 2));
      } else {
        handler_->OnText(WideToUtf8(&character, 1));
      }
      return 0;
    }
    case WM_KEYDOWN:
      handler_->OnKey(KeyFromVirtualKey(wparam), GetKeyState(VK_CONTROL) < 0,
                      GetKeyState(VK_SHIFT) < 0);
      return 0;
    case WM_TIMER:
      if (wparam == kCaretTimerId) handler_->OnCaretTimer();
      if (wparam == kAnimationTimerId) handler_->OnAnimationFrame();
      return 0;
    case WM_CLOSE:
      if (kind_ == WindowKind::kSplash) {
        PostQuitMessage(0);
        return 0;
      }
      break;
    case WM_DESTROY:
      KillTimer(hwnd_, kCaretTimerId);
      KillTimer(hwnd_, kAnimationTimerId);
      handler_ = nullptr;
      if (kind_ == WindowKind::kMain) PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}
