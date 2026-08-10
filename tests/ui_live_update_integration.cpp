// In-process integration check for the "does the live-search UI list actually react to
// USN-driven change events" question (docs/ui-live-search-testcases.md UI7-UI12).
//
// This does NOT drive real NTFS/USN — that requires Administrator + an interactive desktop,
// unavailable in this environment (confirmed: no elevation path, no scheduled-task bypass,
// SeBackup/SeRestore/SeManageVolume privileges absent from the current token). Instead it
// exercises the exact production code path a real CUiSearchSink would drive: it constructs a
// real ui::CMainFrame, waits for startup to settle (real volumes on this machine still fail to
// open without admin — that's fine, this test doesn't need real search hits), then posts the
// same WM_APP_SEARCH_* messages (app_messages.h) that CUiSearchSink::OnAdded/OnUpdated/OnRemoved
// would post from a volume I/O thread, with fabricated ROW_DATA standing in for what a real USN
// record would have produced. It then reads the live CListViewCtrl back via LVM_GETITEMTEXT to
// prove the thread-marshaled message -> CResultModel -> LVN_GETDISPINFO chain actually updates
// what's on screen, not just "the code looks right."
//
// What this proves: the message contract and UI-side reaction are real and correct.
// What this does NOT prove: that CVolume/CUsnJournalMonitor actually detect real filesystem
// changes and call these same ISearchSink methods correctly end-to-end on a live NTFS volume —
// that needs a machine with admin + an interactive desktop, which this one is not.

#include "app/ui/app_messages.h"
#include "app/ui/atl_common.h"
#include "app/ui/main_frame.h"

#include <commctrl.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <thread>

CAppModule _Module;

namespace {

int g_cFail = 0;

void Check(bool bCondition, const char *pszWhat) {
  std::printf("[%s] %s\n", bCondition ? "PASS" : "FAIL", pszWhat);
  if (!bCondition) {
    ++g_cFail;
  }
}

void PumpMessagesFor(std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  MSG msg;
  while (std::chrono::steady_clock::now() < deadline) {
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }
    ::Sleep(5);
  }
}

bool WaitForCondition(const std::function<bool()> &fnCondition, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  MSG msg;
  while (std::chrono::steady_clock::now() < deadline) {
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }
    if (fnCondition()) {
      return true;
    }
    ::Sleep(20);
  }
  return fnCondition();
}

std::wstring GetListItemName(HWND hList, int idx) {
  WCHAR wszBuf[512] = {};
  LVITEMW item = {};
  item.iSubItem = 0;
  item.pszText = wszBuf;
  item.cchTextMax = 512;
  ::SendMessageW(hList, LVM_GETITEMTEXTW, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(&item));
  return wszBuf;
}

int GetListItemCount(HWND hList) {
  return static_cast<int>(::SendMessageW(hList, LVM_GETITEMCOUNT, 0, 0));
}

} // namespace

