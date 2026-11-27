#include "platform/Window.h"

#include <windowsx.h>

#include <cwchar>

namespace lale {

namespace {

constexpr UINT_PTR kCaretTimerId = 1;
constexpr UINT kCaretBlinkMilliseconds = 530;
constexpr int kMinimumClientWidth = 720;
constexpr int kMinimumClientHeight = 480;
constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;

}

SIZE Window::OuterSizeForLogicalClient(int logical_width,
                                       int logical_height) const {
  RECT rect{0, 0,
            MulDiv(logical_width, static_cast<int>(dots_per_inch_),
                   USER_DEFAULT_SCREEN_DPI),
            MulDiv(logical_height, static_cast<int>(dots_per_inch_),
                   USER_DEFAULT_SCREEN_DPI)};
  AdjustWindowRectExForDpi(&rect, kWindowStyle, FALSE, 0, dots_per_inch_);
  return {rect.right - rect.left, rect.bottom - rect.top};
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

bool Window::Create(const wchar_t* title, int logical_client_width,
                    int logical_client_height, WindowHandler* handler) {
  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW window_class{sizeof(window_class)};
  window_class.lpfnWndProc = &Window::WindowProcedure;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = L"LaleWindow";
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(0, window_class.lpszClassName, title, kWindowStyle,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT, nullptr, nullptr, instance, this);
  if (!hwnd_) return false;

  dots_per_inch_ = GetDpiForWindow(hwnd_);
  SIZE outer_size =
      OuterSizeForLogicalClient(logical_client_width, logical_client_height);
  SetWindowPos(hwnd_, nullptr, 0, 0, outer_size.cx, outer_size.cy,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

  RECT client_rect{};
  GetClientRect(hwnd_, &client_rect);
  client_width_ = client_rect.right;
  client_height_ = client_rect.bottom;
  handler_ = handler;
  SetTimer(hwnd_, kCaretTimerId, kCaretBlinkMilliseconds, nullptr);
  return true;
}

void Window::Show(int show_command) { ShowWindow(hwnd_, show_command); }

void Window::OverrideDotsPerInch(UINT dots_per_inch, int logical_client_width,
                                 int logical_client_height) {
  dots_per_inch_ = dots_per_inch;
  if (handler_) handler_->OnDpiChanged(dots_per_inch_);
  SIZE outer_size =
      OuterSizeForLogicalClient(logical_client_width, logical_client_height);
  SetWindowPos(hwnd_, nullptr, 0, 0, outer_size.cx, outer_size.cy,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::Close() { DestroyWindow(hwnd_); }

std::string Window::ClipboardText() const {
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

LRESULT CALLBACK Window::WindowProcedure(HWND hwnd, UINT message, WPARAM wparam,
                                         LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* window = static_cast<Window*>(create_struct->lpCreateParams);
    window->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
  }
  auto* window =
      reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (window && window->handler_) {
    return window->HandleMessage(message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Window::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_GETMINMAXINFO: {
      auto* min_max_info = reinterpret_cast<MINMAXINFO*>(lparam);
      SIZE minimum_size =
          OuterSizeForLogicalClient(kMinimumClientWidth, kMinimumClientHeight);
      min_max_info->ptMinTrackSize = {minimum_size.cx, minimum_size.cy};
      return 0;
    }
    case WM_DPICHANGED: {
      dots_per_inch_ = HIWORD(wparam);
      handler_->OnDpiChanged(dots_per_inch_);
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
      handler_->OnMouseDown(static_cast<float>(GET_X_LPARAM(lparam)),
                            static_cast<float>(GET_Y_LPARAM(lparam)));
      return 0;
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
      handler_->OnKey(static_cast<UINT>(wparam), GetKeyState(VK_CONTROL) < 0,
                      GetKeyState(VK_SHIFT) < 0);
      return 0;
    case WM_TIMER:
      if (wparam == kCaretTimerId) handler_->OnCaretTimer();
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd_, kCaretTimerId);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}
