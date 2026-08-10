#pragma once

#include "core/platform.h"

#include <cstdint>
#include <type_traits>

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

// index::CIndexPersistence (index_persistence.cpp) dumps m_rgNodes as a raw flat array of this
// struct straight to disk and reads it back the same way. It has no pointers, so this is safe as
// long as every reader/writer uses sizeof(INDEX_NODE) (never a hardcoded byte count) — persistence
// code does. The 28 content bytes (two ULONGLONG + two UINT32 + two UINT16) round up to 32 because
// the struct's alignment is 8 (from the ULONGLONG members), leaving 4 bytes of trailing padding
// after m_flags; that padding is written and read back as-is, so it doesn't affect correctness,
// only slightly bloats the on-disk/in-memory size versus a #pragma pack(1) layout. Pin the actual
// size down here: if you add, remove, reorder, or resize a member and this assert fails, you must
// also bump INDEX_PERSIST_VERSION in index_persistence.h so old on-disk files are rejected instead
// of misread (see docs/index-persistence-testcases.md, case P6).
static_assert(sizeof(INDEX_NODE) == 32, "INDEX_NODE layout changed; bump INDEX_PERSIST_VERSION (index_persistence.h) and update this assert");
static_assert(std::is_trivially_copyable<INDEX_NODE>::value, "INDEX_NODE must stay a flat POD for raw-array on-disk persistence");

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
