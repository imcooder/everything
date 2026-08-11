#include "app/ui/main_frame.h"

#include "app/ui/ui_search_sink.h"

#include <shellapi.h>

#include <cstdio>

namespace ui {

namespace {

constexpr int kEditHeight = 26;
constexpr int kMargin = 4;

} // namespace

CMainFrame::~CMainFrame() {
  // Belt-and-suspenders: OnDestroyMsg already does this on the normal close path, but
  // guard against a frame destroyed without going through WM_DESTROY (e.g. CreateEx
  // failure paths elsewhere) leaving a joinable thread behind.
  m_bShuttingDown.store(true, std::memory_order_release);
  if (m_startupThread.joinable()) {
    m_startupThread.join();
  }
}

LRESULT CMainFrame::OnCreate(LPCREATESTRUCT /*lpCreateStruct*/) {
  m_pActiveRequestId = std::make_shared<std::atomic<volume::SEARCH_REQUEST_ID>>(volume::SEARCH_REQUEST_ID_INVALID);

  SetWindowText(_T("Everything Clone"));

  RECT rcDummy = {0, 0, 0, 0};
  m_edit.Create(m_hWnd, rcDummy, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, IDC_EDIT_SEARCH);

  m_list.Create(m_hWnd, rcDummy, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE, IDC_LIST_RESULTS);
  m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);
  m_list.InsertColumn(0, _T("Name"), LVCFMT_LEFT, 320);
  m_list.InsertColumn(1, _T("Path"), LVCFMT_LEFT, 520);

  CreateSimpleStatusBar(_T("Starting..."));

  m_volumeManager.SetErrorCallback([this](DWORD dwError, LPCWSTR wszMessage) { OnVolumeError(dwError, wszMessage); });

  m_startupThread = std::thread(&CMainFrame::StartupWorker, this);

  return 0;
}

LRESULT CMainFrame::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL &bHandled) {
  // Stop the startup/load-poll thread first (it only touches m_volumeManager and posts
  // messages, never touches HWNDs), then cancel any active search and stop every volume
  // exactly like the console harness shutdown sequence (src/app/main.cpp): CancelSearchAll
  // -> StopAllAndWait. This runs synchronously on the UI thread during shutdown, which is
  // fine (the window is already going away) and guarantees no volume I/O thread is still
  // running by the time this HWND becomes invalid.
  m_bShuttingDown.store(true, std::memory_order_release);
  if (m_startupThread.joinable()) {
    m_startupThread.join();
  }

  if (m_pActiveRequestId != nullptr) {
    m_pActiveRequestId->store(volume::SEARCH_REQUEST_ID_INVALID, std::memory_order_release);
  }
  if (m_ullCurrentRequestId != volume::SEARCH_REQUEST_ID_INVALID) {
    m_volumeManager.CancelSearchAll(m_ullCurrentRequestId);
  }
  m_volumeManager.StopAllAndWait();

  ::PostQuitMessage(0);
  bHandled = TRUE;
  return 0;
}

void CMainFrame::OnSize(UINT /*nType*/, CSize /*size*/) {
  RECT rcClient = {};
  GetClientRect(&rcClient);

  int statusHeight = 0;
  if (m_hWndStatusBar != nullptr && ::IsWindow(m_hWndStatusBar)) {
    ::SendMessage(m_hWndStatusBar, WM_SIZE, 0, 0);
    RECT rcStatus = {};
    ::GetWindowRect(m_hWndStatusBar, &rcStatus);
    statusHeight = rcStatus.bottom - rcStatus.top;
  }

  if (m_edit.IsWindow()) {
    m_edit.SetWindowPos(nullptr, kMargin, kMargin, rcClient.right - 2 * kMargin, kEditHeight, SWP_NOZORDER);
  }

  if (m_list.IsWindow()) {
    const int listY = 2 * kMargin + kEditHeight;
    const int listHeight = rcClient.bottom - statusHeight - listY;
    m_list.SetWindowPos(nullptr, 0, listY, rcClient.right, listHeight > 0 ? listHeight : 0, SWP_NOZORDER);
  }
}

std::wstring CMainFrame::GetEditText() const {
  const int cch = m_edit.GetWindowTextLength();
  std::wstring wstrText(static_cast<size_t>(cch), L'\0');
  if (cch > 0) {
    m_edit.GetWindowText(wstrText.data(), cch + 1);
  }
  return wstrText;
}

LRESULT CMainFrame::OnSearchTextChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/) {
  if (m_bLoaded) {
    StartNewSearch(GetEditText());
  }
  return 0;
}

