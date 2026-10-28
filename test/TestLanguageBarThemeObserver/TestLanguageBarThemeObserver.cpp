#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0603
#define WINVER 0x0603

#include "../../WeaselTSF/LanguageBarThemeObserver.h"
#include <cwchar>

static int failures = 0;

static void Check(bool condition, const wchar_t* message) {
  if (!condition) {
    std::fwprintf(stderr, L"FAIL: %ls (error=%lu)\n", message, GetLastError());
    ++failures;
  }
}

static void Count(void* context) {
  ++*static_cast<int*>(context);
}

struct ReentrantContext {
  weasel::LanguageBarThemeObserver* observer;
  int count;
};

static void DeleteDuringCallback(void* context) {
  auto* state = static_cast<ReentrantContext*>(context);
  ++state->count;
  delete state->observer;
  state->observer = nullptr;
}

static void CheckClassRemoved(HINSTANCE instance, const wchar_t* name) {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  Check(!GetClassInfoExW(instance, name, &window_class),
        L"the window class must not retain a DLL procedure after destruction");
}

int wmain() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  weasel::LanguageBarThemeObserver first;
  weasel::LanguageBarThemeObserver second;
  int first_count = 0;
  int second_count = 0;
  Check(first.Start(instance, Count, &first_count), L"create the first observer");
  Check(second.Start(instance, Count, &second_count), L"create the second observer");
  const HWND first_window = first.window();
  const HWND second_window = second.window();
  Check(first_window && second_window && first_window != second_window,
        L"multiple language bar instances must have independent observers");
  Check(GetParent(first_window) == nullptr &&
            GetAncestor(first_window, GA_ROOT) == first_window &&
            (GetWindowLongPtrW(first_window, GWL_STYLE) & WS_POPUP) != 0,
        L"theme broadcasts require a top-level window");
  Check(!IsWindowVisible(first_window) &&
            (GetWindowLongPtrW(first_window, GWL_EXSTYLE) & WS_EX_NOACTIVATE),
        L"the observer must stay hidden and never take input focus");

  const UINT notifications[] = {WM_SETTINGCHANGE, WM_THEMECHANGED,
                                 WM_SYSCOLORCHANGE, WM_DISPLAYCHANGE};
  for (UINT message : notifications)
    SendMessageW(first_window, message, 0, 0);
  SendMessageW(first_window, WM_NULL, 0, 0);
  Check(first_count == 4 && second_count == 0,
        L"appearance messages notify only the matching owner");

  wchar_t first_class[80]{};
  GetClassNameW(first_window, first_class, _countof(first_class));
  first.Stop();
  Check(!IsWindow(first_window), L"Stop must destroy the hidden window");
  CheckClassRemoved(instance, first_class);
  SendMessageW(second_window, WM_THEMECHANGED, 0, 0);
  Check(second_count == 1, L"stopping one observer must leave the other active");
  first.Stop();
  Check(first.Start(instance, Count, &first_count), L"an observer can be restarted");
  first.Stop();
  second.Stop();

  ReentrantContext reentrant{new weasel::LanguageBarThemeObserver, 0};
  Check(reentrant.observer->Start(instance, DeleteDuringCallback, &reentrant),
        L"create the reentrant observer");
  const HWND reentrant_window = reentrant.observer->window();
  wchar_t reentrant_class[80]{};
  GetClassNameW(reentrant_window, reentrant_class, _countof(reentrant_class));
  SendMessageW(reentrant_window, WM_THEMECHANGED, 0, 0);
  Check(reentrant.count == 1 && !reentrant.observer &&
            !IsWindow(reentrant_window),
        L"the owner may safely destroy its observer during a callback");
  CheckClassRemoved(instance, reentrant_class);

  const DWORD user_before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  const DWORD gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  for (int iteration = 0; iteration < 1000; ++iteration) {
    weasel::LanguageBarThemeObserver observer;
    Check(observer.Start(instance, Count, &first_count),
          L"repeated construction must succeed");
  }
  const DWORD user_after = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  const DWORD gdi_after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  Check(user_before == user_after && gdi_before == gdi_after,
        L"repeated observers must not leak USER or GDI handles");
  std::wprintf(L"Theme notifications, independent owners, reentrant deletion and "
                L"class cleanup checked; 1,000 instances USER %lu->%lu GDI %lu->%lu\n",
                user_before, user_after, gdi_before, gdi_after);
  std::wprintf(L"%ls (%d failures)\n", failures ? L"FAIL" : L"PASS", failures);
  return failures ? 1 : 0;
}
