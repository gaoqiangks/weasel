#pragma once

#include <Windows.h>
#include <cstdlib>
#include <cwchar>

namespace weasel {

// Receives theme broadcasts on the language bar COM object's owning thread.
// A message-only window cannot receive the broadcasts sent by Windows.
class LanguageBarThemeObserver {
 public:
  using Callback = void (*)(void*);

  LanguageBarThemeObserver() = default;
  LanguageBarThemeObserver(const LanguageBarThemeObserver&) = delete;
  LanguageBarThemeObserver& operator=(const LanguageBarThemeObserver&) = delete;
  ~LanguageBarThemeObserver() { Stop(); }

  bool Start(HINSTANCE instance, Callback callback, void* context) {
    Stop();
    if (!instance || !callback)
      return false;

    // Each observer owns its class so the final window and its DLL window
    // procedure are unregistered together, without cross-thread bookkeeping.
    swprintf_s(_class_name, _countof(_class_name),
                 L"Weasel.LanguageBarThemeObserver.%p", static_cast<void*>(this));
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.lpszClassName = _class_name;
    _class_atom = RegisterClassExW(&window_class);
    if (!_class_atom)
      return false;

    _instance = instance;
    _callback = callback;
    _context = context;
    // Keep callbacks disabled until CreateWindowEx returns: construction can
    // itself dispatch messages, before the owner is ready for a callback.
    _window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              MAKEINTATOM(_class_atom), L"", WS_POPUP,
                              0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!_window) {
      Stop();
      return false;
    }
    _active = true;
    return true;
  }

  void Stop() {
    _active = false;
    _callback = nullptr;
    _context = nullptr;
    if (_window) {
      DestroyWindow(_window);
      _window = nullptr;
    }
    if (_class_atom) {
      UnregisterClassW(MAKEINTATOM(_class_atom), _instance);
      _class_atom = 0;
    }
    _instance = nullptr;
  }

  HWND window() const { return _window; }

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
    auto* observer = reinterpret_cast<LanguageBarThemeObserver*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      observer = static_cast<LanguageBarThemeObserver*>(
          reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA,
                         reinterpret_cast<LONG_PTR>(observer));
      observer->_window = window;
    } else if (message == WM_NCDESTROY) {
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      if (observer)
        observer->_window = nullptr;
    } else if (observer && observer->_active &&
               (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED ||
                message == WM_SYSCOLORCHANGE || message == WM_DISPLAYCHANGE)) {
      const Callback callback = observer->_callback;
      void* const context = observer->_context;
      // The sink may reenter COM and destroy its owner during this callback.
      // Do not access the observer or window after invoking it.
      if (callback)
        callback(context);
      return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }

  HINSTANCE _instance = nullptr;
  HWND _window = nullptr;
  ATOM _class_atom = 0;
  wchar_t _class_name[80]{};
  Callback _callback = nullptr;
  void* _context = nullptr;
  bool _active = false;
};

}  // namespace weasel
