// Coverage for the "ext:" search modifier end to end: CParsedQuery (query_parser_test.cpp covers
// parsing in isolation) feeding CIndexStore::NodeMatchesParsedQuery against real ingested nodes.
#include "index/index_store.h"
#include "index/query_parser.h"

#include <gtest/gtest.h>

namespace {

std::vector<UINT32> SearchWithParsedQuery(index::CIndexStore &store, LPCWSTR wszQuery, WCHAR wchDrive) {
  index::CParsedQuery plan;
  if (!index::ParseSearchQuery(wszQuery, plan)) {
    return {};
  }
  store.ResolveParsedQuery(wchDrive, plan);

  std::vector<UINT32> rgMatches;
  const UINT32 cCount = store.GetSearchEntryCount();
  for (UINT32 i = 0; i < cCount; ++i) {
    const UINT32 nodeId = store.GetSearchEntryNodeId(i);
    if (store.NodeMatchesParsedQuery(nodeId, plan)) {
      rgMatches.push_back(nodeId);
    }
  }
  return rgMatches;
}

std::string NameOf(index::CIndexStore &store, UINT32 nodeId) {
  std::vector<char> rgPath;
  if (!store.MaterializePathUtf8(nodeId, rgPath)) {
    return "";
  }
  return std::string(rgPath.begin(), rgPath.end());
}

} // namespace

TEST(QueryExtensionFilter, MatchesOnlyRequestedExtension) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ASSERT_TRUE(store.UpsertFromMftRecord(100, 5, L"report.txt", 10, false, FILE_ATTRIBUTE_NORMAL));
  ASSERT_TRUE(store.UpsertFromMftRecord(101, 5, L"photo.png", 9, false, FILE_ATTRIBUTE_NORMAL));
  store.FinalizeInitialLoad();

  const auto matches = SearchWithParsedQuery(store, L"ext:txt", L'C');
  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(NameOf(store, matches[0]), "report.txt");
}

TEST(QueryExtensionFilter, CaseInsensitive) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ASSERT_TRUE(store.UpsertFromMftRecord(100, 5, L"Report.TXT", 10, false, FILE_ATTRIBUTE_NORMAL));
  store.FinalizeInitialLoad();

  EXPECT_EQ(SearchWithParsedQuery(store, L"ext:txt", L'C').size(), 1u);
  EXPECT_EQ(SearchWithParsedQuery(store, L"ext:TXT", L'C').size(), 1u);
}

TEST(QueryExtensionFilter, UsesLastDotOnlyForMultiDotNames) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ASSERT_TRUE(store.UpsertFromMftRecord(100, 5, L"archive.tar.gz", 14, false, FILE_ATTRIBUTE_NORMAL));
  store.FinalizeInitialLoad();

  EXPECT_EQ(SearchWithParsedQuery(store, L"ext:gz", L'C').size(), 1u);
  EXPECT_EQ(SearchWithParsedQuery(store, L"ext:tar", L'C').size(), 0u);
  EXPECT_EQ(SearchWithParsedQuery(store, L"ext:tar.gz", L'C').size(), 0u);
}

TEST(QueryExtensionFilter, LeadingDotfileHasNoExtension) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ASSERT_TRUE(store.UpsertFromMftRecord(100, 5, L".gitignore", 10, false, FILE_ATTRIBUTE_NORMAL));
  store.FinalizeInitialLoad();

  EXPECT_EQ(SearchWithParsedQuery(store, L"ext:gitignore", L'C').size(), 0u);
}

TEST(QueryExtensionFilter, SemicolonListMatchesAny) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ASSERT_TRUE(store.UpsertFromMftRecord(100, 5, L"a.txt", 5, false, FILE_ATTRIBUTE_NORMAL));
  ASSERT_TRUE(store.UpsertFromMftRecord(101, 5, L"b.doc", 5, false, FILE_ATTRIBUTE_NORMAL));
  ASSERT_TRUE(store.UpsertFromMftRecord(102, 5, L"c.pdf", 5, false, FILE_ATTRIBUTE_NORMAL));
  store.FinalizeInitialLoad();

  EXPECT_EQ(SearchWithParsedQuery(store, L"ext:txt;doc", L'C').size(), 2u);
}

TEST(QueryExtensionFilter, CombinesWithTrailingFilenameFilter) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ASSERT_TRUE(store.UpsertFromMftRecord(100, 5, L"report.txt", 10, false, FILE_ATTRIBUTE_NORMAL));
  ASSERT_TRUE(store.UpsertFromMftRecord(101, 5, L"other.txt", 9, false, FILE_ATTRIBUTE_NORMAL));
  store.FinalizeInitialLoad();

  const auto matches = SearchWithParsedQuery(store, L"ext:txt report", L'C');
  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(NameOf(store, matches[0]), "report.txt");
}

TEST(QueryExtensionFilter, DeletedNodeExcludedEvenIfExtensionMatches) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ASSERT_TRUE(store.UpsertFromMftRecord(100, 5, L"gone.txt", 8, false, FILE_ATTRIBUTE_NORMAL));
  store.FinalizeInitialLoad();
  ASSERT_EQ(SearchWithParsedQuery(store, L"ext:txt", L'C').size(), 1u);

  const USHORT cchName = static_cast<USHORT>(wcslen(L"gone.txt"));
  const USHORT cbName = static_cast<USHORT>(cchName * sizeof(WCHAR));
  const DWORD cbRecord = static_cast<DWORD>(sizeof(USN_RECORD_V2) + cbName);
  std::vector<BYTE> rgBuffer(cbRecord);
  USN_RECORD_V2 &record = *reinterpret_cast<USN_RECORD_V2 *>(rgBuffer.data());
  record.RecordLength = cbRecord;
  record.FileReferenceNumber = 100;
  record.ParentFileReferenceNumber = 5;
  record.FileNameOffset = sizeof(USN_RECORD_V2);
  record.FileNameLength = cbName;
  record.Reason = USN_REASON_FILE_DELETE;
  memcpy(rgBuffer.data() + sizeof(USN_RECORD_V2), L"gone.txt", cbName);
  ASSERT_TRUE(store.ApplyUsnRecord(record, nullptr));

  EXPECT_EQ(SearchWithParsedQuery(store, L"ext:txt", L'C').size(), 0u);
}
