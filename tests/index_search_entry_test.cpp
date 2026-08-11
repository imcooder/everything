// Regression coverage for CIndexStore::TouchSearchEntry's O(1) rewrite (was an O(n) linear scan
// over every indexed file on every single live USN record — pathological under monitoring on
// large volumes; found via a real procdump/cdb stack trace during real-hardware testing,
// 2026-08-11). These tests exercise the live-update path directly (repeated touches of the same
// node), which BuildsSearchableTreeFromMftDerivedFields-style bulk-load tests never do, since
// TouchSearchEntry is a no-op during BeginBulkLoad()/FinalizeInitialLoad().
#include "core/platform.h"
#include "index/index_store.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace {

bool ApplyNamedRecord(index::CIndexStore &store, ULONGLONG ullFrn, ULONGLONG ullParentFrn, LPCWSTR wszName, DWORD dwReason = USN_REASON_FILE_CREATE) {
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
  record.Reason = dwReason;
  record.FileAttributes = FILE_ATTRIBUTE_NORMAL;

  memcpy(rgBuffer.data() + sizeof(USN_RECORD_V2), wszName, cbName);
  return store.ApplyUsnRecord(record, nullptr);
}

} // namespace

TEST(IndexStoreSearchEntryLiveTouch, RepeatedTouchesOfSameNodeStayCorrect) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ApplyNamedRecord(store, 100, 5, L"alpha.txt");
  ApplyNamedRecord(store, 101, 5, L"beta.txt");
  store.FinalizeInitialLoad();

  ASSERT_EQ(store.GetStats().m_cSearchEntries, 2u);

  // Simulate heavy live churn on node 100 (e.g. an app repeatedly saving the same file) —
  // this is exactly the pattern that made the old O(n) TouchSearchEntry pathological.
  for (int i = 0; i < 5000; ++i) {
    ASSERT_TRUE(ApplyNamedRecord(store, 100, 5, L"alpha.txt", USN_REASON_DATA_OVERWRITE));
  }

  // Still exactly one live entry per node — no duplicates, no entries lost — despite 5000
  // tombstone+recreate cycles on the same node.
  EXPECT_EQ(store.GetStats().m_cSearchEntries, 2u);

  // Node IDs are assigned by insertion order (GetOrCreateNodeId), not equal to the FRN — check
  // content via MaterializePathUtf8 rather than assuming a specific node ID value.
  std::vector<UINT32> resultsAlpha;
  store.SearchUtf8("alpha", resultsAlpha, 0);
  ASSERT_EQ(resultsAlpha.size(), 1u);
  std::vector<char> rgAlphaPath;
  ASSERT_TRUE(store.MaterializePathUtf8(resultsAlpha[0], rgAlphaPath));
  EXPECT_EQ(std::string(rgAlphaPath.begin(), rgAlphaPath.end()), "alpha.txt");

  std::vector<UINT32> resultsBeta;
  store.SearchUtf8("beta", resultsBeta, 0);
  ASSERT_EQ(resultsBeta.size(), 1u);
  std::vector<char> rgBetaPath;
  ASSERT_TRUE(store.MaterializePathUtf8(resultsBeta[0], rgBetaPath));
  EXPECT_EQ(std::string(rgBetaPath.begin(), rgBetaPath.end()), "beta.txt");

  std::vector<UINT32> resultsAll;
  store.SearchUtf8("", resultsAll, 0);
  EXPECT_EQ(resultsAll.size(), 2u);
}

TEST(IndexStoreSearchEntryLiveTouch, TombstonedEntriesExcludedFromSearchAfterDelete) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ApplyNamedRecord(store, 200, 5, L"gamma.txt");
  store.FinalizeInitialLoad();

  ASSERT_EQ(store.GetStats().m_cSearchEntries, 1u);

  ApplyNamedRecord(store, 200, 5, L"gamma.txt", USN_REASON_FILE_DELETE);

  EXPECT_EQ(store.GetStats().m_cSearchEntries, 0u);

  std::vector<UINT32> results;
  store.SearchUtf8("gamma", results, 0);
  EXPECT_EQ(results.size(), 0u);
}

TEST(IndexStoreSearchEntryLiveTouch, ManyDistinctNodesTouchedRepeatedlyStayConsistent) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  for (ULONGLONG frn = 1000; frn < 1100; ++frn) {
    const std::wstring name = L"file" + std::to_wstring(frn) + L".txt";
    ApplyNamedRecord(store, frn, 5, name.c_str());
  }
  store.FinalizeInitialLoad();
  ASSERT_EQ(store.GetStats().m_cSearchEntries, 100u);

  // Touch every node a few times, interleaved, like a real burst of file-system activity.
  for (int round = 0; round < 10; ++round) {
    for (ULONGLONG frn = 1000; frn < 1100; ++frn) {
      const std::wstring name = L"file" + std::to_wstring(frn) + L".txt";
      ApplyNamedRecord(store, frn, 5, name.c_str(), USN_REASON_DATA_OVERWRITE);
    }
  }

  EXPECT_EQ(store.GetStats().m_cSearchEntries, 100u);

  std::vector<UINT32> results;
  store.SearchUtf8("file10", results, 0);
  EXPECT_EQ(results.size(), 100u); // file1000..file1099 all start with "file10"
}
