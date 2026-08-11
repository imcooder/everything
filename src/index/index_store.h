#pragma once

#include "core/platform.h"
#include "index/bump_string_pool.h"
#include "index/index_types.h"
#include "index/query_matcher.h"
#include "index/query_parser.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace index {

// Per-volume in-memory index. Thread-affinity: caller must be the volume I/O thread.
class CIndexStore {
public:
  CIndexStore();

  void Reset();
  void BeginBulkLoad();
  bool ApplyUsnRecord(const USN_RECORD_V2 &record, INDEX_USN_CHANGE *pChange = nullptr);

  // Direct-MFT-read ingestion (ntfs::CMftReader): takes MFT-derived fields straight from a
  // parsed $FILE_NAME/$STANDARD_INFORMATION pair instead of requiring a fabricated
  // USN_RECORD_V2. Shares UpsertFromRecord's node/name-pool/parent-link mechanics; callers
  // still finish with FinalizeInitialLoad() exactly as the USN bulk-load path does, so parent
  // resolution and search-entry rebuilding are never duplicated between the two load paths.
  bool UpsertFromMftRecord(ULONGLONG ullFrn, ULONGLONG ullParentFrn, LPCWSTR wszName, USHORT cchName, bool bIsDirectory, DWORD dwAttributes);

  void FinalizeInitialLoad();

  void SearchUtf8(LPCSTR pszQueryUtf8, std::vector<UINT32> &rgNodeIds, UINT32 cMaxResults) const;

  void SearchUtf8Streaming(LPCSTR pszQueryUtf8, UINT32 cMaxResults, UINT32 cBatchSize, const std::function<bool()> &fnShouldStop, const std::function<void(const UINT32 *, UINT32)> &fnOnBatch) const;

  UINT32 GetSearchEntryCount() const;
  UINT32 GetSearchEntryNodeId(UINT32 idx) const;
  bool NodeMatchesQuery(UINT32 nodeId, const CQueryMatcher &matcher) const;
  void ResolveParsedQuery(WCHAR wchVolumeDrive, CParsedQuery &plan) const;
  bool IsNodeInSubtree(UINT32 nodeId, const CParsedQuery &plan) const;
  bool NodeMatchesParsedQuery(UINT32 nodeId, const CParsedQuery &plan) const;
  bool MaterializePathUtf8(UINT32 nodeId, std::vector<char> &rgPathUtf8) const;

  INDEX_STATS GetStats() const;

  // Persistence support (index::CIndexPersistence). Save() reads the node table and string pool
  // through these read-only accessors; Load() replaces both wholesale via LoadPersistedState(),
  // which rebuilds m_mapFrnToNodeId and m_rgSearchEntries exactly as FinalizeInitialLoad() does
  // after a full USN enumeration, so FindNodeIdByFrn/search behave identically either way.
  const std::vector<INDEX_NODE> &GetNodesForPersist() const {
    return m_rgNodes;
  }
  const IStringPoolBackend &GetNamePoolForPersist() const {
    return *m_pNamePool;
  }
  void LoadPersistedState(std::vector<INDEX_NODE> &&rgNodes, const char *pPoolBytes, UINT32 cbPoolPhysical, UINT32 cbPoolLogical);

private:
  bool UpsertFromRecord(const USN_RECORD_V2 &record, INDEX_USN_CHANGE *pChange);
  void MarkDeleted(ULONGLONG ullFrn, INDEX_USN_CHANGE *pChange = nullptr);
  void ResolveParents();
  void RebuildSearchEntries();

  UINT32 GetOrCreateNodeId(ULONGLONG ullFrn);
  UINT32 FindNodeIdByFrn(ULONGLONG ullFrn) const;
  void ResolveParentForNode(UINT32 nodeId);
  void TouchSearchEntry(UINT32 nodeId);

  std::unique_ptr<IStringPoolBackend> m_pNamePool;
  std::vector<INDEX_NODE> m_rgNodes;
  std::unordered_map<ULONGLONG, UINT32> m_mapFrnToNodeId;
  std::vector<SEARCH_ENTRY> m_rgSearchEntries;
  // nodeId -> its live slot in m_rgSearchEntries, so TouchSearchEntry can find/retire an existing
  // entry in O(1) instead of a linear scan over every indexed file (was O(n) per USN record —
  // pathological under live monitoring on large volumes, see notes on TouchSearchEntry). A
  // retired slot is tombstoned in place (m_nodeId = INDEX_INVALID_NODE) rather than erased, so
  // this map's indices never need updating for unrelated entries; tombstones are skipped by
  // every reader and only reclaimed by the next full RebuildSearchEntries().
  std::unordered_map<UINT32, UINT32> m_mapNodeIdToSearchEntryIndex;
  UINT32 m_cUnresolvedParents;
  bool m_bBulkLoad;
};

} // namespace index
