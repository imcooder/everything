#include "core/platform.h"
#include "index/index_store.h"
#include "ntfs/mft_record.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

// Hand-crafted synthetic MFT buffers exercising ntfs/mft_record.{h,cpp} — the pure,
// I/O-free parsing layer — per docs/mft-direct-read-testcases.md. This is the layer that can
// be verified in isolation without a real NTFS volume or Administrator rights; ntfs/mft_reader
// (real ReadFile/SetFilePointerEx I/O against a raw volume handle) is reasoned-through-code
// only, exactly as the test doc anticipates for M1-M3, M7-M9, M12.

namespace {

using namespace ntfs;

template <typename T> void AppendValue(std::vector<BYTE> &buf, const T &v) {
  const BYTE *pb = reinterpret_cast<const BYTE *>(&v);
  buf.insert(buf.end(), pb, pb + sizeof(T));
}

void AppendResidentAttribute(std::vector<BYTE> &buf, UINT32 typeCode, const std::vector<BYTE> &content) {
  ATTRIBUTE_RECORD_HEADER header = {};
  header.TypeCode = typeCode;
  header.NonResident = 0;
  header.Length = static_cast<UINT32>(sizeof(ATTRIBUTE_RECORD_HEADER) + sizeof(ATTRIBUTE_RESIDENT_TAIL) + content.size());

  ATTRIBUTE_RESIDENT_TAIL tail = {};
  tail.ContentLength = static_cast<UINT32>(content.size());
  tail.ContentOffset = static_cast<UINT16>(sizeof(ATTRIBUTE_RECORD_HEADER) + sizeof(ATTRIBUTE_RESIDENT_TAIL));

  AppendValue(buf, header);
  AppendValue(buf, tail);
  buf.insert(buf.end(), content.begin(), content.end());
}

std::vector<BYTE> BuildStandardInfoContent(DWORD dwDosAttributes) {
  STANDARD_INFORMATION info = {};
  info.DosFileAttributes = dwDosAttributes;
  std::vector<BYTE> content;
  AppendValue(content, info);
  return content;
}

std::vector<BYTE> BuildFileNameContent(ULONGLONG ullParentFrn, LPCWSTR wszName, BYTE bNamespace) {
  FILE_NAME_ATTRIBUTE fn = {};
  fn.ParentDirectory = ullParentFrn;
  fn.FileNameLength = static_cast<BYTE>(wcslen(wszName));
  fn.FileNameNamespace = bNamespace;

  std::vector<BYTE> content;
  AppendValue(content, fn);
  const BYTE *pName = reinterpret_cast<const BYTE *>(wszName);
  content.insert(content.end(), pName, pName + fn.FileNameLength * sizeof(WCHAR));
  return content;
}

struct SYNTHETIC_RECORD_OPTIONS {
  DWORD cbRecordSize = 1024;
  DWORD cbSectorSize = 512;
  UINT16 usnSequenceNumber = 1;
  UINT16 headerFlags = MFT_RECORD_FLAG_IN_USE;
  ULONGLONG baseFileRecordReference = 0;
  bool bIncludeStandardInfo = true;
  DWORD dwDosAttributes = FILE_ATTRIBUTE_NORMAL;
  bool bIncludeFileName = true;
  ULONGLONG ullParentFrn = 5;
  std::wstring wstrWin32Name = L"hello.txt";
  bool bIncludeDosDuplicate = false;
  std::wstring wstrDosName = L"HELLO~1.TXT";
};

struct BUILT_RECORD {
  std::vector<BYTE> buf;
  UINT16 usaOffset = 0;
  UINT16 usaSize = 0;
};

// Builds header + attributes + end marker exactly as they'd sit on disk, with correct
// UsedSize/AllocatedSize/UpdateSequenceArrayOffset/Size header fields, but WITHOUT the
// write-time fixup applied yet — i.e. every byte, including each sector's real last two
// bytes, is genuine record content. EncodeFixupInPlace (below) is the separate step that
// mimics what NTFS itself does at write time, so the fixup round-trip is testable end to end.
BUILT_RECORD BuildRecordUnfixed(const SYNTHETIC_RECORD_OPTIONS &opts) {
  const DWORD cSectors = opts.cbRecordSize / opts.cbSectorSize;
  const UINT16 usaSize = static_cast<UINT16>(1 + cSectors);
  const UINT16 usaOffset = static_cast<UINT16>(sizeof(FILE_RECORD_HEADER));
  const UINT16 firstAttrOffset = static_cast<UINT16>(usaOffset + usaSize * sizeof(UINT16));

  std::vector<BYTE> buf(opts.cbRecordSize, 0);

  FILE_RECORD_HEADER header = {};
  header.Signature = MFT_SIGNATURE_FILE;
  header.UpdateSequenceArrayOffset = usaOffset;
  header.UpdateSequenceArraySize = usaSize;
  header.SequenceNumber = opts.usnSequenceNumber;
  header.HardLinkCount = 1;
  header.FirstAttributeOffset = firstAttrOffset;
  header.Flags = opts.headerFlags;
  header.BaseFileRecordReference = opts.baseFileRecordReference;

  memcpy(buf.data(), &header, sizeof(header));

  std::vector<BYTE> attrs;
  if (opts.bIncludeStandardInfo) {
    AppendResidentAttribute(attrs, ATTR_TYPE_STANDARD_INFORMATION, BuildStandardInfoContent(opts.dwDosAttributes));
  }
  if (opts.bIncludeFileName) {
    AppendResidentAttribute(attrs, ATTR_TYPE_FILE_NAME, BuildFileNameContent(opts.ullParentFrn, opts.wstrWin32Name.c_str(), FILE_NAME_NAMESPACE_WIN32));
  }
  if (opts.bIncludeDosDuplicate) {
    AppendResidentAttribute(attrs, ATTR_TYPE_FILE_NAME, BuildFileNameContent(opts.ullParentFrn, opts.wstrDosName.c_str(), FILE_NAME_NAMESPACE_DOS));
  }

  ATTRIBUTE_RECORD_HEADER endMarker = {};
  endMarker.TypeCode = ATTR_TYPE_END;
  AppendValue(attrs, endMarker);

  const DWORD usedSize = firstAttrOffset + static_cast<DWORD>(attrs.size());
  EXPECT_LE(usedSize, buf.size()) << "synthetic record too small for its content; grow cbRecordSize";
  memcpy(buf.data() + firstAttrOffset, attrs.data(), attrs.size());

  FILE_RECORD_HEADER *pHdr = reinterpret_cast<FILE_RECORD_HEADER *>(buf.data());
  pHdr->UsedSize = usedSize;
  pHdr->AllocatedSize = opts.cbRecordSize;

  return {buf, usaOffset, usaSize};
}

// Mirrors the on-disk write-time fixup: stash each sector's real last two bytes into the
// update sequence array, then stamp the check value over them (this is exactly the state a
// caller reads back off a real disk, and exactly what ApplyFixup must undo).
void EncodeFixupInPlace(BUILT_RECORD &record, DWORD cbSectorSize, UINT16 magic) {
  UINT16 *pUsa = reinterpret_cast<UINT16 *>(record.buf.data() + record.usaOffset);
  pUsa[0] = magic;

  const DWORD cSectors = record.usaSize - 1;
  for (DWORD i = 0; i < cSectors; ++i) {
    UINT16 *pLastWord = reinterpret_cast<UINT16 *>(record.buf.data() + (i + 1) * cbSectorSize - sizeof(UINT16));
    pUsa[1 + i] = *pLastWord;
    *pLastWord = magic;
  }
}

std::vector<BYTE> BuildSyntheticBootSector(UINT16 bytesPerSector = 512, BYTE sectorsPerCluster = 8, UINT64 mftStartLcn = 4, INT8 clustersPerFileRecordSegment = -10) {
  std::vector<BYTE> buf(512, 0);
  NTFS_BOOT_SECTOR *pBoot = reinterpret_cast<NTFS_BOOT_SECTOR *>(buf.data());
  memcpy(pBoot->OemId, "NTFS    ", 8);
  pBoot->BytesPerSector = bytesPerSector;
  pBoot->SectorsPerCluster = sectorsPerCluster;
  pBoot->MftStartLcn = mftStartLcn;
  pBoot->ClustersPerFileRecordSegment = clustersPerFileRecordSegment;
  pBoot->ClustersPerIndexBuffer = -12;
  pBoot->EndMarker = 0xAA55;
  return buf;
}

} // namespace

