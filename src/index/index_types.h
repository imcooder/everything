#pragma once

#include <cstdint>

namespace index {

constexpr UINT32 INDEX_INVALID_NODE = UINT32_MAX;
constexpr UINT32 INDEX_ROOT_PARENT = UINT32_MAX;

enum INDEX_NODE_FLAGS : UINT16 {
  INDEX_NODE_NONE = 0,
  INDEX_NODE_DIRECTORY = 0x0001,
  INDEX_NODE_DELETED = 0x0002,
};

struct INDEX_NODE {
  ULONGLONG m_ullFrn;
  ULONGLONG m_ullParentFrn;
  UINT32 m_parentNodeId;
  UINT32 m_nameOffset;
  UINT16 m_cbName;
  UINT16 m_flags;
};

struct SEARCH_ENTRY {
  UINT32 m_nodeId;
};

struct INDEX_STATS {
  UINT32 m_cNodes;
  UINT32 m_cSearchEntries;
  UINT32 m_cbPoolUsed;
  UINT32 m_cbPoolAllocated;
  UINT32 m_cUnresolvedParents;
};

// Populated by ApplyUsnRecord when a node was touched (create/rename/delete).
struct INDEX_USN_CHANGE {
  UINT32 m_nodeId = INDEX_INVALID_NODE;
};

} // namespace index
