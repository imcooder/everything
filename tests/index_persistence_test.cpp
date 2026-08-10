#include "core/platform.h"
#include "index/bump_string_pool.h"
#include "index/index_persistence.h"
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
  ApplyNamedRecord(store, 103, 101, L"user32.dll", FILE_ATTRIBUTE_NORMAL);
  ApplyNamedRecord(store, 200, 5, L"Users", FILE_ATTRIBUTE_DIRECTORY);
  ApplyNamedRecord(store, 201, 200, L"readme.txt", FILE_ATTRIBUTE_NORMAL);
  store.FinalizeInitialLoad();
}

std::wstring TestFilePath(LPCWSTR wszName) {
  WCHAR wszTemp[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, wszTemp);
  return std::wstring(wszTemp) + wszName;
}

// RAII: deletes the file on construction (stale run) and destruction (this run) so tests never
// depend on leftover state from a prior run and never leave litter in %TEMP% on success.
class CScopedTestFile {
public:
  explicit CScopedTestFile(LPCWSTR wszName) : m_wstrPath(TestFilePath(wszName)) {
    DeleteFileW(m_wstrPath.c_str());
    DeleteFileW((m_wstrPath + L".tmp").c_str());
  }

  ~CScopedTestFile() {
    DeleteFileW(m_wstrPath.c_str());
    DeleteFileW((m_wstrPath + L".tmp").c_str());
  }

  const std::wstring &Path() const {
    return m_wstrPath;
  }

private:
  std::wstring m_wstrPath;
};

index::INDEX_PERSIST_CHECKPOINT SampleCheckpoint() {
  index::INDEX_PERSIST_CHECKPOINT checkpoint;
  checkpoint.m_ullJournalId = 0x0102030405060708ull;
  checkpoint.m_usnNext = 123456;
  checkpoint.m_usnFirst = 100;
  checkpoint.m_usnLast = 200000;
  return checkpoint;
}

// Overwrites just the m_dwVersion field of an already-Save()'d file, leaving everything else
// (including the checksum, which does not cover the header) untouched.
bool PatchHeaderVersion(const std::wstring &wstrPath, UINT32 dwNewVersion) {
  HANDLE hFile = CreateFileW(wstrPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  const DWORD dwVersionFieldOffset = sizeof(UINT32); // m_dwMagic comes first
  DWORD dwPos = SetFilePointer(hFile, dwVersionFieldOffset, nullptr, FILE_BEGIN);
  DWORD cbWritten = 0;
  const bool bOk = dwPos == dwVersionFieldOffset && WriteFile(hFile, &dwNewVersion, sizeof(dwNewVersion), &cbWritten, nullptr) && cbWritten == sizeof(dwNewVersion);
  CloseHandle(hFile);
  return bOk;
}

} // namespace

TEST(IndexPersistence, StringPoolExportImportRoundTrip) {
  index::CBumpStringPool poolOriginal(64);
  const UINT32 offAlpha = poolOriginal.Alloc("alpha", 5);
  const UINT32 offBeta = poolOriginal.Alloc("beta", 4);
  const UINT32 offGamma = poolOriginal.Alloc("a-much-longer-string-to-force-a-second-chunk", 45);

  std::vector<char> rgExported;
  const UINT32 cbPhysical = poolOriginal.ExportBytes(rgExported);
  ASSERT_GT(cbPhysical, 0u);
  ASSERT_EQ(cbPhysical, rgExported.size());

  index::CBumpStringPool poolReloaded(64);
  ASSERT_TRUE(poolReloaded.ImportBytes(rgExported.data(), cbPhysical, poolOriginal.GetUsedBytes()));

  EXPECT_STREQ(poolReloaded.GetPtr(offAlpha), "alpha");
  EXPECT_STREQ(poolReloaded.GetPtr(offBeta), "beta");
  EXPECT_STREQ(poolReloaded.GetPtr(offGamma), "a-much-longer-string-to-force-a-second-chunk");
  EXPECT_EQ(poolReloaded.GetUsedBytes(), poolOriginal.GetUsedBytes());
}

