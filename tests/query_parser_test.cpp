#include "index/query_parser.h"

#include <gtest/gtest.h>

namespace {

void ExpectPathScope(const index::CParsedQuery &plan, index::PATH_SCOPE scope) {
  EXPECT_EQ(plan.m_pathScope, scope);
}

} // namespace

TEST(QueryParser, FilenameOnly) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"*.dll", plan));
  EXPECT_EQ(plan.m_wchPathDrive, L'\0');
  EXPECT_TRUE(plan.m_rgPathUtf8.empty());
  EXPECT_TRUE(plan.m_bHasFilenameFilter);
}

TEST(QueryParser, ParentWithFilename) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"parent:c:\\Windows *.dll", plan));
  EXPECT_EQ(plan.m_wchPathDrive, L'C');
  EXPECT_FALSE(plan.m_rgPathUtf8.empty());
  EXPECT_TRUE(plan.m_bHasFilenameFilter);
}

TEST(QueryParser, DrivePrefixWithSpace) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"d:\\music\\ *.flac", plan));
  EXPECT_EQ(plan.m_wchPathDrive, L'D');
  EXPECT_TRUE(plan.m_bHasFilenameFilter);
}

TEST(QueryParser, DrivePrefixOnly) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"c:\\", plan));
  EXPECT_EQ(plan.m_wchPathDrive, L'C');
  EXPECT_TRUE(plan.m_rgPathUtf8.empty());
  EXPECT_FALSE(plan.m_bHasFilenameFilter);
}

TEST(QueryParser, EmptyQuery) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"", plan));
  ExpectPathScope(plan, index::PATH_SCOPE_ENTIRE_VOLUME);
  EXPECT_FALSE(plan.m_bHasFilenameFilter);
}

TEST(QueryParser, ExtensionOnly) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"ext:txt", plan));
  EXPECT_TRUE(plan.m_bHasExtensionFilter);
  ASSERT_EQ(plan.m_rgExtensionFiltersLower.size(), 1u);
  EXPECT_EQ(plan.m_rgExtensionFiltersLower[0], "txt");
  EXPECT_FALSE(plan.m_bHasFilenameFilter);
}

TEST(QueryParser, ExtensionMixedCaseNormalizedToLower) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"ext:PNG", plan));
  ASSERT_EQ(plan.m_rgExtensionFiltersLower.size(), 1u);
  EXPECT_EQ(plan.m_rgExtensionFiltersLower[0], "png");
}

TEST(QueryParser, ExtensionListSemicolonSeparated) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"ext:txt;doc;PDF", plan));
  ASSERT_EQ(plan.m_rgExtensionFiltersLower.size(), 3u);
  EXPECT_EQ(plan.m_rgExtensionFiltersLower[0], "txt");
  EXPECT_EQ(plan.m_rgExtensionFiltersLower[1], "doc");
  EXPECT_EQ(plan.m_rgExtensionFiltersLower[2], "pdf");
}

TEST(QueryParser, ExtensionWithTrailingFilename) {
  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"ext:txt report", plan));
  ASSERT_EQ(plan.m_rgExtensionFiltersLower.size(), 1u);
  EXPECT_EQ(plan.m_rgExtensionFiltersLower[0], "txt");
  EXPECT_TRUE(plan.m_bHasFilenameFilter);
}

TEST(QueryParser, ExtensionEmptyValueRejected) {
  index::CParsedQuery plan;
  EXPECT_FALSE(index::ParseSearchQuery(L"ext:", plan));
}
