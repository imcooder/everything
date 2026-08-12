#include "app/ui/atl_common.h"
#include "app/ui/main_frame.h"

#include <shellapi.h>

// Without an embedded manifest declaring this dependency, Windows loads the legacy,
// unthemed comctl32.dll (v5.82, the pre-XP "Windows Classic" look — flat gray buttons/edit
// boxes) even though InitCommonControlsEx() below still runs fine against it. This linker
// directive merges a manifest fragment into the exe requesting the side-by-side v6.0.0.0
// assembly instead, which is what actually turns on visual styles (themed controls matching
// the current Windows theme) — InitCommonControlsEx alone only initializes whichever version
// got loaded, it does not select one.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// Entry point. Linked with /ENTRY:mainCRTStartup + WIN32 subsystem (see src/CMakeLists.txt)
// so this behaves like a normal GUI app (no console window, per UI1) without relying on
// MSVC's WinMain/wWinMain entry-point auto-detection.
int main() {
  const HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

  ::CoInitialize(nullptr);

  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
  ::InitCommonControlsEx(&icc);

  HRESULT hRes = _Module.Init(nullptr, hInstance);
  hRes;
  ATLASSERT(SUCCEEDED(hRes));

  int nRet = 0;
  {
    WTL::CMessageLoop theLoop;
    _Module.AddMessageLoop(&theLoop);

    ui::CMainFrame wndMain;
    // CFrameWindowImpl::CreateEx() reads an m_uCommonResourceID field off CWndClassInfo
    // to auto-load a title/menu/accelerator from resources; that field no longer exists
    // in the ATL shipped with current VS2022 toolsets (WTL 10.0.10320 vs. newer ATL —
    // a known version-skew issue), so call the underlying Create() directly instead.
    // We have no menu/accelerator resources in this v1 UI anyway.
    if (wndMain.Create(nullptr, CWindow::rcDefault, L"Everything Clone", WS_OVERLAPPEDWINDOW, 0, 0U, nullptr) == nullptr) {
      ATLASSERT(FALSE);
    } else {
      wndMain.ShowWindow(SW_SHOWDEFAULT);
      wndMain.UpdateWindow();
      nRet = theLoop.Run();
    }

    _Module.RemoveMessageLoop();
  }

  _Module.Term();
  ::CoUninitialize();
  return nRet;
}