int main() {
  ::CoInitialize(nullptr);

  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
  ::InitCommonControlsEx(&icc);

  const HRESULT hRes = _Module.Init(nullptr, ::GetModuleHandleW(nullptr));
  Check(SUCCEEDED(hRes), "ATL module initialized");

  ui::CMainFrame wndMain;
  const HWND hWndMain = wndMain.Create(nullptr, CWindow::rcDefault, L"EverythingUILiveUpdateTest", WS_OVERLAPPEDWINDOW, 0, 0U, nullptr);
  Check(hWndMain != nullptr, "CMainFrame::Create succeeded (headless, never shown)");
  if (hWndMain == nullptr) {
    return 1;
  }

  std::printf("Waiting for startup to settle (StartupWorker + the automatic post-load search)...\n");
  const bool bStartupSettled = WaitForCondition([&]() { return wndMain.GetCurrentRequestIdForTesting() != volume::SEARCH_REQUEST_ID_INVALID; }, std::chrono::seconds(30));
  Check(bStartupSettled, "startup settled and an initial search request id was assigned");

  const volume::SEARCH_REQUEST_ID ullActiveRequestId = wndMain.GetCurrentRequestIdForTesting();
  std::printf("Active search request id: %llu\n", static_cast<unsigned long long>(ullActiveRequestId));

  const HWND hList = ::GetDlgItem(hWndMain, ui::CMainFrame::IDC_LIST_RESULTS);
  Check(hList != nullptr, "found the results ListView child control");
  if (hList == nullptr) {
    wndMain.SendMessage(WM_CLOSE);
    PumpMessagesFor(std::chrono::milliseconds(2000));
    return 1;
  }

  const int cBaseline = GetListItemCount(hList);
  std::printf("Baseline row count (real volumes fail to open without admin, expected small/zero): %d\n", cBaseline);

  // --- Simulate ISearchSink::OnAdded (a file create/rename-into-match observed via USN) ---
  {
    auto pBatch = std::make_unique<ui::ROW_BATCH>();
    ui::ROW_DATA row;
    row.m_wchDrive = L'Z';
    row.m_nodeId = 111;
    row.m_wstrName = L"synthetic-delta-report.txt";
    row.m_wstrFolder = L"Z:\\fake\\path";
    row.m_wstrFullPath = L"Z:\\fake\\path\\synthetic-delta-report.txt";
    pBatch->m_rgRows.push_back(row);

    ui::ROW_BATCH *pRaw = pBatch.release();
    if (::PostMessageW(hWndMain, ui::WM_APP_SEARCH_ADDED, static_cast<WPARAM>(ullActiveRequestId), reinterpret_cast<LPARAM>(pRaw)) == 0) {
      delete pRaw;
    }

    const bool bAdded = WaitForCondition([&]() { return GetListItemCount(hList) == cBaseline + 1; }, std::chrono::seconds(3));
    Check(bAdded, "OnAdded (simulated external file create) added exactly one row to the live list");
    Check(GetListItemName(hList, cBaseline) == L"synthetic-delta-report.txt", "new row displays the correct Name column via LVN_GETDISPINFO");
  }

  // --- Simulate ISearchSink::OnUpdated (rename-in-place: still matches, membership unchanged) ---
  {
    auto pBatch = std::make_unique<ui::ROW_BATCH>();
    ui::ROW_DATA row;
    row.m_wchDrive = L'Z';
    row.m_nodeId = 111;
    row.m_wstrName = L"synthetic-delta-renamed.txt";
    row.m_wstrFolder = L"Z:\\fake\\path";
    row.m_wstrFullPath = L"Z:\\fake\\path\\synthetic-delta-renamed.txt";
    pBatch->m_rgRows.push_back(row);

    ui::ROW_BATCH *pRaw = pBatch.release();
    if (::PostMessageW(hWndMain, ui::WM_APP_SEARCH_UPDATED, static_cast<WPARAM>(ullActiveRequestId), reinterpret_cast<LPARAM>(pRaw)) == 0) {
      delete pRaw;
    }

    const bool bRenamed = WaitForCondition([&]() { return GetListItemName(hList, cBaseline) == L"synthetic-delta-renamed.txt"; }, std::chrono::seconds(3));
    Check(bRenamed, "OnUpdated (simulated external rename-in-place) refreshed the displayed Name");
    Check(GetListItemCount(hList) == cBaseline + 1, "row count unchanged after rename-in-place (no duplicate/leaked row, UI12)");
  }

  // --- Simulate ISearchSink::OnRemoved (a file delete/rename-out-of-match observed via USN) ---
  {
    const ui::ROW_KEY key = ui::PackRowKey(L'Z', 111);
    ::PostMessageW(hWndMain, ui::WM_APP_SEARCH_REMOVED, static_cast<WPARAM>(ullActiveRequestId), static_cast<LPARAM>(key));

    const bool bRemoved = WaitForCondition([&]() { return GetListItemCount(hList) == cBaseline; }, std::chrono::seconds(3));
    Check(bRemoved, "OnRemoved (simulated external file delete) removed the row from the live list");
  }

  // --- Stale request id must be dropped (query switched mid-flight, belt-and-suspenders check) ---
  {
    auto pBatch = std::make_unique<ui::ROW_BATCH>();
    ui::ROW_DATA row;
    row.m_wchDrive = L'Z';
    row.m_nodeId = 222;
    row.m_wstrName = L"should-be-ignored.txt";
    pBatch->m_rgRows.push_back(row);

    const int cBeforeStale = GetListItemCount(hList);
    ui::ROW_BATCH *pRaw = pBatch.release();
    if (::PostMessageW(hWndMain, ui::WM_APP_SEARCH_ADDED, static_cast<WPARAM>(ullActiveRequestId + 999), reinterpret_cast<LPARAM>(pRaw)) == 0) {
      delete pRaw;
    }

    PumpMessagesFor(std::chrono::milliseconds(500));
    Check(GetListItemCount(hList) == cBeforeStale, "message tagged with a stale/foreign request id is ignored (no row added)");
  }

  wndMain.SendMessage(WM_CLOSE);
  PumpMessagesFor(std::chrono::milliseconds(3000));

  std::printf("\n%d check(s) failed.\n", g_cFail);

  _Module.Term();
  ::CoUninitialize();
  return g_cFail == 0 ? 0 : 1;
}