// ---------------------------------------------------------------------------
// Boot sector
// ---------------------------------------------------------------------------

TEST(MftBootSector, ValidBootSectorParsesGeometryAndMftLocation) {
  const std::vector<BYTE> buf = BuildSyntheticBootSector();
  NTFS_BOOT_PARAMS params;
  ASSERT_TRUE(ParseBootSector(buf.data(), static_cast<DWORD>(buf.size()), params));

  EXPECT_EQ(params.cbBytesPerSector, 512u);
  EXPECT_EQ(params.cbCluster, 512u * 8u);
  EXPECT_EQ(params.ullMftStartLcn, 4u);
  EXPECT_EQ(params.cbMftRecordSize, 1024u); // ClustersPerFileRecordSegment == -10 => 2^10 bytes
}

TEST(MftBootSector, BadOemIdRejected) {
  std::vector<BYTE> buf = BuildSyntheticBootSector();
  memcpy(buf.data() + 3, "FAT32   ", 8);

  NTFS_BOOT_PARAMS params;
  EXPECT_FALSE(ParseBootSector(buf.data(), static_cast<DWORD>(buf.size()), params));
}

TEST(MftBootSector, BadEndMarkerRejected) {
  std::vector<BYTE> buf = BuildSyntheticBootSector();
  reinterpret_cast<NTFS_BOOT_SECTOR *>(buf.data())->EndMarker = 0x1234;

  NTFS_BOOT_PARAMS params;
  EXPECT_FALSE(ParseBootSector(buf.data(), static_cast<DWORD>(buf.size()), params));
}

