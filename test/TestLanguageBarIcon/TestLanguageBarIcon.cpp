#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0603
#define WINVER 0x0603

#include "../../WeaselTSF/LanguageBarIcon.h"
#include "../../include/resource.h"
#include <cwchar>
#include <string>
#include <vector>

static int failures = 0;

static void Check(bool condition, const wchar_t* message) {
  if (!condition) {
    std::fwprintf(stderr, L"FAIL: %ls (error=%lu)\n", message, GetLastError());
    ++failures;
  }
}

static SIZE IconSize(HICON icon) {
  ICONINFO info{};
  SIZE size{};
  if (icon && GetIconInfo(icon, &info)) {
    BITMAP bitmap{};
    if (GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask,
                   sizeof(bitmap), &bitmap)) {
      size.cx = bitmap.bmWidth;
      size.cy = info.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
    }
    if (info.hbmColor)
      DeleteObject(info.hbmColor);
    if (info.hbmMask)
      DeleteObject(info.hbmMask);
  }
  return size;
}

static void CheckSize(HICON icon, SIZE expected) {
  const SIZE actual = IconSize(icon);
  Check(actual.cx == expected.cx && actual.cy == expected.cy,
        L"the returned icon must have the requested pixel dimensions");
}

static void TestDpiContexts() {
  const HMODULE user32 = GetModuleHandleW(L"user32.dll");
  const auto get_dpi = reinterpret_cast<UINT(WINAPI*)(HWND)>(
      GetProcAddress(user32, "GetDpiForWindow"));
  const auto get_metrics = reinterpret_cast<int(WINAPI*)(int, UINT)>(
      GetProcAddress(user32, "GetSystemMetricsForDpi"));
  const auto set_context = reinterpret_cast<HANDLE(WINAPI*)(HANDLE)>(
      GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
  const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (!get_dpi || !get_metrics || !set_context || !taskbar) {
    const SIZE actual = weasel::GetLanguageBarIconSize();
    Check(actual.cx == GetSystemMetrics(SM_CXSMICON) &&
              actual.cy == GetSystemMetrics(SM_CYSMICON),
          L"the compatibility fallback must use system icon metrics");
    std::wprintf(L"DPI-context checks skipped: newer DPI APIs or Explorer absent.\n");
    return;
  }

  const UINT dpi = get_dpi(taskbar);
  Check(dpi != 0, L"Explorer taskbar DPI must be available");
  const SIZE expected = {get_metrics(SM_CXSMICON, dpi),
                          get_metrics(SM_CYSMICON, dpi)};
  const wchar_t* labels[] = {L"unaware", L"system", L"per-monitor",
                             L"per-monitor-v2"};
  for (int index = 0; index < 4; ++index) {
    const HANDLE previous =
        set_context(reinterpret_cast<HANDLE>(-(INT_PTR)(index + 1)));
    if (!previous) {
      std::wprintf(L"SKIP: %ls context unsupported by this Windows version.\n",
                   labels[index]);
      continue;
    }
    const SIZE actual = weasel::GetLanguageBarIconSize();
    Check(actual.cx == expected.cx && actual.cy == expected.cy,
          L"taskbar icon size must be independent of the host DPI context");
    std::wprintf(L"DPI %ls: host=%dx%d taskbar=%u result=%ldx%ld\n",
                 labels[index], GetSystemMetrics(SM_CXSMICON),
                 GetSystemMetrics(SM_CYSMICON), dpi, actual.cx, actual.cy);
    Check(set_context(previous) != nullptr, L"restore the previous DPI context");
  }
}

static void TestOwnership(HINSTANCE instance,
                          UINT resource,
                          const std::wstring& path,
                          SIZE size) {
  HICON first = weasel::LoadLanguageBarIcon(instance, resource, path, size);
  HICON second = weasel::LoadLanguageBarIcon(instance, resource, path, size);
  Check(first && second, L"resource and custom icons must load");
  Check(first != second, L"each returned icon must be independently owned");
  CheckSize(first, size);
  CheckSize(second, size);
  if (first)
    Check(DestroyIcon(first) != FALSE, L"the caller must be able to free its icon");
  if (second && first != second) {
    CheckSize(second, size);
    Check(DestroyIcon(second) != FALSE,
          L"destroying one icon must leave the other independently usable");
  }
}

static std::vector<DWORD> IconPixels(HICON icon) {
  ICONINFO info{};
  if (!icon || !GetIconInfo(icon, &info))
    return {};
  BITMAP bitmap{};
  std::vector<DWORD> pixels;
  if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap)) {
    BITMAPINFO dib{};
    dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = bitmap.bmWidth;
    dib.bmiHeader.biHeight = -bitmap.bmHeight;
    dib.bmiHeader.biPlanes = 1;
    dib.bmiHeader.biBitCount = 32;
    dib.bmiHeader.biCompression = BI_RGB;
    pixels.resize(static_cast<size_t>(bitmap.bmWidth) * bitmap.bmHeight);
    HDC dc = GetDC(nullptr);
    const int rows = dc ? GetDIBits(dc, info.hbmColor, 0, bitmap.bmHeight,
                                   pixels.data(), &dib, DIB_RGB_COLORS) : 0;
    if (dc)
      ReleaseDC(nullptr, dc);
    if (rows != bitmap.bmHeight)
      pixels.clear();
  }
  if (info.hbmColor)
    DeleteObject(info.hbmColor);
  if (info.hbmMask)
    DeleteObject(info.hbmMask);
  return pixels;
}

