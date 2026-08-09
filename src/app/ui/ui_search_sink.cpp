#include "app/ui/ui_search_sink.h"

#include <memory>

namespace ui {

namespace {

std::wstring Utf8ToWide(const char *pszUtf8) {
  if (pszUtf8 == nullptr || pszUtf8[0] == '\0') {
    return std::wstring();
  }

  const int cchNeeded = MultiByteToWideChar(CP_UTF8, 0, pszUtf8, -1, nullptr, 0);
  if (cchNeeded <= 1) {
    return std::wstring();
  }

  std::wstring wstrResult(static_cast<size_t>(cchNeeded - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, pszUtf8, -1, wstrResult.data(), cchNeeded);
  return wstrResult;
}

} // namespace

CUiSearchSink::CUiSearchSink(HWND hWndTarget, volume::CVolumeManager *pManager, std::shared_ptr<std::atomic<volume::SEARCH_REQUEST_ID>> pActiveRequestId)
    : m_hWndTarget(hWndTarget), m_pManager(pManager), m_pActiveRequestId(std::move(pActiveRequestId)) {}

bool CUiSearchSink::IsCancelled(volume::SEARCH_REQUEST_ID ullRequestId) const {
  if (m_pActiveRequestId == nullptr) {
    return true;
  }
  return m_pActiveRequestId->load(std::memory_order_acquire) != ullRequestId;
}

bool CUiSearchSink::BuildRow(WCHAR wchDrive, UINT32 nodeId, ROW_DATA &outRow) const {
  if (m_pManager == nullptr) {
    return false;
  }

  volume::CVolume *pVolume = m_pManager->GetVolume(wchDrive);
  if (pVolume == nullptr) {
    return false;
  }

  // Safe: BuildRow is only ever invoked from within a sink callback, which the volume
  // coordinator guarantees runs on this same volume's I/O thread.
  std::vector<char> rgPathUtf8;
  if (!pVolume->MaterializeFullPathUtf8(nodeId, rgPathUtf8)) {
    return false;
  }
  rgPathUtf8.push_back('\0');

  const std::wstring wstrFull = Utf8ToWide(rgPathUtf8.data());
  if (wstrFull.empty()) {
    return false;
  }

  outRow.m_wchDrive = wchDrive;
  outRow.m_nodeId = nodeId;
  outRow.m_wstrFullPath = wstrFull;

  const size_t pos = wstrFull.find_last_of(L'\\');
  if (pos == std::wstring::npos) {
    outRow.m_wstrName = wstrFull;
    outRow.m_wstrFolder.clear();
  } else {
    outRow.m_wstrName = wstrFull.substr(pos + 1);
    outRow.m_wstrFolder = wstrFull.substr(0, pos);
  }

  return true;
}

void CUiSearchSink::PostSingleRow(int message, volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDrive, UINT32 nodeId) const {
  auto pBatch = std::make_unique<ROW_BATCH>();
  ROW_DATA row;
  if (!BuildRow(wchDrive, nodeId, row)) {
    return;
  }
  pBatch->m_rgRows.push_back(std::move(row));

  if (::PostMessageW(m_hWndTarget, static_cast<UINT>(message), static_cast<WPARAM>(ullRequestId), reinterpret_cast<LPARAM>(pBatch.get())) != 0) {
    pBatch.release();
  }
}

void CUiSearchSink::OnBatch(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, const UINT32 *rgNodeIds, UINT32 cNodeIds) {
  if (rgNodeIds == nullptr || cNodeIds == 0) {
    return;
  }

  auto pBatch = std::make_unique<ROW_BATCH>();
  pBatch->m_rgRows.reserve(cNodeIds);

  for (UINT32 i = 0; i < cNodeIds; ++i) {
    ROW_DATA row;
    if (BuildRow(wchDriveLetter, rgNodeIds[i], row)) {
      pBatch->m_rgRows.push_back(std::move(row));
    }
  }

  if (pBatch->m_rgRows.empty()) {
    return;
  }

  if (::PostMessageW(m_hWndTarget, WM_APP_SEARCH_BATCH, static_cast<WPARAM>(ullRequestId), reinterpret_cast<LPARAM>(pBatch.get())) != 0) {
    pBatch.release();
  }
}

void CUiSearchSink::OnInitialScanComplete(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter) {
  ::PostMessageW(m_hWndTarget, WM_APP_SEARCH_SCAN_DONE, static_cast<WPARAM>(ullRequestId), static_cast<LPARAM>(wchDriveLetter));
}

void CUiSearchSink::OnAdded(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) {
  PostSingleRow(WM_APP_SEARCH_ADDED, ullRequestId, wchDriveLetter, nodeId);
}

void CUiSearchSink::OnRemoved(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) {
  const ROW_KEY key = PackRowKey(wchDriveLetter, nodeId);
  ::PostMessageW(m_hWndTarget, WM_APP_SEARCH_REMOVED, static_cast<WPARAM>(ullRequestId), static_cast<LPARAM>(key));
}

void CUiSearchSink::OnUpdated(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) {
  PostSingleRow(WM_APP_SEARCH_UPDATED, ullRequestId, wchDriveLetter, nodeId);
}

void CUiSearchSink::OnComplete(volume::SEARCH_REQUEST_ID ullRequestId, bool bCancelled) {
  ::PostMessageW(m_hWndTarget, WM_APP_SEARCH_COMPLETE, static_cast<WPARAM>(ullRequestId), static_cast<LPARAM>(bCancelled ? 1 : 0));
}

} // namespace ui