TEST(MftBootSector, TooShortBufferRejected) {
  std::vector<BYTE> buf(16, 0);
  NTFS_BOOT_PARAMS params;
  EXPECT_FALSE(ParseBootSector(buf.data(), static_cast<DWORD>(buf.size()), params));
}

TEST(MftRecordSizeEncoding, PositiveMeansClusterCount) {
  EXPECT_EQ(ResolveRecordOrIndexSize(2, 4096u), 8192u);
  EXPECT_EQ(ResolveRecordOrIndexSize(1, 512u), 512u);
}

TEST(MftRecordSizeEncoding, NegativeMeansPowerOfTwoBytesIndependentOfClusterSize) {
  EXPECT_EQ(ResolveRecordOrIndexSize(-10, 4096u), 1024u); // 2^10, typical 1 KB MFT record
  EXPECT_EQ(ResolveRecordOrIndexSize(-12, 512u), 4096u);  // 2^12, independent of the 512-byte cluster here
}

// ---------------------------------------------------------------------------
// Data runs (non-resident attribute run-length-encoded cluster lists)
// ---------------------------------------------------------------------------

TEST(MftDataRuns, SingleContiguousRun) {
  const std::vector<BYTE> runs = {0x11, 0x64, 0x10, 0x00}; // 1-byte length, 1-byte offset: 0x64 clusters @ LCN 0x10
  std::vector<MFT_DATA_RUN> out;
  ASSERT_TRUE(ParseDataRuns(runs.data(), static_cast<DWORD>(runs.size()), 0, out));

  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].ullVcnStart, 0u);
  EXPECT_EQ(out[0].cClusters, 0x64u);
  EXPECT_EQ(out[0].ullLcn, 0x10u);
  EXPECT_FALSE(out[0].bSparse);
}

TEST(MftDataRuns, MultipleRunsWithNegativeOffsetDelta) {
  // Run 1: 5 clusters at LCN 100. Run 2: 3 clusters, LCN delta -50 relative to run 1 => LCN 50.
  // Models a fragmented $MFT where the second extent sits physically before the first.
  std::vector<BYTE> runs;
  runs.push_back(0x11);
  runs.push_back(5);
  runs.push_back(100);
  runs.push_back(0x11);
  runs.push_back(3);
  runs.push_back(static_cast<BYTE>(-50));
  runs.push_back(0x00);

  std::vector<MFT_DATA_RUN> out;
  ASSERT_TRUE(ParseDataRuns(runs.data(), static_cast<DWORD>(runs.size()), 0, out));

  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].ullVcnStart, 0u);
  EXPECT_EQ(out[0].cClusters, 5u);
  EXPECT_EQ(out[0].ullLcn, 100u);
  EXPECT_EQ(out[1].ullVcnStart, 5u);
  EXPECT_EQ(out[1].cClusters, 3u);
  EXPECT_EQ(out[1].ullLcn, 50u);
}