static std::vector<DWORD> RenderIcon(HICON icon, SIZE size, BYTE background) {
  BITMAPINFO dib{};
  dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  dib.bmiHeader.biWidth = size.cx;
  dib.bmiHeader.biHeight = -size.cy;
  dib.bmiHeader.biPlanes = 1;
  dib.bmiHeader.biBitCount = 32;
  dib.bmiHeader.biCompression = BI_RGB;
  DWORD* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(nullptr, &dib, DIB_RGB_COLORS,
      reinterpret_cast<void**>(&bits), nullptr, 0);
  HDC dc = CreateCompatibleDC(nullptr);
  std::vector<DWORD> pixels;
  if (bitmap && dc) {
    HGDIOBJ old = SelectObject(dc, bitmap);
    const size_t count = static_cast<size_t>(size.cx) * size.cy;
    for (size_t i = 0; i < count; ++i)
      bits[i] = static_cast<DWORD>(background) * 0x010101;
    if (DrawIconEx(dc, 0, 0, icon, size.cx, size.cy, 0, nullptr, DI_NORMAL)) {
      GdiFlush();
      pixels.assign(bits, bits + count);
    }
    SelectObject(dc, old);
  }
  if (dc)
    DeleteDC(dc);
  if (bitmap)
    DeleteObject(bitmap);
  return pixels;
}

