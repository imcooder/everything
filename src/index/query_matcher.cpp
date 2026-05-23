#include "index/query_matcher.h"

#include <cstring>

namespace index {

void CQueryMatcher::SetQueryUtf8(const std::string &strQuery) {
  m_strQuery = strQuery;
  m_bHasWildcard = strQuery.find('*') != std::string::npos || strQuery.find('?') != std::string::npos;
  m_bMatchAnywhere = true;
}

bool CQueryMatcher::MatchesFilename(const char *pszName, UINT32 cbName) const {
  if (m_strQuery.empty()) {
    return true;
  }

  if (pszName == nullptr || cbName == 0) {
    return false;
  }

  if (m_bHasWildcard) {
    return MatchesGlob(pszName, cbName);
  }

  return MatchesSubstring(pszName, cbName);
}

bool CQueryMatcher::MatchesSubstring(const char *pszName, UINT32 cbName) const {
  if (m_strQuery.size() > cbName) {
    return false;
  }

  if (m_bMatchAnywhere) {
    for (UINT32 i = 0; i + m_strQuery.size() <= cbName; ++i) {
      if (memcmp(pszName + i, m_strQuery.data(), m_strQuery.size()) == 0) {
        return true;
      }
    }
    return false;
  }

  return m_strQuery.size() == cbName && memcmp(pszName, m_strQuery.data(), cbName) == 0;
}

bool CQueryMatcher::MatchesGlob(const char *pszName, UINT32 cbName) const {
  const char *pszPattern = m_strQuery.c_str();
  const char *pszPatEnd = pszPattern + m_strQuery.size();
  const char *pszStr = pszName;
  const char *pszStrEnd = pszName + cbName;

  const char *pszPatStar = nullptr;
  const char *pszStrStar = nullptr;

  while (pszPattern < pszPatEnd) {
    if (*pszPattern == '*') {
      ++pszPattern;
      pszPatStar = pszPattern;
      pszStrStar = pszStr;
      continue;
    }

    if (*pszPattern == '?') {
      if (pszStr >= pszStrEnd) {
        return false;
      }
      ++pszPattern;
      ++pszStr;
      continue;
    }

    if (pszStr >= pszStrEnd || *pszStr != *pszPattern) {
      if (pszPatStar == nullptr) {
        return false;
      }
      pszPattern = pszPatStar;
      pszStr = ++pszStrStar;
      continue;
    }

    ++pszPattern;
    ++pszStr;
  }

  while (pszStr < pszStrEnd) {
    if (pszPatStar == nullptr) {
      return false;
    }
    pszPattern = pszPatStar;
    pszStr = ++pszStrStar;
  }

  return true;
}

} // namespace index
