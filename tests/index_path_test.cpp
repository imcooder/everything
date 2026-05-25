#include "core/platform.h"
#include "index/index_store.h"
#include "index/query_parser.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace {

bool ApplyNamedRecord(index::CIndexStore &store, ULONGLONG ullFrn, ULONGLONG ullParentFrn, LPCWSTR wszName, DWORD dwAttributes) {
  const USHORT cchName = static_cast<USHORT>(wcslen(wszName));
  const USHORT cbName = static_cast<USHORT>(cchName * sizeof(WCHAR));
  const DWORD cbRecord = static_cast<DWORD>(sizeof(USN_RECORD_V2) + cbName);

  std::vector<BYTE> rgBuffer(cbRecord);
  USN_RECORD_V2 &record = *reinterpret_cast<USN_RECORD_V2 *>(rgBuffer.data());

  record.RecordLength = cbRecord;
  record.FileReferenceNumber = ullFrn;
  record.ParentFileReferenceNumber = ullParentFrn;
  record.FileNameOffset = sizeof(USN_RECORD_V2);
  record.FileNameLength = cbName;
  record.Reason = USN_REASON_FILE_CREATE;
  record.FileAttributes = dwAttributes;

  memcpy(rgBuffer.data() + sizeof(USN_RECORD_V2), wszName, cbName);
  return store.ApplyUsnRecord(record, nullptr);
}

void BuildSampleTree(index::CIndexStore &store) {
  store.BeginBulkLoad();
  ApplyNamedRecord(store, 100, 5, L"Windows", FILE_ATTRIBUTE_DIRECTORY);
  ApplyNamedRecord(store, 101, 100, L"System32", FILE_ATTRIBUTE_DIRECTORY);
  ApplyNamedRecord(store, 102, 101, L"kernel32.dll", FILE_ATTRIBUTE_NORMAL);
  ApplyNamedRecord(store, 200, 5, L"Users", FILE_ATTRIBUTE_DIRECTORY);
  ApplyNamedRecord(store, 201, 200, L"readme.txt", FILE_ATTRIBUTE_NORMAL);
  store.FinalizeInitialLoad();
}

UINT32 CountMatches(const index::CIndexStore &store, const index::CParsedQuery &plan) {
  UINT32 cMatches = 0;

  for (UINT32 idx = 0; idx < store.GetSearchEntryCount(); ++idx) {
    const UINT32 nodeId = store.GetSearchEntryNodeId(idx);
    if (store.NodeMatchesParsedQuery(nodeId, plan)) {
      ++cMatches;
    }
  }

  return cMatches;
}

} // namespace

TEST(IndexPath, SubtreePrunesResults) {
  index::CIndexStore store;
  BuildSampleTree(store);

  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"parent:c:\\Windows", plan));
  store.ResolveParsedQuery(L'C', plan);
  EXPECT_EQ(plan.m_pathScope, index::PATH_SCOPE_SUBTREE);
  EXPECT_EQ(CountMatches(store, plan), 3u);
}

TEST(IndexPath, SubtreeWithFilenameGlob) {
  index::CIndexStore store;
  BuildSampleTree(store);

  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"parent:c:\\Windows *.dll", plan));
  store.ResolveParsedQuery(L'C', plan);
  EXPECT_EQ(CountMatches(store, plan), 1u);
}

TEST(IndexPath, WrongDriveMatchesNothing) {
  index::CIndexStore store;
  BuildSampleTree(store);

  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"parent:d:\\Windows", plan));
  store.ResolveParsedQuery(L'C', plan);
  EXPECT_EQ(plan.m_pathScope, index::PATH_SCOPE_NONE);
  EXPECT_EQ(CountMatches(store, plan), 0u);
}

TEST(IndexPath, MissingPathMatchesNothing) {
  index::CIndexStore store;
  BuildSampleTree(store);

  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"parent:c:\\Missing", plan));
  store.ResolveParsedQuery(L'C', plan);
  EXPECT_EQ(plan.m_pathScope, index::PATH_SCOPE_NONE);
  EXPECT_EQ(CountMatches(store, plan), 0u);
}
