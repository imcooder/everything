#include "app/ui/result_model.h"

namespace ui {

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

} // namespace ui