void CMainFrame::StartNewSearch(const std::wstring &wstrQuery) {
  if (m_ullCurrentRequestId != volume::SEARCH_REQUEST_ID_INVALID) {
    m_volumeManager.CancelSearchAll(m_ullCurrentRequestId);
  }

  const volume::SEARCH_REQUEST_ID ullNewId = m_ullNextRequestId++;
  m_pActiveRequestId->store(ullNewId, std::memory_order_release);
  m_ullCurrentRequestId = ullNewId;

  m_model.Clear();
  m_bSearchSettled = false;
  m_cVolumesScanPending = static_cast<UINT32>(m_volumeManager.GetDriveLetters().size());

  if (m_list.IsWindow()) {
    m_list.SetItemCount(0);
  }
  UpdateStatusBarCount();

  auto pSink = std::make_shared<CUiSearchSink>(m_hWnd, &m_volumeManager, m_pActiveRequestId);
  m_volumeManager.SearchAllAsync(ullNewId, wstrQuery.c_str(), pSink);
}

LRESULT CMainFrame::OnListGetDispInfo(int /*idCtrl*/, LPNMHDR pnmh, BOOL & /*bHandled*/) {
  NMLVDISPINFO *pDispInfo = reinterpret_cast<NMLVDISPINFO *>(pnmh);
  LVITEM &item = pDispInfo->item;

  const ROW_DATA *pRow = m_model.GetRow(static_cast<UINT32>(item.iItem));
  if (pRow == nullptr) {
    return 0;
  }

  if ((item.mask & LVIF_TEXT) != 0 && item.pszText != nullptr && item.cchTextMax > 0) {
    const std::wstring &wstrValue = (item.iSubItem == 0) ? pRow->m_wstrName : pRow->m_wstrFolder;
    lstrcpynW(item.pszText, wstrValue.c_str(), item.cchTextMax);
  }

  return 0;
}

void CMainFrame::OpenSelectedRow() {
  const int idx = m_list.GetNextItem(-1, LVNI_SELECTED);
  if (idx < 0) {
    return;
  }

  const ROW_DATA *pRow = m_model.GetRow(static_cast<UINT32>(idx));
  if (pRow == nullptr || pRow->m_wstrFullPath.empty()) {
    return;
  }

  // ShellExecute handles a missing/deleted target by returning an error code (<= 32);
  // beep instead of a blocking MessageBox so a stale row never stalls the app (UI5).
  const HINSTANCE hInst = ::ShellExecuteW(m_hWnd, L"open", pRow->m_wstrFullPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(hInst) <= 32) {
    ::MessageBeep(MB_ICONWARNING);
  }
}

void CMainFrame::OpenSelectedRowContainingFolder() {
  const int idx = m_list.GetNextItem(-1, LVNI_SELECTED);
  if (idx < 0) {
    return;
  }

  const ROW_DATA *pRow = m_model.GetRow(static_cast<UINT32>(idx));
  if (pRow == nullptr || pRow->m_wstrFullPath.empty()) {
    return;
  }

  // "/select," highlights the file itself in the opened Explorer window, matching
  // Everything's own "Open path" behavior, instead of just landing in the bare folder.
  const std::wstring wstrArgs = L"/select,\"" + pRow->m_wstrFullPath + L"\"";
  const HINSTANCE hInst = ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", wstrArgs.c_str(), nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(hInst) <= 32) {
    ::MessageBeep(MB_ICONWARNING);
  }
}

void CMainFrame::CopySelectedRowsToClipboard(bool bFullPath) {
  std::wstring wstrText;
  int idx = -1;
  while ((idx = m_list.GetNextItem(idx, LVNI_SELECTED)) != -1) {
    const ROW_DATA *pRow = m_model.GetRow(static_cast<UINT32>(idx));
    if (pRow == nullptr) {
      continue;
    }

    if (!wstrText.empty()) {
      wstrText += L"\r\n";
    }
    wstrText += bFullPath ? pRow->m_wstrFullPath : pRow->m_wstrName;
  }

  if (wstrText.empty() || !::OpenClipboard(m_hWnd)) {
    return;
  }

  ::EmptyClipboard();

  const size_t cbBuffer = (wstrText.size() + 1) * sizeof(WCHAR);
  HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, cbBuffer);
  if (hMem != nullptr) {
    void *pMem = ::GlobalLock(hMem);
    if (pMem != nullptr) {
      memcpy(pMem, wstrText.c_str(), cbBuffer);
      ::GlobalUnlock(hMem);
      // Clipboard owns hMem once SetClipboardData succeeds; do not free it ourselves.
      if (::SetClipboardData(CF_UNICODETEXT, hMem) == nullptr) {
        ::GlobalFree(hMem);
      }
    } else {
      ::GlobalFree(hMem);
    }
  }

  ::CloseClipboard();
}

