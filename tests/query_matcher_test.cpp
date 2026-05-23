#include "core/platform.h"
#include "index/query_matcher.h"

#include <gtest/gtest.h>

#include <cstring>

namespace {

bool MatchUtf8(index::CQueryMatcher &matcher, const char *pszName) {
  return matcher.MatchesFilename(pszName, static_cast<UINT32>(strlen(pszName)));
}

} // namespace

TEST(QueryMatcher, EmptyQueryMatchesAll) {
  index::CQueryMatcher matcher;
  matcher.SetQueryUtf8("");
  EXPECT_TRUE(MatchUtf8(matcher, "anything.txt"));
}

TEST(QueryMatcher, SubstringMatch) {
  index::CQueryMatcher matcher;
  matcher.SetQueryUtf8("report");
  EXPECT_TRUE(MatchUtf8(matcher, "work_report.txt"));
  EXPECT_FALSE(MatchUtf8(matcher, "readme.txt"));
}

TEST(QueryMatcher, GlobExtension) {
  index::CQueryMatcher matcher;
  matcher.SetQueryUtf8("*.dll");
  EXPECT_TRUE(MatchUtf8(matcher, "kernel32.dll"));
  EXPECT_FALSE(MatchUtf8(matcher, "kernel32.exe"));
}

TEST(QueryMatcher, GlobQuestionMark) {
  index::CQueryMatcher matcher;
  matcher.SetQueryUtf8("file?.txt");
  EXPECT_TRUE(MatchUtf8(matcher, "file1.txt"));
  EXPECT_FALSE(MatchUtf8(matcher, "file10.txt"));
}
