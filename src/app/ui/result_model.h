#pragma once

// UI-thread-only backing store for the virtual (LVS_OWNERDATA) result ListView. All
// methods here must be called from the UI thread only — ISearchSink callbacks arrive on
// volume I/O threads and must PostMessage into CMainFrame first (see app_messages.h).
//
// Rows are keyed by (drive letter, per-volume node id) since node ids are only unique
// within a single volume's index.

#include "app/ui/app_messages.h"

#include <unordered_map>
#include <vector>

namespace ui {

class CResultModel {
public:
  void Clear();

  // Returns true if the row was newly inserted (false if the key already existed, in
  // which case the row is left untouched — guards against duplicate rows from a stray
  // repeated OnBatch/OnAdded, see UI14 churn case).
  bool AddRowIfAbsent(ROW_DATA row);

  // Refreshes Name/Folder/FullPath for an already-present row without moving it
  // (rename-in-place, ISearchSink::OnUpdated). Returns true if the row existed.
  bool UpdateRowIfPresent(const ROW_DATA &row);

  // Returns true if a row with this key was removed.
  bool RemoveRow(ROW_KEY key);

  UINT32 GetCount() const {
    return static_cast<UINT32>(m_rgRows.size());
  }

  const ROW_DATA *GetRow(UINT32 idx) const {
    return idx < m_rgRows.size() ? &m_rgRows[idx] : nullptr;
  }

private:
  std::vector<ROW_DATA> m_rgRows;
  std::unordered_map<ROW_KEY, UINT32> m_mapKeyToIndex;
};

} // namespace ui
