#pragma once

// Main application window: search box (top) + virtual-mode result ListView (middle) +
// status bar (bottom). See README §6 (UI milestone scope) and
// docs/ui-live-search-testcases.md (acceptance cases UI1-UI20).
//
// Threading: this class and everything it touches directly (m_edit, m_list,
// m_hWndStatusBar, m_model) live on the UI thread ONLY. Search results arrive from
// volume I/O threads via ui::CUiSearchSink, which PostMessage()s WM_APP_SEARCH_* here
// (see app_messages.h) instead of calling into these members directly.

#include "app/ui/app_messages.h"
#include "app/ui/atl_common.h"
#include "app/ui/result_model.h"
#include "volume/volume_manager.h"

#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>

namespace ui {

class CMainFrame : public CFrameWindowImpl<CMainFrame> {
public:
  DECLARE_WND_CLASS_EX(_T("Everything.Clone.MainFrame"), CS_HREDRAW | CS_VREDRAW, COLOR_WINDOW)

  // clang-format off
  BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_CREATE(OnCreate)
    MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
    MSG_WM_SIZE(OnSize)
    MESSAGE_HANDLER(WM_APP_SEARCH_BATCH, OnSearchBatch)
    MESSAGE_HANDLER(WM_APP_SEARCH_ADDED, OnSearchAdded)
    MESSAGE_HANDLER(WM_APP_SEARCH_REMOVED, OnSearchRemoved)
    MESSAGE_HANDLER(WM_APP_SEARCH_UPDATED, OnSearchUpdated)
    MESSAGE_HANDLER(WM_APP_SEARCH_SCAN_DONE, OnSearchScanDone)
    MESSAGE_HANDLER(WM_APP_SEARCH_COMPLETE, OnSearchComplete)
    MESSAGE_HANDLER(WM_APP_LOAD_STATUS, OnLoadStatus)
    MESSAGE_HANDLER(WM_APP_LOAD_DONE, OnLoadDone)
    COMMAND_HANDLER(IDC_EDIT_SEARCH, EN_CHANGE, OnSearchTextChanged)
    NOTIFY_HANDLER(IDC_LIST_RESULTS, LVN_GETDISPINFO, OnListGetDispInfo)
    NOTIFY_HANDLER(IDC_LIST_RESULTS, NM_DBLCLK, OnListDblClick)
    NOTIFY_HANDLER(IDC_LIST_RESULTS, NM_RETURN, OnListEnterKey)
  END_MSG_MAP()
  // clang-format on

  enum { IDC_EDIT_SEARCH = 1001, IDC_LIST_RESULTS = 1002 };

  ~CMainFrame();

private:
  LRESULT OnCreate(LPCREATESTRUCT lpCreateStruct);
  LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
  void OnSize(UINT nType, CSize size);

  LRESULT OnSearchTextChanged(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL &bHandled);
  LRESULT OnListGetDispInfo(int idCtrl, LPNMHDR pnmh, BOOL &bHandled);
  LRESULT OnListDblClick(int idCtrl, LPNMHDR pnmh, BOOL &bHandled);
  LRESULT OnListEnterKey(int idCtrl, LPNMHDR pnmh, BOOL &bHandled);

  LRESULT OnSearchBatch(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
  LRESULT OnSearchAdded(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
  LRESULT OnSearchRemoved(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
  LRESULT OnSearchUpdated(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
  LRESULT OnSearchScanDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
  LRESULT OnSearchComplete(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
  LRESULT OnLoadStatus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);
  LRESULT OnLoadDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled);

  void StartNewSearch(const std::wstring &wstrQuery);
  void OpenSelectedRow();
  void UpdateStatusBarCount();
  void PostStatus(const std::wstring &wstrText) const;
  void StartupWorker();
  std::wstring GetEditText() const;

  CEdit m_edit;
  CListViewCtrl m_list;

  volume::CVolumeManager m_volumeManager;
  CResultModel m_model;

  // Shared with every CUiSearchSink instance so a sink whose request has been superseded
  // can bail out (IsCancelled) even if a stale message is already queued for the UI
  // thread. Kept alive via shared_ptr so it outlives any in-flight sink safely.
  std::shared_ptr<std::atomic<volume::SEARCH_REQUEST_ID>> m_pActiveRequestId;
  volume::SEARCH_REQUEST_ID m_ullNextRequestId = 1;
  volume::SEARCH_REQUEST_ID m_ullCurrentRequestId = volume::SEARCH_REQUEST_ID_INVALID;
  UINT32 m_cVolumesScanPending = 0;
  bool m_bSearchSettled = true;

  std::thread m_startupThread;
  std::atomic<bool> m_bShuttingDown{false};
  bool m_bLoaded = false;
};

} // namespace ui