LRESULT CMainFrame::OnListDblClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL & /*bHandled*/) {
  OpenSelectedRow();
  return 0;
}

LRESULT CMainFrame::OnListEnterKey(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL & /*bHandled*/) {
  OpenSelectedRow();
  return 0;
}

LRESULT CMainFrame::OnListColumnClick(int /*idCtrl*/, LPNMHDR pnmh, BOOL & /*bHandled*/) {
  const NMLISTVIEW *pInfo = reinterpret_cast<NMLISTVIEW *>(pnmh);
  const CResultModel::SORT_COLUMN column = (pInfo->iSubItem == 0) ? CResultModel::SORT_BY_NAME : CResultModel::SORT_BY_PATH;

  // Everything's own behavior: clicking the already-active sort column reverses direction;
  // clicking a different column starts that column fresh at ascending.
  if (column == m_sortColumn) {
    m_bSortAscending = !m_bSortAscending;
  } else {
    m_sortColumn = column;
    m_bSortAscending = true;
  }

  m_model.SortBy(m_sortColumn, m_bSortAscending);
  m_list.RedrawItems(0, m_list.GetItemCount() - 1);

  CHeaderCtrl header = m_list.GetHeader();
  for (int i = 0; i < 2; ++i) {
    HDITEM hdItem = {};
    hdItem.mask = HDI_FORMAT;
    header.GetItem(i, &hdItem);
    hdItem.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
    if (i == pInfo->iSubItem) {
      hdItem.fmt |= m_bSortAscending ? HDF_SORTUP : HDF_SORTDOWN;
    }
    header.SetItem(i, &hdItem);
  }

  return 0;
}

LRESULT CMainFrame::OnListRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL & /*bHandled*/) {
  if (m_list.GetSelectedCount() == 0) {
    return 0;
  }

  POINT ptScreen = {};
  ::GetCursorPos(&ptScreen);

  // Everything's own context-menu order: Open, Open path, then the two copy actions.
  HMENU hMenu = ::CreatePopupMenu();
  if (hMenu == nullptr) {
    return 0;
  }

  ::AppendMenuW(hMenu, MF_STRING, ID_CONTEXT_OPEN, L"Open");
  ::AppendMenuW(hMenu, MF_STRING, ID_CONTEXT_OPEN_PATH, L"Open path");
  ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
  ::AppendMenuW(hMenu, MF_STRING, ID_CONTEXT_COPY_PATH, L"Copy full path");
  ::AppendMenuW(hMenu, MF_STRING, ID_CONTEXT_COPY_NAME, L"Copy name");

  // SetForegroundWindow before/after TrackPopupMenu is the documented workaround for the
  // popup not dismissing on an outside click when the owner window wasn't already the
  // foreground window (MSDN TrackPopupMenu remarks).
  ::SetForegroundWindow(m_hWnd);
  // TPM_RETURNCMD + explicit PostMessage(WM_COMMAND) rather than TPM_NONOTIFY: lets
  // TrackPopupMenu's own modal loop finish and the menu fully close before the command
  // handler runs, matching how a normal menu click is dispatched.
  const int idCmd = ::TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, ptScreen.x, ptScreen.y, 0, m_hWnd, nullptr);
  ::PostMessage(m_hWnd, WM_NULL, 0, 0);
  ::DestroyMenu(hMenu);

  if (idCmd != 0) {
    ::PostMessage(m_hWnd, WM_COMMAND, MAKEWPARAM(idCmd, 0), 0);
  }

  return 0;
}

LRESULT CMainFrame::OnListKeyDown(int /*idCtrl*/, LPNMHDR pnmh, BOOL & /*bHandled*/) {
  const NMLVKEYDOWN *pInfo = reinterpret_cast<NMLVKEYDOWN *>(pnmh);
  const bool bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;

  if (bCtrl && pInfo->wVKey == 'C') {
    CopySelectedRowsToClipboard(true);
  } else if (bCtrl && pInfo->wVKey == 'A') {
    m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
  } else if (bCtrl && pInfo->wVKey == VK_RETURN) {
    OpenSelectedRowContainingFolder();
  }

  return 0;
}