TEST(IndexPersistence, IndexStoreRoundTripPreservesStatsAndSearch) {
  CScopedTestFile testFile(L"everything_persist_roundtrip.idx");

  index::CIndexStore storeOriginal;
  BuildSampleTree(storeOriginal);

  const index::INDEX_STATS statsOriginal = storeOriginal.GetStats();
  ASSERT_GT(statsOriginal.m_cNodes, 0u);
  ASSERT_EQ(statsOriginal.m_cUnresolvedParents, 0u);

  std::vector<UINT32> rgResultsOriginal;
  storeOriginal.SearchUtf8(".dll", rgResultsOriginal, 0);
  ASSERT_EQ(rgResultsOriginal.size(), 2u);

  const index::INDEX_PERSIST_CHECKPOINT checkpointSaved = SampleCheckpoint();
  ASSERT_TRUE(index::CIndexPersistence::Save(testFile.Path(), 0xAABBCCDD, storeOriginal, checkpointSaved));

  index::CIndexStore storeReloaded;
  index::INDEX_PERSIST_CHECKPOINT checkpointLoaded;
  const index::INDEX_PERSIST_LOAD_RESULT result = index::CIndexPersistence::Load(testFile.Path(), 0xAABBCCDD, storeReloaded, checkpointLoaded);
  ASSERT_EQ(result, index::INDEX_PERSIST_LOAD_OK);

  EXPECT_EQ(checkpointLoaded.m_ullJournalId, checkpointSaved.m_ullJournalId);
  EXPECT_EQ(checkpointLoaded.m_usnNext, checkpointSaved.m_usnNext);
  EXPECT_EQ(checkpointLoaded.m_usnFirst, checkpointSaved.m_usnFirst);
  EXPECT_EQ(checkpointLoaded.m_usnLast, checkpointSaved.m_usnLast);

  const index::INDEX_STATS statsReloaded = storeReloaded.GetStats();
  EXPECT_EQ(statsReloaded.m_cNodes, statsOriginal.m_cNodes);
  EXPECT_EQ(statsReloaded.m_cSearchEntries, statsOriginal.m_cSearchEntries);
  EXPECT_EQ(statsReloaded.m_cbPoolUsed, statsOriginal.m_cbPoolUsed);
  EXPECT_EQ(statsReloaded.m_cUnresolvedParents, 0u);

  std::vector<UINT32> rgResultsReloaded;
  storeReloaded.SearchUtf8(".dll", rgResultsReloaded, 0);
  EXPECT_EQ(rgResultsReloaded, rgResultsOriginal);

  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"parent:c:\\Windows", plan));
  storeReloaded.ResolveParsedQuery(L'C', plan);
  EXPECT_EQ(plan.m_pathScope, index::PATH_SCOPE_SUBTREE);

  std::vector<char> rgPathOriginal;
  std::vector<char> rgPathReloaded;
  ASSERT_TRUE(storeOriginal.MaterializePathUtf8(rgResultsOriginal[0], rgPathOriginal));
  ASSERT_TRUE(storeReloaded.MaterializePathUtf8(rgResultsReloaded[0], rgPathReloaded));
  EXPECT_EQ(rgPathOriginal, rgPathReloaded);
}

TEST(IndexPersistence, MissingFileReturnsNotFound) {
  CScopedTestFile testFile(L"everything_persist_missing.idx");

  index::CIndexStore store;
  index::INDEX_PERSIST_CHECKPOINT checkpoint;
  const index::INDEX_PERSIST_LOAD_RESULT result = index::CIndexPersistence::Load(testFile.Path(), 0x11223344, store, checkpoint);

  EXPECT_EQ(result, index::INDEX_PERSIST_LOAD_NOT_FOUND);
  EXPECT_EQ(store.GetStats().m_cNodes, 0u);
}

