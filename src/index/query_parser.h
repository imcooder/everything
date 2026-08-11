#pragma once

#include "core/platform.h"
#include "index/index_types.h"
#include "index/query_matcher.h"

#include <string>
#include <vector>

namespace index {

enum PATH_SCOPE {
  PATH_SCOPE_ENTIRE_VOLUME = 0,
  PATH_SCOPE_SUBTREE,
  PATH_SCOPE_NONE,
};

// Parsed search query: optional path restriction + optional filename filter.
// Call CIndexStore::ResolveParsedQuery on the volume thread before matching.
struct CParsedQuery {
  PATH_SCOPE m_pathScope = PATH_SCOPE_ENTIRE_VOLUME;
  WCHAR m_wchPathDrive = L'\0';
  std::vector<char> m_rgPathUtf8;
  UINT32 m_subtreeRootNodeId = INDEX_INVALID_NODE;
  bool m_bSubtreeIncludesDescendants = true;
  CQueryMatcher m_filenameMatcher;
  bool m_bHasFilenameFilter = false;

  // "ext:txt" or "ext:txt;doc report" (semicolon-separated, ANDed with an optional trailing
  // filename filter). Lowercased, no leading dot. Not combinable with a leading "parent:" in
  // this version — ext: must be the query's own prefix.
  std::vector<std::string> m_rgExtensionFiltersLower;
  bool m_bHasExtensionFilter = false;
};

// Parse UI query (UTF-16). Does not touch the index; path scope is finalized by ResolveParsedQuery.
bool ParseSearchQuery(LPCWSTR wszQuery, CParsedQuery &plan);

} // namespace index