static void TestThemePixels(HINSTANCE instance,
                            UINT resource,
                            const std::wstring& path,
                            SIZE size) {
  HICON original = weasel::LoadLanguageBarIcon(instance, resource, L"", size);
  const auto source = IconPixels(original);
  Check(!source.empty(), L"built-in icon pixels must be readable");
  for (const COLORREF color : {RGB(0, 0, 0), RGB(255, 255, 255), RGB(32, 128, 240)}) {
    HICON themed = weasel::RecolorLanguageBarIcon(original, color);
    Check(themed && themed != original,
          L"recoloring must return an independently owned icon");
    CheckSize(themed, size);
    const auto pixels = IconPixels(themed);
    Check(pixels.size() == source.size(), L"recoloring must preserve dimensions");
    bool alpha_preserved = true;
    bool color_correct = true;
    size_t visible = 0;
    size_t transparent = 0;
    for (size_t index = 0; index < pixels.size() && index < source.size(); ++index) {
      const DWORD alpha = source[index] >> 24;
      alpha_preserved &= (pixels[index] >> 24) == alpha;
      const DWORD rgb = pixels[index] & 0x00ffffff;
      const DWORD expected_rgb = alpha ?
          (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color) : 0u;
      color_correct &= rgb == expected_rgb;
      visible += alpha > 0;
      transparent += alpha == 0;
    }
    Check(alpha_preserved, L"theme changes must preserve every alpha value");
    Check(color_correct, L"glyph RGB must match its color with clear zero-alpha pixels");
    Check(visible && transparent,
          L"both themes must keep visible strokes and a clear background");
    const BYTE background = color == RGB(0, 0, 0) ? 243 : 32;
    const auto rendered = RenderIcon(themed, size, background);
    Check(rendered.size() == source.size(), L"theme icons must render with GDI");
    bool blend_correct = true;
    int max_error = 0;
    for (size_t index = 0; index < rendered.size() && index < source.size(); ++index) {
      const DWORD alpha = source[index] >> 24;
      for (int shift = 0; shift < 24; shift += 8) {
        const DWORD glyph = shift == 16 ? GetRValue(color) :
                            shift == 8 ? GetGValue(color) : GetBValue(color);
        const int expected = static_cast<int>(
            (glyph * alpha + background * (255 - alpha)) / 255);
        const int channel = (rendered[index] >> shift) & 255;
        blend_correct &= channel >= expected - 1 && channel <= expected + 1;
        const int delta = channel > expected ? channel - expected : expected - channel;
        if (delta > max_error)
          max_error = delta;
      }
    }
    Check(blend_correct,
          L"GDI composition must retain correct strokes and antialiased edges");
    if (!blend_correct)
      std::wprintf(L"Blend mismatch resource=%u size=%ld color=%06lx max_delta=%d\n",
                   resource, size.cx, color, max_error);
    if (themed)
      DestroyIcon(themed);
    Check(IconPixels(original) == source,
          L"recoloring and destroying the result must not alter the source");
  }
  if (original)
    DestroyIcon(original);

  HICON custom = weasel::LoadLanguageBarIcon(instance, resource, path, size);
  HICON themed_custom =
      weasel::LoadThemedLanguageBarIcon(instance, resource, path, size);
  Check(custom && themed_custom && custom != themed_custom,
        L"custom icons must remain independently owned");
  Check(IconPixels(custom) == IconPixels(themed_custom),
        L"a custom icon's color and alpha must remain unchanged");
  if (custom)
    DestroyIcon(custom);
  if (themed_custom)
    DestroyIcon(themed_custom);
}