TEST(MftDataRuns, SparseRunHasNoLcn) {
  const std::vector<BYTE> runs = {0x01, 10, 0x00}; // length-only header nibble (offset count 0) => sparse
  std::vector<MFT_DATA_RUN> out;
  ASSERT_TRUE(ParseDataRuns(runs.data(), static_cast<DWORD>(runs.size()), 0, out));

  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(out[0].bSparse);
  EXPECT_EQ(out[0].cClusters, 10u);
}

TEST(MftDataRuns, TruncatedRunRejected) {
  const std::vector<BYTE> runs = {0x11, 5}; // header claims a 1-byte offset follows; buffer ends first
  std::vector<MFT_DATA_RUN> out;
  EXPECT_FALSE(ParseDataRuns(runs.data(), static_cast<DWORD>(runs.size()), 0, out));
}

TEST(MftDataRuns, StartingVcnSeedsCumulativeVcn) {
  const std::vector<BYTE> runs = {0x11, 20, 5, 0x00};
  std::vector<MFT_DATA_RUN> out;
  ASSERT_TRUE(ParseDataRuns(runs.data(), static_cast<DWORD>(runs.size()), 1000, out));

  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].ullVcnStart, 1000u);
}

// ---------------------------------------------------------------------------
// Fixup (update sequence array) — docs/mft-direct-read-testcases.md M5
// ---------------------------------------------------------------------------

TEST(MftFixup, RoundTripRestoresExactOriginalSectorEndBytes) {
  SYNTHETIC_RECORD_OPTIONS opts;
  BUILT_RECORD record = BuildRecordUnfixed(opts);

  // Capture the genuine content sitting at each sector's last two bytes before any fixup
  // encoding touches them.
  const DWORD cSectors = opts.cbRecordSize / opts.cbSectorSize;
  std::vector<UINT16> rgOriginalLastWords(cSectors);
  for (DWORD i = 0; i < cSectors; ++i) {
    rgOriginalLastWords[i] = *reinterpret_cast<const UINT16 *>(record.buf.data() + (i + 1) * opts.cbSectorSize - sizeof(UINT16));
  }

  EncodeFixupInPlace(record, opts.cbSectorSize, 0x0001);

  // After write-time encoding, every sector-end word must read as the check value, not the
  // real content.
  for (DWORD i = 0; i < cSectors; ++i) {
    const UINT16 stamped = *reinterpret_cast<const UINT16 *>(record.buf.data() + (i + 1) * opts.cbSectorSize - sizeof(UINT16));
    EXPECT_EQ(stamped, 0x0001u) << "sector " << i;
  }

  ASSERT_TRUE(ApplyFixup(record.buf.data(), static_cast<DWORD>(record.buf.size()), opts.cbSectorSize));

  for (DWORD i = 0; i < cSectors; ++i) {
    const UINT16 restored = *reinterpret_cast<const UINT16 *>(record.buf.data() + (i + 1) * opts.cbSectorSize - sizeof(UINT16));
    EXPECT_EQ(restored, rgOriginalLastWords[i]) << "sector " << i;
  }

  // And parsing after fixup must recover the exact original name — no fixup-magic garbage
  // bytes leaking into it (the concrete acceptance criterion behind M5).
  MFT_PARSED_RECORD parsed;
  ASSERT_EQ(ParseFileRecord(record.buf.data(), static_cast<DWORD>(record.buf.size()), 42, parsed), MFT_PARSE_OK);
  EXPECT_EQ(std::wstring(parsed.pwszName, parsed.cchName), opts.wstrWin32Name);
}

TEST(MftFixup, MismatchedSignatureRejectedRatherThanSilentlyParsed) {
  SYNTHETIC_RECORD_OPTIONS opts;
  BUILT_RECORD record = BuildRecordUnfixed(opts);
  EncodeFixupInPlace(record, opts.cbSectorSize, 0x0001);

  // Corrupt the on-disk sector-end signature so it no longer matches USA[0] — simulates a torn
  // write. ApplyFixup must detect this and refuse, not restore some sectors and not others.
  UINT16 *pLastWord = reinterpret_cast<UINT16 *>(record.buf.data() + opts.cbSectorSize - sizeof(UINT16));
  *pLastWord = 0xDEAD;

  EXPECT_FALSE(ApplyFixup(record.buf.data(), static_cast<DWORD>(record.buf.size()), opts.cbSectorSize));
}

// ---------------------------------------------------------------------------
// FILE_RECORD_HEADER / attribute walk
// ---------------------------------------------------------------------------

