#pragma once

#include "core/platform.h"

#include <string>
#include <vector>

namespace index {

// Phase-1 filename matcher (ASCII-oriented fast path).
class CQueryMatcher {
public:
  void SetQueryUtf8(const std::string &strQuery);

  bool MatchesFilename(const char *pszName, UINT32 cbName) const;

private:
  bool MatchesSubstring(const char *pszName, UINT32 cbName) const;
  bool MatchesGlob(const char *pszName, UINT32 cbName) const;

  std::string m_strQuery;
  bool m_bHasWildcard;
  bool m_bMatchAnywhere;
};

} // namespace index