// Redirect HKCU only inside this test process. No real theme setting or
// broadcast message is changed, and the temporary fixture is removed afterward.
static void TestThemeSelection(HINSTANCE instance) {
  constexpr const wchar_t* personalize =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
  const std::wstring name = L"Software\\WeaselIconTests\\" +
      std::to_wstring(GetCurrentProcessId()) + L"-" +
      std::to_wstring(GetTickCount64());
  HKEY original_root = nullptr;
  HKEY fixture = nullptr;
  HKEY settings = nullptr;
  Check(RegOpenKeyExW(HKEY_CURRENT_USER, L"", 0, KEY_ALL_ACCESS,
                      &original_root) == ERROR_SUCCESS,
        L"open the original user hive for fixture cleanup");
  if (!original_root)
    return;
  const LSTATUS created = RegCreateKeyExW(original_root, name.c_str(), 0, nullptr,
      REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr, &fixture, nullptr);
  Check(created == ERROR_SUCCESS, L"create the isolated theme fixture");
  if (created != ERROR_SUCCESS) {
    RegCloseKey(original_root);
    return;
  }
  const LSTATUS personalized = RegCreateKeyExW(fixture, personalize, 0, nullptr,
      REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr, &settings, nullptr);
  Check(personalized == ERROR_SUCCESS, L"create isolated personalization values");
  const bool redirected = personalized == ERROR_SUCCESS &&
      RegOverridePredefKey(HKEY_CURRENT_USER, fixture) == ERROR_SUCCESS;
  Check(redirected, L"redirect only this process's user hive");
  if (redirected) {
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    const bool high_contrast = SystemParametersInfoW(
        SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    for (const DWORD light : {0u, 1u, 0u}) {
      const DWORD app_light = !light;
      Check(RegSetValueExW(settings, L"SystemUsesLightTheme", 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&light),
                            sizeof(light)) == ERROR_SUCCESS &&
                RegSetValueExW(settings, L"AppsUseLightTheme", 0, REG_DWORD,
                                reinterpret_cast<const BYTE*>(&app_light),
                                sizeof(app_light)) == ERROR_SUCCESS,
            L"write isolated opposing Windows and application modes");
      const COLORREF expected = high_contrast ? GetSysColor(COLOR_WINDOWTEXT) :
          light ? RGB(0, 0, 0) : RGB(255, 255, 255);
      Check(weasel::GetLanguageBarIconColor() == expected,
            L"glyph color must follow Windows mode and refresh after a change");
      for (const UINT resource : {IDI_ZH, IDI_EN}) {
        HICON actual = weasel::LoadThemedLanguageBarIcon(
            instance, resource, L"", {32, 32});
        HICON source = weasel::LoadLanguageBarIcon(
            instance, resource, L"", {32, 32});
        HICON reference = weasel::RecolorLanguageBarIcon(source, expected);
        Check(actual && reference && IconPixels(actual) == IconPixels(reference),
              L"the production loader must apply the selected theme color");
        if (actual)
          DestroyIcon(actual);
        if (reference)
          DestroyIcon(reference);
        if (source)
          DestroyIcon(source);
      }
    }
    const COLORREF fallback = high_contrast ? GetSysColor(COLOR_WINDOWTEXT) :
        RGB(0, 0, 0);
    Check(RegDeleteValueW(settings, L"SystemUsesLightTheme") == ERROR_SUCCESS,
          L"remove the isolated Windows mode value");
    Check(weasel::GetLanguageBarIconColor() == fallback,
          L"an absent Windows mode must use the compatibility fallback");
    const wchar_t invalid[] = L"not a DWORD";
    Check(RegSetValueExW(settings, L"SystemUsesLightTheme", 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(invalid),
                          sizeof(invalid)) == ERROR_SUCCESS,
          L"write an invalid isolated Windows mode type");
    Check(weasel::GetLanguageBarIconColor() == fallback,
          L"an invalid Windows mode must use the compatibility fallback");
    Check(RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS,
          L"restore the original user hive mapping");
  }
  if (settings)
    RegCloseKey(settings);
  RegCloseKey(fixture);
  Check(RegDeleteTreeW(original_root, name.c_str()) == ERROR_SUCCESS,
        L"remove the isolated theme fixture");
  RegCloseKey(original_root);
  std::wprintf(L"Windows mode dark/light/dark, opposing app mode and fallback checked.\n");
}

static void WriteThemePreview(HINSTANCE instance, const wchar_t* path) {
  constexpr LONG width = 112;
  constexpr LONG height = 112;
  std::vector<DWORD> pixels(width * height);
  for (LONG y = 0; y < height; ++y)
    for (LONG x = 0; x < width; ++x)
      pixels[y * width + x] = y < height / 2 ? 0x00f3f3f3 : 0x00202020;
  const UINT resources[] = {IDI_ZH, IDI_EN};
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 2; ++column) {
      HICON source = weasel::LoadLanguageBarIcon(instance, resources[column],
                                                 L"", {32, 32});
      HICON themed = weasel::RecolorLanguageBarIcon(source,
          row == 0 ? RGB(0, 0, 0) : RGB(255, 255, 255));
      const auto rendered = RenderIcon(themed, {32, 32}, row == 0 ? 243 : 32);
      if (rendered.size() == 32 * 32) {
        for (LONG y = 0; y < 32; ++y)
          for (LONG x = 0; x < 32; ++x)
            pixels[(row * 56 + 12 + y) * width + column * 56 + 12 + x] =
                rendered[y * 32 + x];
      }
      if (themed)
        DestroyIcon(themed);
      if (source)
        DestroyIcon(source);
    }
  }
  BITMAPFILEHEADER file{};
  BITMAPINFOHEADER info{};
  file.bfType = 0x4d42;
  file.bfOffBits = sizeof(file) + sizeof(info);
  file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size() * sizeof(DWORD));
  info.biSize = sizeof(info);
  info.biWidth = width;
  info.biHeight = -height;
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;
  FILE* output = nullptr;
  Check(_wfopen_s(&output, path, L"wb") == 0,
        L"open native theme preview output");
  if (output) {
    const bool written = std::fwrite(&file, sizeof(file), 1, output) == 1 &&
        std::fwrite(&info, sizeof(info), 1, output) == 1 &&
        std::fwrite(pixels.data(), sizeof(DWORD), pixels.size(), output) == pixels.size();
    Check(std::fclose(output) == 0 && written, L"write native theme preview BMP");
  }
}