TEST(MftParseFileRecord, ValidRecordExtractsFrnParentNameAndAttributes) {
  SYNTHETIC_RECORD_OPTIONS opts;
  opts.usnSequenceNumber = 7;
  opts.ullParentFrn = 5;
  opts.wstrWin32Name = L"report.docx";
  opts.dwDosAttributes = FILE_ATTRIBUTE_ARCHIVE;

  BUILT_RECORD record = BuildRecordUnfixed(opts);

  MFT_PARSED_RECORD parsed;
  const MFT_PARSE_RESULT result = ParseFileRecord(record.buf.data(), static_cast<DWORD>(record.buf.size()), 123, parsed);

  ASSERT_EQ(result, MFT_PARSE_OK);
  EXPECT_EQ(parsed.ullFrn, (static_cast<ULONGLONG>(7) << 48) | 123ull);
  EXPECT_EQ(parsed.ullParentFrn, 5u);
  EXPECT_EQ(std::wstring(parsed.pwszName, parsed.cchName), L"report.docx");
  EXPECT_FALSE(parsed.bIsDirectory);
  EXPECT_EQ(parsed.dwAttributes, static_cast<DWORD>(FILE_ATTRIBUTE_ARCHIVE));
}

TEST(MftParseFileRecord, DirectoryFlagFromHeaderNotFromFileNameAttribute) {
  SYNTHETIC_RECORD_OPTIONS opts;
  opts.headerFlags = MFT_RECORD_FLAG_IN_USE | MFT_RECORD_FLAG_IS_DIRECTORY;
  BUILT_RECORD record = BuildRecordUnfixed(opts);

  MFT_PARSED_RECORD parsed;
  ASSERT_EQ(ParseFileRecord(record.buf.data(), static_cast<DWORD>(record.buf.size()), 1, parsed), MFT_PARSE_OK);
  EXPECT_TRUE(parsed.bIsDirectory);
}

TEST(MftParseFileRecord, NotInUseRecordSkipped) {
  // M4: in-use bit clear (deleted file / never-reused slot).
  SYNTHETIC_RECORD_OPTIONS opts;
  opts.headerFlags = 0;
  BUILT_RECORD record = BuildRecordUnfixed(opts);

  MFT_PARSED_RECORD parsed;
  EXPECT_EQ(ParseFileRecord(record.buf.data(), static_cast<DWORD>(record.buf.size()), 1, parsed), MFT_PARSE_NOT_IN_USE);
}

TEST(MftParseFileRecord, ExtensionRecordSkipped) {
  // M6 (partial): base file record reference != 0 => overflow attribute container, not a file.
  SYNTHETIC_RECORD_OPTIONS opts;
  opts.baseFileRecordReference = 0x0001000000000032ull;
  BUILT_RECORD record = BuildRecordUnfixed(opts);

  MFT_PARSED_RECORD parsed;
  EXPECT_EQ(ParseFileRecord(record.buf.data(), static_cast<DWORD>(record.buf.size()), 1, parsed), MFT_PARSE_EXTENSION_RECORD);
}

TEST(MftParseFileRecord, NoFileNameAttributeReportsGap) {
  // M6 gap surface: no usable $FILE_NAME found (e.g. it lives in an unsupported $ATTRIBUTE_LIST
  // extension). Must be a clean, distinguishable result — not a crash, not a made-up name.
  SYNTHETIC_RECORD_OPTIONS opts;
  opts.bIncludeFileName = false;
  BUILT_RECORD record = BuildRecordUnfixed(opts);

  MFT_PARSED_RECORD parsed;
  EXPECT_EQ(ParseFileRecord(record.buf.data(), static_cast<DWORD>(record.buf.size()), 1, parsed), MFT_PARSE_NO_FILE_NAME);
}

TEST(MftParseFileRecord, BadSignatureIsMalformedNotCrash) {
  // M10: an unrecognized/corrupt structure must trigger a graceful, distinguishable failure.
  SYNTHETIC_RECORD_OPTIONS opts;
  BUILT_RECORD record = BuildRecordUnfixed(opts);
  reinterpret_cast<FILE_RECORD_HEADER *>(record.buf.data())->Signature = 0x44414142; // 'BAAD'

  MFT_PARSED_RECORD parsed;
  EXPECT_EQ(ParseFileRecord(record.buf.data(), static_cast<DWORD>(record.buf.size()), 1, parsed), MFT_PARSE_MALFORMED);
}

