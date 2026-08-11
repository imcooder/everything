#pragma once

// UI-thread message contract between the volume I/O threads (via ISearchSink /
// CUiSearchSink) and CMainFrame. Every ISearchSink callback fires on the matching
// volume's own I/O thread (see src/app/main.cpp CSampleSearchSink comment) — nothing
// here may touch a WTL/Win32 control directly. Every payload below is heap-allocated
// by the poster and freed by the UI-thread handler that receives it.

#include "core/platform.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

// wParam carries the SEARCH_REQUEST_ID for every WM_APP_SEARCH_* message so the UI-thread
// handler can drop stale messages that were already queued when a newer query started
// (belt-and-suspenders on top of CVolume's own stale/cancelled-id checks).
enum {
  WM_APP_SEARCH_BATCH = WM_APP + 1,     // lParam = ROW_BATCH* (one or more new matches)
  WM_APP_SEARCH_ADDED = WM_APP + 2,     // lParam = ROW_BATCH* with exactly one row
  WM_APP_SEARCH_REMOVED = WM_APP + 3,   // lParam = packed ROW_KEY (see PackRowKey)
  WM_APP_SEARCH_UPDATED = WM_APP + 4,   // lParam = ROW_BATCH* with exactly one row (rename-in-place refresh)
  WM_APP_SEARCH_SCAN_DONE = WM_APP + 5, // lParam = WCHAR drive letter whose initial scan finished
  WM_APP_SEARCH_COMPLETE = WM_APP + 6,  // lParam = bCancelled (BOOL)
  WM_APP_LOAD_STATUS = WM_APP + 7,      // lParam = std::wstring* status text (heap, UI thread frees)
  WM_APP_LOAD_DONE = WM_APP + 8,        // lParam = 0
};

struct ROW_DATA {
  WCHAR m_wchDrive = L'\0';
  UINT32 m_nodeId = 0;
  std::wstring m_wstrName;   // leaf filename, e.g. "delta-report.txt"
  std::wstring m_wstrFolder; // containing folder, e.g. "C:\Temp\evtest" (no trailing backslash)
  std::wstring m_wstrFullPath;
  // Display-only (Size/Type/Date Modified columns) — see INDEX_NODE_METADATA. Always
  // m_bIsDirectory == false/size == 0 fallback if the underlying node lookup fails.
  bool m_bIsDirectory = false;
  UINT64 m_ullFileSize = 0;
  UINT64 m_ullModifiedTime = 0; // FILETIME (100ns intervals since 1601-01-01), 0 if unknown
};

// Posted for OnBatch/OnAdded/OnUpdated. Heap-allocated by the sink (on the volume I/O
// thread), freed by CMainFrame's message handler (UI thread) after applying it.
struct ROW_BATCH {
  std::vector<ROW_DATA> m_rgRows;
};

using ROW_KEY = UINT64;

inline ROW_KEY PackRowKey(WCHAR wchDrive, UINT32 nodeId) {
  return (static_cast<ROW_KEY>(static_cast<UINT16>(wchDrive)) << 32) | static_cast<ROW_KEY>(nodeId);
}

} // namespace ui
