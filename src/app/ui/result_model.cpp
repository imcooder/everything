#include "app/ui/result_model.h"

#include <algorithm>

namespace ui {

namespace {

int CompareIgnoreCase(const std::wstring &wstrLhs, const std::wstring &wstrRhs) {
  return _wcsicmp(wstrLhs.c_str(), wstrRhs.c_str());
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
    const int cmp = (column == SORT_BY_NAME) ? CompareIgnoreCase(lhs.m_wstrName, rhs.m_wstrName) : CompareIgnoreCase(lhs.m_wstrFullPath, rhs.m_wstrFullPath);
    return bAscending ? cmp < 0 : cmp > 0;
  });

  m_mapKeyToIndex.clear();
  m_mapKeyToIndex.reserve(m_rgRows.size());
  for (UINT32 idx = 0; idx < m_rgRows.size(); ++idx) {
    m_mapKeyToIndex.emplace(PackRowKey(m_rgRows[idx].m_wchDrive, m_rgRows[idx].m_nodeId), idx);
  }
}

} // namespace ui