TEST(MftParseFileRecord, TruncatedBufferIsMalformedNotCrash) {
  // M10: a buffer far too small to hold even the fixed header must not be read out of bounds.
  std::vector<BYTE> tiny(8, 0);
  MFT_PARSED_RECORD parsed;
  EXPECT_EQ(ParseFileRecord(tiny.data(), static_cast<DWORD>(tiny.size()), 1, parsed), MFT_PARSE_MALFORMED);
}

TEST(MftParseFileRecord, PrefersWin32NamespaceOverDosNamespaceDuplicate) {
  // README: "a file can have MULTIPLE $FILE_NAME attributes for short+long name pairs; per
  // NTFS convention prefer the Win32 or Win32+DOS namespace one for display, skip pure-DOS
  // -namespace duplicates so you don't produce two nodes for one file."
  SYNTHETIC_RECORD_OPTIONS opts;
  opts.wstrWin32Name = L"Program Files";
  opts.bIncludeDosDuplicate = true;
  opts.wstrDosName = L"PROGRA~1";
  BUILT_RECORD record = BuildRecordUnfixed(opts);

  MFT_PARSED_RECORD parsed;
  ASSERT_EQ(ParseFileRecord(record.buf.data(), static_cast<DWORD>(record.buf.size()), 1, parsed), MFT_PARSE_OK);

  // ParseFileRecord surfaces exactly one chosen name per record (the caller never sees the DOS
  // duplicate at all), and it must be the long Win32 name, not the 8.3 short name.
  EXPECT_EQ(std::wstring(parsed.pwszName, parsed.cchName), L"Program Files");
}

// ---------------------------------------------------------------------------
// CIndexStore::UpsertFromMftRecord — the new ingestion seam CVolume::DoLoad feeds from
// ntfs::CMftReader's callback, mirroring UpsertFromRecord without a fabricated USN_RECORD_V2.
// ---------------------------------------------------------------------------

TEST(IndexStoreMftIngestion, BuildsSearchableTreeFromMftDerivedFields) {
  index::CIndexStore store;
  store.BeginBulkLoad();

  ASSERT_TRUE(store.UpsertFromMftRecord(100, 5, L"Projects", 8, true, FILE_ATTRIBUTE_DIRECTORY));
  ASSERT_TRUE(store.UpsertFromMftRecord(101, 100, L"notes.txt", 9, false, FILE_ATTRIBUTE_NORMAL));
  store.FinalizeInitialLoad();

  const index::INDEX_STATS stats = store.GetStats();
  EXPECT_EQ(stats.m_cNodes, 2u);
  EXPECT_EQ(stats.m_cUnresolvedParents, 0u);

  std::vector<UINT32> results;
  store.SearchUtf8("notes", results, 0);
  ASSERT_EQ(results.size(), 1u);

  std::vector<char> rgPath;
  ASSERT_TRUE(store.MaterializePathUtf8(results[0], rgPath));
  EXPECT_EQ(std::string(rgPath.begin(), rgPath.end()), "Projects\\notes.txt");
}

TEST(IndexStoreMftIngestion, DotAndDotDotNamesAreSkipped) {
  index::CIndexStore store;
  store.BeginBulkLoad();

  EXPECT_FALSE(store.UpsertFromMftRecord(5, 5, L".", 1, true, FILE_ATTRIBUTE_DIRECTORY));
  EXPECT_FALSE(store.UpsertFromMftRecord(6, 5, L"..", 2, true, FILE_ATTRIBUTE_DIRECTORY));

  store.FinalizeInitialLoad();
  EXPECT_EQ(store.GetStats().m_cNodes, 0u);
}

TEST(IndexStoreMftIngestion, DirectoryFlagPropagatesToIndexNode) {
  index::CIndexStore store;
  store.BeginBulkLoad();
  ASSERT_TRUE(store.UpsertFromMftRecord(200, 5, L"Downloads", 9, true, FILE_ATTRIBUTE_DIRECTORY));
  store.FinalizeInitialLoad();

  std::vector<UINT32> results;
  store.SearchUtf8("Downloads", results, 0);
  ASSERT_EQ(results.size(), 1u);

  index::CParsedQuery plan;
  ASSERT_TRUE(index::ParseSearchQuery(L"parent:c:\\Downloads", plan));
  store.ResolveParsedQuery(L'C', plan);
  EXPECT_EQ(plan.m_pathScope, index::PATH_SCOPE_SUBTREE);
}