LRESULT CMainFrame::OnContextOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/) {
  OpenSelectedRow();
  return 0;
}

LRESULT CMainFrame::OnContextOpenPath(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/) {
  OpenSelectedRowContainingFolder();
  return 0;
}

LRESULT CMainFrame::OnContextCopyFullPath(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/) {
  CopySelectedRowsToClipboard(true);
  return 0;
}

LRESULT CMainFrame::OnContextCopyName(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL & /*bHandled*/) {
  CopySelectedRowsToClipboard(false);
  return 0;
}

LRESULT CMainFrame::OnSearchBatch(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL & /*bHandled*/) {
  std::unique_ptr<ROW_BATCH> pBatch(reinterpret_cast<ROW_BATCH *>(lParam));
  const volume::SEARCH_REQUEST_ID ullRequestId = static_cast<volume::SEARCH_REQUEST_ID>(wParam);
  if (ullRequestId != m_ullCurrentRequestId) {
    return 0; // Stale: a newer query already started (belt-and-suspenders on top of CVolume's own checks).
  }

  bool bAnyAdded = false;
  for (ROW_DATA &row : pBatch->m_rgRows) {
    if (m_model.AddRowIfAbsent(std::move(row))) {
      bAnyAdded = true;
    }
  }

  if (bAnyAdded && m_list.IsWindow()) {
    m_list.SetItemCount(static_cast<int>(m_model.GetCount()));
  }
  UpdateStatusBarCount();
  return 0;
}

LRESULT CMainFrame::OnSearchAdded(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled) {
  return OnSearchBatch(uMsg, wParam, lParam, bHandled);
}

LRESULT CMainFrame::OnSearchUpdated(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL & /*bHandled*/) {
  std::unique_ptr<ROW_BATCH> pBatch(reinterpret_cast<ROW_BATCH *>(lParam));
  const volume::SEARCH_REQUEST_ID ullRequestId = static_cast<volume::SEARCH_REQUEST_ID>(wParam);
  if (ullRequestId != m_ullCurrentRequestId) {
    return 0;
  }

  bool bAnyUpdated = false;
  for (const ROW_DATA &row : pBatch->m_rgRows) {
    if (m_model.UpdateRowIfPresent(row)) {
      bAnyUpdated = true;
    }
  }

  if (bAnyUpdated && m_list.IsWindow()) {
    m_list.Invalidate(FALSE);
  }
  return 0;
}

LRESULT CMainFrame::OnSearchRemoved(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL & /*bHandled*/) {
  const volume::SEARCH_REQUEST_ID ullRequestId = static_cast<volume::SEARCH_REQUEST_ID>(wParam);
  if (ullRequestId != m_ullCurrentRequestId) {
    return 0;
  }

  const ROW_KEY key = static_cast<ROW_KEY>(lParam);
  if (m_model.RemoveRow(key) && m_list.IsWindow()) {
    m_list.SetItemCount(static_cast<int>(m_model.GetCount()));
  }
  UpdateStatusBarCount();
  return 0;
}

LRESULT CMainFrame::OnSearchScanDone(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL & /*bHandled*/) {
  const volume::SEARCH_REQUEST_ID ullRequestId = static_cast<volume::SEARCH_REQUEST_ID>(wParam);
  if (ullRequestId != m_ullCurrentRequestId) {
    return 0;
  }

  if (m_cVolumesScanPending > 0) {
    --m_cVolumesScanPending;
  }
  if (m_cVolumesScanPending == 0) {
    m_bSearchSettled = true;
  }
  UpdateStatusBarCount();
  return 0;
}

LRESULT CMainFrame::OnSearchComplete(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL & /*bHandled*/) {
  const volume::SEARCH_REQUEST_ID ullRequestId = static_cast<volume::SEARCH_REQUEST_ID>(wParam);
  if (ullRequestId == m_ullCurrentRequestId) {
    m_bSearchSettled = true;
    UpdateStatusBarCount();
  }
  return 0;
}

void CMainFrame::UpdateStatusBarCount() {
  if (m_hWndStatusBar == nullptr || !::IsWindow(m_hWndStatusBar) || !m_bLoaded) {
    return;
  }

  WCHAR wszText[128];
  swprintf_s(wszText, L"%u item(s)%ls", m_model.GetCount(), m_bSearchSettled ? L"" : L" (searching...)");
  ::SendMessage(m_hWndStatusBar, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(wszText));
}

