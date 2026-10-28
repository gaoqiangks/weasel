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

inline COLORREF GetLanguageBarIconColor() {
  HIGHCONTRASTW contrast = {sizeof(contrast)};
  if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
      (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
    return GetSysColor(COLOR_WINDOWTEXT);
  }

  // The taskbar follows Windows mode, which can differ from application mode.
  DWORD light = 1;
  DWORD bytes = sizeof(light);
  if (RegGetValueW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes) !=
      ERROR_SUCCESS) {
    light = 1;
  }
  return light ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

inline HICON RecolorLanguageBarIcon(HICON source, COLORREF color) {
  ICONINFO info{};
  if (!source || !GetIconInfo(source, &info))
    return nullptr;

  HICON result = nullptr;
  BITMAP bitmap{};
  if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) &&
      bitmap.bmBitsPixel == 32 && bitmap.bmWidth > 0 && bitmap.bmHeight > 0) {
    BITMAPINFO dib{};
    dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = bitmap.bmWidth;
    dib.bmiHeader.biHeight = -bitmap.bmHeight;
    dib.bmiHeader.biPlanes = 1;
    dib.bmiHeader.biBitCount = 32;
    dib.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels = nullptr;
    HBITMAP tinted = CreateDIBSection(nullptr, &dib, DIB_RGB_COLORS,
                                     reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (tinted) {
      HDC dc = GetDC(nullptr);
      const bool read = dc &&
          GetDIBits(dc, info.hbmColor, 0, bitmap.bmHeight, pixels, &dib,
                    DIB_RGB_COLORS) == static_cast<int>(bitmap.bmHeight);
      if (dc)
        ReleaseDC(nullptr, dc);
      if (read) {
        bool has_alpha = false;
        const size_t count = static_cast<size_t>(bitmap.bmWidth) * bitmap.bmHeight;
        for (size_t i = 0; i < count; ++i) {
          const DWORD alpha = pixels[i] >> 24;
          has_alpha |= alpha != 0;
          // Preserve coverage. CreateIconIndirect takes straight color values;
          // premultiplying here would darken antialiased edges a second time.
          const DWORD rgb = (GetRValue(color) << 16) |
                            (GetGValue(color) << 8) | GetBValue(color);
          pixels[i] = (alpha << 24) | (alpha ? rgb : 0);
        }
        if (has_alpha) {
          ICONINFO themed = info;
          themed.hbmColor = tinted;
          result = CreateIconIndirect(&themed);
        }
      }
      DeleteObject(tinted);
    }
  }
  if (info.hbmColor)
    DeleteObject(info.hbmColor);
  if (info.hbmMask)
    DeleteObject(info.hbmMask);
  // Keep a usable, independently owned icon on old formats or GDI failure.
  return result ? result : CopyIcon(source);
}

inline HICON LoadThemedLanguageBarIcon(HINSTANCE instance,
                                       UINT resource,
                                       const std::wstring& path,
                                       SIZE size) {
  HICON source = LoadLanguageBarIcon(instance, resource, path, size);
  // A schema's custom artwork owns its palette; tint only the built-in glyphs.
  if (!source || !path.empty())
    return source;
  HICON themed = RecolorLanguageBarIcon(source, GetLanguageBarIconColor());
  DestroyIcon(source);
  return themed;
}

}  // namespace weasel
