#include "app/ui/result_model.h"

#include <algorithm>

namespace ui {

namespace {

int CompareIgnoreCase(const std::wstring &wstrLhs, const std::wstring &wstrRhs) {
  return _wcsicmp(wstrLhs.c_str(), wstrRhs.c_str());
}

// Extension substring (no leading dot), e.g. "txt" for "report.txt" and "" for "archive.tar.gz"
// wait — matches only the LAST dot, so "archive.tar.gz" yields "gz"; a dotfile like ".gitignore"
// (dot at index 0, not a real separator) yields "". Used as a fast, deterministic proxy for
// Type-column sorting — the real display string (e.g. "Text Document") comes from
// SHGetFileInfo in CMainFrame::OnListGetDispInfo, which is too costly to call for every
// comparison in an O(n log n) sort, but correlates 1:1 with extension for the vast majority
// of real files, so sorting by extension groups the same way sorting by type name would.
std::wstring GetExtensionForSort(const std::wstring &wstrName) {
  const size_t pos = wstrName.find_last_of(L'.');
  if (pos == std::wstring::npos || pos == 0) {
    return std::wstring();
  }
  return wstrName.substr(pos + 1);
}

int CompareByColumn(CResultModel::SORT_COLUMN column, const ROW_DATA &lhs, const ROW_DATA &rhs) {
  switch (column) {
  case CResultModel::SORT_BY_PATH:
    return CompareIgnoreCase(lhs.m_wstrFullPath, rhs.m_wstrFullPath);
  case CResultModel::SORT_BY_SIZE:
    return (lhs.m_ullFileSize < rhs.m_ullFileSize) ? -1 : (lhs.m_ullFileSize > rhs.m_ullFileSize) ? 1 : 0;
  case CResultModel::SORT_BY_TYPE: {
    // Directories sort together, before any extension (matches Explorer/Everything grouping
    // folders ahead of files within an ascending Type sort).
    if (lhs.m_bIsDirectory != rhs.m_bIsDirectory) {
      return lhs.m_bIsDirectory ? -1 : 1;
    }
    return CompareIgnoreCase(GetExtensionForSort(lhs.m_wstrName), GetExtensionForSort(rhs.m_wstrName));
  }
  case CResultModel::SORT_BY_DATE_MODIFIED:
    return (lhs.m_ullModifiedTime < rhs.m_ullModifiedTime) ? -1 : (lhs.m_ullModifiedTime > rhs.m_ullModifiedTime) ? 1 : 0;
  case CResultModel::SORT_BY_NAME:
  default:
    return CompareIgnoreCase(lhs.m_wstrName, rhs.m_wstrName);
  }
}

} // namespace

void CResultModel::Clear() {
  m_rgRows.clear();
  m_mapKeyToIndex.clear();
}

bool CResultModel::AddRowIfAbsent(ROW_DATA row) {
  const ROW_KEY key = PackRowKey(row.m_wchDrive, row.m_nodeId);
  if (m_mapKeyToIndex.find(key) != m_mapKeyToIndex.end()) {
    return false;
  }

  const UINT32 idx = static_cast<UINT32>(m_rgRows.size());
  m_rgRows.push_back(std::move(row));
  m_mapKeyToIndex.emplace(key, idx);
  return true;
}

bool CResultModel::UpdateRowIfPresent(const ROW_DATA &row) {
  const ROW_KEY key = PackRowKey(row.m_wchDrive, row.m_nodeId);
  const auto it = m_mapKeyToIndex.find(key);
  if (it == m_mapKeyToIndex.end()) {
    return false;
  }

  ROW_DATA &existing = m_rgRows[it->second];
  existing.m_wstrName = row.m_wstrName;
  existing.m_wstrFolder = row.m_wstrFolder;
  existing.m_wstrFullPath = row.m_wstrFullPath;
  // OnUpdated also fires for a content/attribute change (not just a rename) that doesn't move
  // the row into or out of the current result set, so metadata needs refreshing here too.
  existing.m_bIsDirectory = row.m_bIsDirectory;
  existing.m_ullFileSize = row.m_ullFileSize;
  existing.m_ullModifiedTime = row.m_ullModifiedTime;
  return true;
}

bool CResultModel::RemoveRow(ROW_KEY key) {
  const auto it = m_mapKeyToIndex.find(key);
  if (it == m_mapKeyToIndex.end()) {
    return false;
  }

  const UINT32 idx = it->second;
  const UINT32 last = static_cast<UINT32>(m_rgRows.size()) - 1;

  if (idx != last) {
    m_rgRows[idx] = std::move(m_rgRows[last]);
    const ROW_KEY movedKey = PackRowKey(m_rgRows[idx].m_wchDrive, m_rgRows[idx].m_nodeId);
    m_mapKeyToIndex[movedKey] = idx;
  }

  m_rgRows.pop_back();
  m_mapKeyToIndex.erase(it);
  return true;
}

void CResultModel::SortBy(SORT_COLUMN column, bool bAscending) {
  std::stable_sort(m_rgRows.begin(), m_rgRows.end(), [column, bAscending](const ROW_DATA &lhs, const ROW_DATA &rhs) {
    const int cmp = CompareByColumn(column, lhs, rhs);
    return bAscending ? cmp < 0 : cmp > 0;
  });

  m_mapKeyToIndex.clear();
  m_mapKeyToIndex.reserve(m_rgRows.size());
  for (UINT32 idx = 0; idx < m_rgRows.size(); ++idx) {
    m_mapKeyToIndex.emplace(PackRowKey(m_rgRows[idx].m_wchDrive, m_rgRows[idx].m_nodeId), idx);
  }
}

} // namespace ui
