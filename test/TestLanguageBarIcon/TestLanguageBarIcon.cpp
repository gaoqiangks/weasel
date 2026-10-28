#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0603
#define WINVER 0x0603

#include "../../WeaselTSF/LanguageBarIcon.h"
#include "../../include/resource.h"
#include <cwchar>
#include <string>

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

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) {
    std::fwprintf(stderr, L"Usage: TestLanguageBarIcon.exe zh.ico en.ico\n");
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
    }
  }
  std::wprintf(L"Both modes: resource/file sizes 16,20,24,28,32,40,48 and ownership checked.\n");

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
    HICON icon = weasel::LoadLanguageBarIcon(
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
