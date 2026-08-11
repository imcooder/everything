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
  // Display-only metadata (UI Size/Date Modified columns). Not authoritative for anything the
  // search/index logic itself depends on. Always 0 for a directory. Live USN-driven updates
  // cannot refresh m_ullFileSize (USN_RECORD_V2 carries no size field) — it is only ever set by
  // a full MFT-direct or USN-enumeration initial load and simply carries over unchanged across
  // a rename/live-touch of an already-indexed node; a real, accepted gap until the next full
  // reload, same category as the existing directory-rename-cascade gap.
  ULONGLONG m_ullFileSize;
  ULONGLONG m_ullModifiedTime; // FILETIME (100ns intervals since 1601-01-01), 0 if unknown
};

// index::CIndexPersistence (index_persistence.cpp) dumps m_rgNodes as a raw flat array of this
// struct straight to disk and reads it back the same way. It has no pointers, so this is safe as
// long as every reader/writer uses sizeof(INDEX_NODE) (never a hardcoded byte count) — persistence
// code does. Pin the actual size down here: if you add, remove, reorder, or resize a member and
// this assert fails, you must also bump INDEX_PERSIST_VERSION in index_persistence.h so old
// on-disk files are rejected instead of misread (see docs/index-persistence-testcases.md, case P6).
static_assert(sizeof(INDEX_NODE) == 48, "INDEX_NODE layout changed; bump INDEX_PERSIST_VERSION (index_persistence.h) and update this assert");
static_assert(std::is_trivially_copyable<INDEX_NODE>::value, "INDEX_NODE must stay a flat POD for raw-array on-disk persistence");

struct SEARCH_ENTRY {
  UINT32 m_nodeId;
};

// Display-only node metadata for the UI (Size/Date Modified columns) — a copy, not a live
// reference, since GetNodeMetadata is meant to be called well after the node lookup that
// produced its nodeId.
struct INDEX_NODE_METADATA {
  bool m_bIsDirectory = false;
  ULONGLONG m_ullFileSize = 0;
  ULONGLONG m_ullModifiedTime = 0;
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