void CMainFrame::PostStatus(const std::wstring &wstrText) const {
  auto pText = std::make_unique<std::wstring>(wstrText);
  if (::PostMessageW(m_hWnd, WM_APP_LOAD_STATUS, 0, reinterpret_cast<LPARAM>(pText.get())) != 0) {
    pText.release();
  }
}

LRESULT CMainFrame::OnLoadStatus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL & /*bHandled*/) {
  std::unique_ptr<std::wstring> pText(reinterpret_cast<std::wstring *>(lParam));
  if (m_hWndStatusBar != nullptr && ::IsWindow(m_hWndStatusBar) && !m_bLoaded) {
    ::SendMessage(m_hWndStatusBar, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(pText->c_str()));
  }
  return 0;
}

LRESULT CMainFrame::OnLoadDone(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL & /*bHandled*/) {
  m_bLoaded = true;
  StartNewSearch(GetEditText());
  return 0;
}

void CMainFrame::OnVolumeError(DWORD /*dwError*/, LPCWSTR wszMessage) {
  std::lock_guard<std::mutex> lock(m_mutexLastVolumeError);
  m_wstrLastVolumeError = wszMessage != nullptr ? wszMessage : L"";
}

void CMainFrame::StartupWorker() {
  PostStatus(L"Discovering volumes...");

  if (!m_volumeManager.RefreshVolumes()) {
    PostStatus(L"No NTFS fixed volumes found.");
    ::PostMessageW(m_hWnd, WM_APP_LOAD_DONE, 0, 0);
    return;
  }

  const std::vector<WCHAR> rgLetters = m_volumeManager.GetDriveLetters();
  if (rgLetters.empty()) {
    PostStatus(L"No NTFS fixed volumes found.");
    ::PostMessageW(m_hWnd, WM_APP_LOAD_DONE, 0, 0);
    return;
  }

  std::wstring wstrIndexing = L"Indexing ";
  for (size_t i = 0; i < rgLetters.size(); ++i) {
    if (i > 0) {
      wstrIndexing += L", ";
    }
    wstrIndexing.push_back(rgLetters[i]);
    wstrIndexing += L":";
  }
  wstrIndexing += L" ...";
  PostStatus(wstrIndexing);

  m_volumeManager.StartLoadAllAsync();

  std::set<WCHAR> setDone;
  std::set<WCHAR> setFailed;
  while (!m_bShuttingDown.load(std::memory_order_acquire)) {
    bool bAllDone = true;

    for (WCHAR wch : rgLetters) {
      volume::CVolume *pVolume = m_volumeManager.GetVolume(wch);
      if (pVolume == nullptr) {
        continue;
      }

      const volume::VOLUME_STATE state = pVolume->GetState();
      if (state == volume::VOLUME_STATE_ENUMERATING || state == volume::VOLUME_STATE_OPENING) {
        bAllDone = false;
      } else if (pVolume->IsReadyForSearch()) {
        setDone.insert(wch);
      } else if (state == volume::VOLUME_STATE_ERROR) {
        setDone.insert(wch); // Don't spin forever on a volume that failed to open.
        setFailed.insert(wch);
      } else {
        bAllDone = false;
      }
    }

    if (bAllDone && setDone.size() == rgLetters.size()) {
      break;
    }

    ::Sleep(200);
  }

  if (m_bShuttingDown.load(std::memory_order_acquire)) {
    return;
  }

  m_volumeManager.StartMonitorAllAsync();

  // A volume that failed to open (e.g. "Access is denied" opening \\.\C: when not running
  // elevated) previously left the status bar saying "Ready." with zero results and no
  // indication anything was wrong — a real, reproduced bug (repeated non-elevated launches
  // all showed an empty result list under a "Ready." status). Surface the failure instead.
  if (setFailed.empty()) {
    PostStatus(L"Ready.");
  } else {
    std::wstring wstrLastError;
    {
      std::lock_guard<std::mutex> lock(m_mutexLastVolumeError);
      wstrLastError = m_wstrLastVolumeError;
    }

    std::wstring wstrStatus = L"Ready - " + std::to_wstring(setFailed.size()) + L" of " + std::to_wstring(rgLetters.size()) + L" volume(s) failed to open";
    if (!wstrLastError.empty()) {
      wstrStatus += L" (" + wstrLastError + L")";
    }
    wstrStatus += setFailed.size() == rgLetters.size() ? L". Try running as Administrator." : L".";
    PostStatus(wstrStatus);
  }

  ::PostMessageW(m_hWnd, WM_APP_LOAD_DONE, 0, 0);
}

} // namespace ui