TEST(IndexPersistence, VersionMismatchRejected) {
  CScopedTestFile testFile(L"everything_persist_version.idx");

  index::CIndexStore storeOriginal;
  BuildSampleTree(storeOriginal);
  ASSERT_TRUE(index::CIndexPersistence::Save(testFile.Path(), 0x11223344, storeOriginal, SampleCheckpoint()));
  ASSERT_TRUE(PatchHeaderVersion(testFile.Path(), index::INDEX_PERSIST_VERSION + 1));

  index::CIndexStore storeReloaded;
  index::INDEX_PERSIST_CHECKPOINT checkpoint;
  const index::INDEX_PERSIST_LOAD_RESULT result = index::CIndexPersistence::Load(testFile.Path(), 0x11223344, storeReloaded, checkpoint);

  EXPECT_EQ(result, index::INDEX_PERSIST_LOAD_VERSION_MISMATCH);
  EXPECT_EQ(storeReloaded.GetStats().m_cNodes, 0u);
}

TEST(IndexPersistence, VolumeSerialMismatchRejected) {
  CScopedTestFile testFile(L"everything_persist_serial.idx");

  index::CIndexStore storeOriginal;
  BuildSampleTree(storeOriginal);
  ASSERT_TRUE(index::CIndexPersistence::Save(testFile.Path(), 0x11223344, storeOriginal, SampleCheckpoint()));

  index::CIndexStore storeReloaded;
  index::INDEX_PERSIST_CHECKPOINT checkpoint;
  const index::INDEX_PERSIST_LOAD_RESULT result = index::CIndexPersistence::Load(testFile.Path(), 0x99999999, storeReloaded, checkpoint);

  EXPECT_EQ(result, index::INDEX_PERSIST_LOAD_SERIAL_MISMATCH);
  EXPECT_EQ(storeReloaded.GetStats().m_cNodes, 0u);
}

TEST(IndexPersistence, TruncatedFileRejectedAsCorrupt) {
  CScopedTestFile testFile(L"everything_persist_truncated.idx");

  index::CIndexStore storeOriginal;
  BuildSampleTree(storeOriginal);
  ASSERT_TRUE(index::CIndexPersistence::Save(testFile.Path(), 0x11223344, storeOriginal, SampleCheckpoint()));

  // Truncate to a handful of header bytes: shorter than even INDEX_PERSIST_HEADER, so the very
  // first ReadAll() inside Load() must fail cleanly instead of reading garbage counts.
  HANDLE hFile = CreateFileW(testFile.Path().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
  SetFilePointer(hFile, 8, nullptr, FILE_BEGIN);
  SetEndOfFile(hFile);
  CloseHandle(hFile);

  index::CIndexStore storeReloaded;
  index::INDEX_PERSIST_CHECKPOINT checkpoint;
  const index::INDEX_PERSIST_LOAD_RESULT result = index::CIndexPersistence::Load(testFile.Path(), 0x11223344, storeReloaded, checkpoint);

  EXPECT_EQ(result, index::INDEX_PERSIST_LOAD_CORRUPT);
  EXPECT_EQ(storeReloaded.GetStats().m_cNodes, 0u);
}

TEST(IndexPersistence, ChecksumMismatchRejectedAsCorrupt) {
  CScopedTestFile testFile(L"everything_persist_checksum.idx");

  index::CIndexStore storeOriginal;
  BuildSampleTree(storeOriginal);
  ASSERT_TRUE(index::CIndexPersistence::Save(testFile.Path(), 0x11223344, storeOriginal, SampleCheckpoint()));

  // Flip one byte just past the header (inside the node table) without touching any header
  // field, so magic/version/serial all still validate and only the checksum can catch it.
  HANDLE hFile = CreateFileW(testFile.Path().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
  SetFilePointer(hFile, sizeof(index::INDEX_PERSIST_HEADER), nullptr, FILE_BEGIN);
  BYTE bCorrupt = 0xFF;
  DWORD cbWritten = 0;
  ASSERT_TRUE(WriteFile(hFile, &bCorrupt, 1, &cbWritten, nullptr));
  CloseHandle(hFile);

  index::CIndexStore storeReloaded;
  index::INDEX_PERSIST_CHECKPOINT checkpoint;
  const index::INDEX_PERSIST_LOAD_RESULT result = index::CIndexPersistence::Load(testFile.Path(), 0x11223344, storeReloaded, checkpoint);

  EXPECT_EQ(result, index::INDEX_PERSIST_LOAD_CORRUPT);
  EXPECT_EQ(storeReloaded.GetStats().m_cNodes, 0u);
}
