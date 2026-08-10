#include "index/index_store.h"

#include "index/query_parser.h"
#include "index/utf8_convert.h"

#include <string>

namespace index {

namespace {

constexpr ULONGLONG NTFS_ROOT_FRN = 5;

bool IsDotName(LPCWSTR wszName, USHORT cchName) {
  if (cchName == 1 && wszName[0] == L'.') {
    return true;
  }
  if (cchName == 2 && wszName[0] == L'.' && wszName[1] == L'.') {
    return true;
  }
  return false;
}

bool HasDeleteReason(DWORD dwReason) {
  return (dwReason & USN_REASON_FILE_DELETE) != 0;
}

bool SegmentEqualsIgnoreCase(const char *pszName, UINT32 cbName, const char *pszSegment, UINT32 cbSegment) {
  if (pszName == nullptr || pszSegment == nullptr || cbName != cbSegment) {
    return false;
  }

  for (UINT32 i = 0; i < cbName; ++i) {
    char chName = pszName[i];
    char chSegment = pszSegment[i];

    if (chName >= 'A' && chName <= 'Z') {
      chName = static_cast<char>(chName + ('a' - 'A'));
    }

    if (chSegment >= 'A' && chSegment <= 'Z') {
      chSegment = static_cast<char>(chSegment + ('a' - 'A'));
    }

    if (chName != chSegment) {
      return false;
    }
  }

  return true;
}

void SplitPathSegments(LPCSTR pszPathUtf8, UINT32 cbPathUtf8, std::vector<std::string> &rgSegments) {
  rgSegments.clear();

  if (pszPathUtf8 == nullptr || cbPathUtf8 == 0) {
    return;
  }

  UINT32 idxStart = 0;
  while (idxStart < cbPathUtf8 && (pszPathUtf8[idxStart] == '\\' || pszPathUtf8[idxStart] == '/')) {
    ++idxStart;
  }

  while (idxStart < cbPathUtf8) {
    UINT32 idxEnd = idxStart;
    while (idxEnd < cbPathUtf8 && pszPathUtf8[idxEnd] != '\\' && pszPathUtf8[idxEnd] != '/') {
      ++idxEnd;
    }

    if (idxEnd > idxStart) {
      rgSegments.emplace_back(pszPathUtf8 + idxStart, idxEnd - idxStart);
    }

    idxStart = idxEnd + 1;
  }
}

WCHAR ToUpperDriveLetter(WCHAR wchDrive) {
  if (wchDrive >= L'a' && wchDrive <= L'z') {
    return static_cast<WCHAR>(wchDrive - (L'a' - L'A'));
  }

  return wchDrive;
}

} // namespace

CIndexStore::CIndexStore() : m_pNamePool(std::make_unique<CBumpStringPool>()), m_cUnresolvedParents(0), m_bBulkLoad(false) {}

void CIndexStore::Reset() {
  m_pNamePool->Reset();
  m_rgNodes.clear();
  m_mapFrnToNodeId.clear();
  m_rgSearchEntries.clear();
  m_cUnresolvedParents = 0;
  m_bBulkLoad = false;
}

void CIndexStore::BeginBulkLoad() {
  m_bBulkLoad = true;
}

bool CIndexStore::ApplyUsnRecord(const USN_RECORD_V2 &record, INDEX_USN_CHANGE *pChange) {
  if (record.FileNameLength == 0) {
    return false;
  }

  const LPCWSTR wszName = reinterpret_cast<LPCWSTR>(reinterpret_cast<const BYTE *>(&record) + record.FileNameOffset);
  const USHORT cchName = static_cast<USHORT>(record.FileNameLength / sizeof(WCHAR));

  if (IsDotName(wszName, cchName)) {
    return false;
  }

  const ULONGLONG ullFrn = record.FileReferenceNumber;

  if (HasDeleteReason(record.Reason)) {
    MarkDeleted(ullFrn, pChange);
    return true;
  }

  if ((record.Reason & USN_REASON_RENAME_OLD_NAME) != 0) {
    MarkDeleted(ullFrn, pChange);
    return true;
  }

  const bool bOk = UpsertFromRecord(record, pChange);
  return bOk;
}

void CIndexStore::FinalizeInitialLoad() {
  m_bBulkLoad = false;
  ResolveParents();
  RebuildSearchEntries();
}

bool CIndexStore::UpsertFromRecord(const USN_RECORD_V2 &record, INDEX_USN_CHANGE *pChange) {
  const LPCWSTR wszName = reinterpret_cast<LPCWSTR>(reinterpret_cast<const BYTE *>(&record) + record.FileNameOffset);
  const USHORT cchName = static_cast<USHORT>(record.FileNameLength / sizeof(WCHAR));

  if (IsDotName(wszName, cchName)) {
    return false;
  }

  std::vector<char> rgUtf8;
  if (!WideNameToUtf8(wszName, cchName, rgUtf8)) {
    return false;
  }

  const UINT32 nameOffset = m_pNamePool->Alloc(rgUtf8.data(), static_cast<UINT32>(rgUtf8.size()));
  if (nameOffset == UINT32_MAX) {
    return false;
  }

  const UINT32 nodeId = GetOrCreateNodeId(record.FileReferenceNumber);
  INDEX_NODE &node = m_rgNodes[nodeId];

  node.m_ullFrn = record.FileReferenceNumber;
  node.m_ullParentFrn = record.ParentFileReferenceNumber;
  node.m_parentNodeId = INDEX_INVALID_NODE;
  node.m_nameOffset = nameOffset;
  node.m_cbName = static_cast<UINT16>(rgUtf8.size());
  node.m_flags = INDEX_NODE_NONE;

  if ((record.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    node.m_flags |= INDEX_NODE_DIRECTORY;
  }

  ResolveParentForNode(nodeId);
  if (!m_bBulkLoad) {
    TouchSearchEntry(nodeId);
  }

  if (pChange != nullptr) {
    pChange->m_nodeId = nodeId;
  }
  return true;
}

bool CIndexStore::UpsertFromMftRecord(ULONGLONG ullFrn, ULONGLONG ullParentFrn, LPCWSTR wszName, USHORT cchName, bool bIsDirectory, DWORD dwAttributes) {
  // dwAttributes (DOS/NTFS attribute flags from $STANDARD_INFORMATION) is accepted for parity
  // with the USN ingestion shape and potential future flag mapping (hidden/system/etc.), but
  // isDirectory is authoritative here — it comes straight from the FILE_RECORD_HEADER in-use
  // record's own directory bit, which is more reliable than re-deriving it from an attribute
  // flags DWORD (see ntfs::ParseFileRecord).
  (void)dwAttributes;

  if (cchName == 0 || wszName == nullptr) {
    return false;
  }

  if (IsDotName(wszName, cchName)) {
    return false;
  }

  std::vector<char> rgUtf8;
  if (!WideNameToUtf8(wszName, cchName, rgUtf8)) {
    return false;
  }

  const UINT32 nameOffset = m_pNamePool->Alloc(rgUtf8.data(), static_cast<UINT32>(rgUtf8.size()));
  if (nameOffset == UINT32_MAX) {
    return false;
  }

  const UINT32 nodeId = GetOrCreateNodeId(ullFrn);
  INDEX_NODE &node = m_rgNodes[nodeId];

  node.m_ullFrn = ullFrn;
  node.m_ullParentFrn = ullParentFrn;
  node.m_parentNodeId = INDEX_INVALID_NODE;
  node.m_nameOffset = nameOffset;
  node.m_cbName = static_cast<UINT16>(rgUtf8.size());
  node.m_flags = INDEX_NODE_NONE;

  if (bIsDirectory) {
    node.m_flags |= INDEX_NODE_DIRECTORY;
  }

  ResolveParentForNode(nodeId);
  if (!m_bBulkLoad) {
    TouchSearchEntry(nodeId);
  }

  return true;
}

void CIndexStore::MarkDeleted(ULONGLONG ullFrn, INDEX_USN_CHANGE *pChange) {
  const UINT32 nodeId = FindNodeIdByFrn(ullFrn);
  if (nodeId == INDEX_INVALID_NODE) {
    return;
  }

  m_rgNodes[nodeId].m_flags |= INDEX_NODE_DELETED;
  if (!m_bBulkLoad) {
    TouchSearchEntry(nodeId);
  }

  if (pChange != nullptr) {
    pChange->m_nodeId = nodeId;
  }
}

void CIndexStore::ResolveParents() {
  m_cUnresolvedParents = 0;

  for (UINT32 nodeId = 0; nodeId < static_cast<UINT32>(m_rgNodes.size()); ++nodeId) {
    INDEX_NODE &node = m_rgNodes[nodeId];

    if ((node.m_flags & INDEX_NODE_DELETED) != 0) {
      continue;
    }

    if (node.m_ullParentFrn == 0 || node.m_ullParentFrn == NTFS_ROOT_FRN) {
      node.m_parentNodeId = INDEX_ROOT_PARENT;
      continue;
    }

    const UINT32 parentId = FindNodeIdByFrn(node.m_ullParentFrn);
    if (parentId == INDEX_INVALID_NODE) {
      node.m_parentNodeId = INDEX_INVALID_NODE;
      ++m_cUnresolvedParents;
    } else {
      node.m_parentNodeId = parentId;
    }
  }
}

void CIndexStore::RebuildSearchEntries() {
  m_rgSearchEntries.clear();
  m_rgSearchEntries.reserve(m_rgNodes.size());

  for (UINT32 nodeId = 0; nodeId < static_cast<UINT32>(m_rgNodes.size()); ++nodeId) {
    const INDEX_NODE &node = m_rgNodes[nodeId];
    if ((node.m_flags & INDEX_NODE_DELETED) != 0) {
      continue;
    }
    if (node.m_cbName == 0) {
      continue;
    }

    SEARCH_ENTRY entry = {};
    entry.m_nodeId = nodeId;
    m_rgSearchEntries.push_back(entry);
  }
}

void CIndexStore::SearchUtf8(LPCSTR pszQueryUtf8, std::vector<UINT32> &rgNodeIds, UINT32 cMaxResults) const {
  rgNodeIds.clear();
  SearchUtf8Streaming(pszQueryUtf8, cMaxResults, 256, nullptr, [&rgNodeIds](const UINT32 *rgBatch, UINT32 cBatch) { rgNodeIds.insert(rgNodeIds.end(), rgBatch, rgBatch + cBatch); });
}

void CIndexStore::SearchUtf8Streaming(LPCSTR pszQueryUtf8, UINT32 cMaxResults, UINT32 cBatchSize, const std::function<bool()> &fnShouldStop, const std::function<void(const UINT32 *, UINT32)> &fnOnBatch) const {
  if (cBatchSize == 0) {
    cBatchSize = 256;
  }

  CQueryMatcher matcher;
  if (pszQueryUtf8 != nullptr) {
    matcher.SetQueryUtf8(pszQueryUtf8);
  }

  std::vector<UINT32> rgBatch;
  rgBatch.reserve(cBatchSize);
  UINT32 cTotal = 0;

  for (const SEARCH_ENTRY &entry : m_rgSearchEntries) {
    if (cMaxResults > 0 && cTotal >= cMaxResults) {
      break;
    }

    if (rgBatch.size() >= cBatchSize) {
      if (fnShouldStop && fnShouldStop()) {
        return;
      }
      fnOnBatch(rgBatch.data(), static_cast<UINT32>(rgBatch.size()));
      rgBatch.clear();
    }

    const INDEX_NODE &node = m_rgNodes[entry.m_nodeId];
    const char *pszName = m_pNamePool->GetPtr(node.m_nameOffset);
    if (matcher.MatchesFilename(pszName, node.m_cbName)) {
      rgBatch.push_back(entry.m_nodeId);
      ++cTotal;
    }
  }

  if (rgBatch.empty()) {
    return;
  }

  if (fnShouldStop && fnShouldStop()) {
    return;
  }

  fnOnBatch(rgBatch.data(), static_cast<UINT32>(rgBatch.size()));
}

UINT32 CIndexStore::GetSearchEntryCount() const {
  return static_cast<UINT32>(m_rgSearchEntries.size());
}

UINT32 CIndexStore::GetSearchEntryNodeId(UINT32 idx) const {
  return m_rgSearchEntries[idx].m_nodeId;
}

bool CIndexStore::NodeMatchesQuery(UINT32 nodeId, const CQueryMatcher &matcher) const {
  if (nodeId >= m_rgNodes.size()) {
    return false;
  }

  const INDEX_NODE &node = m_rgNodes[nodeId];
  if ((node.m_flags & INDEX_NODE_DELETED) != 0 || node.m_cbName == 0) {
    return false;
  }

  const char *pszName = m_pNamePool->GetPtr(node.m_nameOffset);
  return matcher.MatchesFilename(pszName, node.m_cbName);
}

void CIndexStore::ResolveParsedQuery(WCHAR wchVolumeDrive, CParsedQuery &plan) const {
  const bool bHasPathRestriction = plan.m_wchPathDrive != L'\0' || !plan.m_rgPathUtf8.empty();

  if (!bHasPathRestriction) {
    plan.m_pathScope = PATH_SCOPE_ENTIRE_VOLUME;
    return;
  }

  if (plan.m_wchPathDrive != L'\0' && ToUpperDriveLetter(plan.m_wchPathDrive) != ToUpperDriveLetter(wchVolumeDrive)) {
    plan.m_pathScope = PATH_SCOPE_NONE;
    plan.m_subtreeRootNodeId = INDEX_INVALID_NODE;
    return;
  }

  if (plan.m_rgPathUtf8.empty()) {
    plan.m_pathScope = PATH_SCOPE_ENTIRE_VOLUME;
    plan.m_subtreeRootNodeId = INDEX_INVALID_NODE;
    plan.m_bSubtreeIncludesDescendants = true;
    return;
  }

  std::vector<std::string> rgSegments;
  SplitPathSegments(plan.m_rgPathUtf8.data(), static_cast<UINT32>(plan.m_rgPathUtf8.size()), rgSegments);

  if (rgSegments.empty()) {
    plan.m_pathScope = PATH_SCOPE_ENTIRE_VOLUME;
    plan.m_subtreeRootNodeId = INDEX_INVALID_NODE;
    plan.m_bSubtreeIncludesDescendants = true;
    return;
  }

  UINT32 parentId = INDEX_ROOT_PARENT;

  for (const std::string &strSegment : rgSegments) {
    UINT32 matchedNodeId = INDEX_INVALID_NODE;

    for (UINT32 nodeId = 0; nodeId < static_cast<UINT32>(m_rgNodes.size()); ++nodeId) {
      const INDEX_NODE &node = m_rgNodes[nodeId];
      if ((node.m_flags & INDEX_NODE_DELETED) != 0 || node.m_cbName == 0) {
        continue;
      }

      if (node.m_parentNodeId != parentId) {
        continue;
      }

      const char *pszName = m_pNamePool->GetPtr(node.m_nameOffset);
      if (SegmentEqualsIgnoreCase(pszName, node.m_cbName, strSegment.data(), static_cast<UINT32>(strSegment.size()))) {
        matchedNodeId = nodeId;
        break;
      }
    }

    if (matchedNodeId == INDEX_INVALID_NODE) {
      plan.m_pathScope = PATH_SCOPE_NONE;
      plan.m_subtreeRootNodeId = INDEX_INVALID_NODE;
      return;
    }

    parentId = matchedNodeId;
  }

  plan.m_pathScope = PATH_SCOPE_SUBTREE;
  plan.m_subtreeRootNodeId = parentId;
  plan.m_bSubtreeIncludesDescendants = (m_rgNodes[parentId].m_flags & INDEX_NODE_DIRECTORY) != 0;
}

bool CIndexStore::IsNodeInSubtree(UINT32 nodeId, const CParsedQuery &plan) const {
  switch (plan.m_pathScope) {
  case PATH_SCOPE_ENTIRE_VOLUME:
    return true;
  case PATH_SCOPE_NONE:
    return false;
  case PATH_SCOPE_SUBTREE:
    break;
  }

  if (nodeId >= m_rgNodes.size() || plan.m_subtreeRootNodeId >= m_rgNodes.size()) {
    return false;
  }

  if (nodeId == plan.m_subtreeRootNodeId) {
    return true;
  }

  if (!plan.m_bSubtreeIncludesDescendants) {
    return false;
  }

  UINT32 currentId = nodeId;
  for (UINT32 depth = 0; depth < 512; ++depth) {
    if (currentId >= m_rgNodes.size()) {
      return false;
    }

    const INDEX_NODE &node = m_rgNodes[currentId];
    if (node.m_parentNodeId == plan.m_subtreeRootNodeId) {
      return true;
    }

    if (node.m_parentNodeId == INDEX_ROOT_PARENT || node.m_parentNodeId == INDEX_INVALID_NODE) {
      return false;
    }

    currentId = node.m_parentNodeId;
  }

  return false;
}

bool CIndexStore::NodeMatchesParsedQuery(UINT32 nodeId, const CParsedQuery &plan) const {
  if (!IsNodeInSubtree(nodeId, plan)) {
    return false;
  }

  if (!plan.m_bHasFilenameFilter) {
    if (nodeId >= m_rgNodes.size()) {
      return false;
    }

    const INDEX_NODE &node = m_rgNodes[nodeId];
    return (node.m_flags & INDEX_NODE_DELETED) == 0 && node.m_cbName > 0;
  }

  return NodeMatchesQuery(nodeId, plan.m_filenameMatcher);
}

bool CIndexStore::MaterializePathUtf8(UINT32 nodeId, std::vector<char> &rgPathUtf8) const {
  rgPathUtf8.clear();

  if (nodeId >= m_rgNodes.size()) {
    return false;
  }

  struct SEGMENT {
    const char *m_psz;
    UINT16 m_cb;
  };

  std::vector<SEGMENT> rgSegments;
  UINT32 currentId = nodeId;

  for (UINT32 depth = 0; depth < 512; ++depth) {
    if (currentId >= m_rgNodes.size()) {
      return false;
    }

    const INDEX_NODE &node = m_rgNodes[currentId];
    if ((node.m_flags & INDEX_NODE_DELETED) != 0) {
      return false;
    }

    if (node.m_cbName > 0) {
      rgSegments.push_back({m_pNamePool->GetPtr(node.m_nameOffset), node.m_cbName});
    }

    if (node.m_parentNodeId == INDEX_ROOT_PARENT || node.m_parentNodeId == INDEX_INVALID_NODE) {
      break;
    }

    currentId = node.m_parentNodeId;
  }

  if (rgSegments.empty()) {
    return false;
  }

  for (auto it = rgSegments.rbegin(); it != rgSegments.rend(); ++it) {
    if (!rgPathUtf8.empty()) {
      rgPathUtf8.push_back('\\');
    }

    rgPathUtf8.insert(rgPathUtf8.end(), it->m_psz, it->m_psz + it->m_cb);
  }

  return true;
}

INDEX_STATS CIndexStore::GetStats() const {
  INDEX_STATS stats = {};
  stats.m_cNodes = static_cast<UINT32>(m_rgNodes.size());
  stats.m_cSearchEntries = static_cast<UINT32>(m_rgSearchEntries.size());
  stats.m_cbPoolUsed = m_pNamePool->GetUsedBytes();
  stats.m_cbPoolAllocated = m_pNamePool->GetAllocatedBytes();
  stats.m_cUnresolvedParents = m_cUnresolvedParents;
  return stats;
}

void CIndexStore::LoadPersistedState(std::vector<INDEX_NODE> &&rgNodes, const char *pPoolBytes, UINT32 cbPoolPhysical, UINT32 cbPoolLogical) {
  m_rgNodes = std::move(rgNodes);
  m_pNamePool->ImportBytes(pPoolBytes, cbPoolPhysical, cbPoolLogical);

  m_mapFrnToNodeId.clear();
  m_mapFrnToNodeId.reserve(m_rgNodes.size());
  for (UINT32 nodeId = 0; nodeId < static_cast<UINT32>(m_rgNodes.size()); ++nodeId) {
    m_mapFrnToNodeId.emplace(m_rgNodes[nodeId].m_ullFrn, nodeId);
  }

  m_cUnresolvedParents = 0;
  m_bBulkLoad = false;
  ResolveParents();
  RebuildSearchEntries();
}

UINT32 CIndexStore::GetOrCreateNodeId(ULONGLONG ullFrn) {
  const auto it = m_mapFrnToNodeId.find(ullFrn);
  if (it != m_mapFrnToNodeId.end()) {
    return it->second;
  }

  const UINT32 nodeId = static_cast<UINT32>(m_rgNodes.size());
  m_rgNodes.push_back(INDEX_NODE{});
  m_mapFrnToNodeId.emplace(ullFrn, nodeId);
  return nodeId;
}

UINT32 CIndexStore::FindNodeIdByFrn(ULONGLONG ullFrn) const {
  const auto it = m_mapFrnToNodeId.find(ullFrn);
  if (it == m_mapFrnToNodeId.end()) {
    return INDEX_INVALID_NODE;
  }
  return it->second;
}

void CIndexStore::ResolveParentForNode(UINT32 nodeId) {
  if (nodeId >= m_rgNodes.size()) {
    return;
  }

  INDEX_NODE &node = m_rgNodes[nodeId];

  if (node.m_ullParentFrn == 0 || node.m_ullParentFrn == NTFS_ROOT_FRN) {
    node.m_parentNodeId = INDEX_ROOT_PARENT;
    return;
  }

  const UINT32 parentId = FindNodeIdByFrn(node.m_ullParentFrn);
  node.m_parentNodeId = parentId == INDEX_INVALID_NODE ? INDEX_INVALID_NODE : parentId;
}

void CIndexStore::TouchSearchEntry(UINT32 nodeId) {
  for (auto it = m_rgSearchEntries.begin(); it != m_rgSearchEntries.end(); ++it) {
    if (it->m_nodeId == nodeId) {
      m_rgSearchEntries.erase(it);
      break;
    }
  }

  if (nodeId >= m_rgNodes.size()) {
    return;
  }

  const INDEX_NODE &node = m_rgNodes[nodeId];
  if ((node.m_flags & INDEX_NODE_DELETED) != 0 || node.m_cbName == 0) {
    return;
  }

  SEARCH_ENTRY entry = {};
  entry.m_nodeId = nodeId;
  m_rgSearchEntries.push_back(entry);
}

} // namespace index
