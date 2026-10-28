#pragma once

#include <Windows.h>
#include <string>

namespace weasel {

inline SIZE GetLanguageBarIconSize() {
  // The TSF DLL runs in the focused application's DPI context, but the input
  // mode icon is displayed by Explorer. Use the taskbar's DPI, not the host's.
  // Resolve these APIs at runtime to retain Windows 8.1 compatibility.
  using GetDpiForWindowProc = UINT(WINAPI*)(HWND);
  using GetSystemMetricsForDpiProc = int(WINAPI*)(int, UINT);
  static const auto get_dpi_for_window =
      reinterpret_cast<GetDpiForWindowProc>(
          GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  static const auto get_system_metrics_for_dpi =
      reinterpret_cast<GetSystemMetricsForDpiProc>(GetProcAddress(
          GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));

  if (get_dpi_for_window && get_system_metrics_for_dpi) {
    const UINT dpi = get_dpi_for_window(FindWindowW(L"Shell_TrayWnd", NULL));
    if (dpi) {
      const SIZE size = {get_system_metrics_for_dpi(SM_CXSMICON, dpi),
                         get_system_metrics_for_dpi(SM_CYSMICON, dpi)};
      if (size.cx > 0 && size.cy > 0)
        return size;
    }
  }
  return {GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)};
}

inline HICON LoadLanguageBarIcon(HINSTANCE instance,
                                 UINT resource,
                                 const std::wstring& path,
                                 SIZE size) {
  // GetIcon transfers ownership to TSF, which calls DestroyIcon. Do not use
  // LR_SHARED: each request needs its own handle at the requested pixel size.
  return static_cast<HICON>(LoadImageW(
      path.empty() ? instance : NULL,
      path.empty() ? MAKEINTRESOURCEW(resource) : path.c_str(), IMAGE_ICON,
      size.cx, size.cy, path.empty() ? LR_DEFAULTCOLOR : LR_LOADFROMFILE));
}

}  // namespace weasel