int wmain(int argc, wchar_t** argv) {
  if (argc != 3 && argc != 4) {
    std::fwprintf(stderr, L"Usage: TestLanguageBarIcon.exe zh.ico en.ico [preview.bmp]\n");
    return 2;
  }
  TestDpiContexts();
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  const UINT resources[] = {IDI_ZH, IDI_EN};
  const int dimensions[] = {16, 20, 24, 28, 32, 40, 48};
  for (int mode = 0; mode < 2; ++mode) {
    for (const int dimension : dimensions) {
      const SIZE size = {dimension, dimension};
      TestOwnership(instance, resources[mode], L"", size);
      TestOwnership(instance, resources[mode], argv[mode + 1], size);
      TestThemePixels(instance, resources[mode], argv[mode + 1], size);
    }
  }
  std::wprintf(L"Both modes: resource/file sizes 16,20,24,28,32,40,48 and ownership checked.\n");
  std::wprintf(L"Black/white glyph pixels, alpha, antialiasing and custom palette checked.\n");
  TestThemeSelection(instance);
  if (argc == 4)
    WriteThemePreview(instance, argv[3]);

  const SIZE size = {32, 32};
  HICON invalid = weasel::LoadLanguageBarIcon(
      instance, IDI_ZH, std::wstring(argv[1]) + L".does-not-exist", size);
  Check(invalid == nullptr, L"a missing custom icon must report failure");
  if (invalid)
    DestroyIcon(invalid);
  invalid = weasel::LoadLanguageBarIcon(instance, 65535, L"", size);
  Check(invalid == nullptr, L"a missing icon resource must report failure");
  if (invalid)
    DestroyIcon(invalid);

  // The earlier cases warm up system caches before measuring native handles.
  const HANDLE process = GetCurrentProcess();
  const DWORD user_before = GetGuiResources(process, GR_USEROBJECTS);
  const DWORD gdi_before = GetGuiResources(process, GR_GDIOBJECTS);
  for (int iteration = 0; iteration < 1000; ++iteration) {
    const int mode = iteration % 2;
    const int dimension = dimensions[iteration % 7];
    const SIZE requested = {dimension, dimension};
    HICON icon = weasel::LoadThemedLanguageBarIcon(
        instance, resources[mode], iteration % 4 < 2 ? L"" : argv[mode + 1],
        requested);
    Check(icon != nullptr, L"repeated loading must remain successful");
    CheckSize(icon, requested);
    if (icon)
      Check(DestroyIcon(icon) != FALSE, L"repeated loading must release each icon");
  }
  const DWORD user_after = GetGuiResources(process, GR_USEROBJECTS);
  const DWORD gdi_after = GetGuiResources(process, GR_GDIOBJECTS);
  Check(user_after == user_before, L"repeated requests must not leak USER handles");
  Check(gdi_after == gdi_before, L"repeated requests must not leak GDI handles");
  std::wprintf(L"1,000 requests: USER %lu->%lu, GDI %lu->%lu\n", user_before,
               user_after, gdi_before, gdi_after);
  std::wprintf(L"%ls (%d failures)\n", failures ? L"FAIL" : L"PASS", failures);
  return failures ? 1 : 0;
}
