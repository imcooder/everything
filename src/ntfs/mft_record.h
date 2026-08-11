#pragma once

#include "core/platform.h"

#include <cstdint>
#include <vector>

namespace ntfs {

// ---------------------------------------------------------------------------
// On-disk NTFS structures (README §1 "Direct MFT access instead of recursive
// directory enumeration"). Every struct here mirrors real on-disk byte layout;
// see docs/mft-direct-read-testcases.md for the acceptance checklist these
// parse routines are built against.
//
// This translation unit is pure parsing: it never touches a HANDLE, never
// issues I/O, and has no Win32 volume dependency. That is deliberate — it is
// the part unit-testable with hand-crafted synthetic buffers (M4, M5, M6,
// M10). See ntfs/mft_reader.h for the layer that turns real volume bytes into
// the inputs these functions take.
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

// NTFS boot sector ($Boot), sector 0 of the volume. Layout source: NTFS on-disk
// structure documentation (ntfs.com "Boot Sector"); field names follow that reference.
struct NTFS_BOOT_SECTOR {
  BYTE Jump[3];
  BYTE OemId[8];
  UINT16 BytesPerSector;
  BYTE SectorsPerCluster;
  UINT16 ReservedSectors;
  BYTE Unused1[3];
  UINT16 Unused2;
  BYTE MediaDescriptor;
  UINT16 Unused3;
  UINT16 SectorsPerTrack;
  UINT16 NumberOfHeads;
  UINT32 HiddenSectors;
  UINT32 Unused4;
  UINT32 Unused5;
  UINT64 TotalSectors;
  UINT64 MftStartLcn;
  UINT64 Mft2StartLcn;
  INT8 ClustersPerFileRecordSegment; // signed: >0 = N clusters, <0 = 2^|N| bytes
  BYTE Unused6[3];
  INT8 ClustersPerIndexBuffer; // same signed encoding
  BYTE Unused7[3];
  UINT64 VolumeSerialNumber;
  UINT32 Checksum;
  BYTE BootCode[426];
  UINT16 EndMarker; // 0xAA55
};

// FILE_RECORD_HEADER: the fixed header at the start of every MFT record ("FILE" record).
struct FILE_RECORD_HEADER {
  UINT32 Signature; // 'FILE' (0x454C4946 little-endian), or 'BAAD' for a known-corrupt record
  UINT16 UpdateSequenceArrayOffset;
  UINT16 UpdateSequenceArraySize; // word count: 1 (USN) + one word per sector
  UINT64 LogFileSequenceNumber;
  UINT16 SequenceNumber; // incremented each time this record slot is reused
  UINT16 HardLinkCount;
  UINT16 FirstAttributeOffset;
  UINT16 Flags; // bit 0: in use; bit 1: directory
  UINT32 UsedSize;
  UINT32 AllocatedSize;
  UINT64 BaseFileRecordReference; // non-zero => this is an extension record
  UINT16 NextAttributeId;
};

// Common attribute header shared by resident and non-resident attributes.
struct ATTRIBUTE_RECORD_HEADER {
  UINT32 TypeCode; // 0xFFFFFFFF marks the end of the attribute list
  UINT32 Length;   // total length of this attribute record, including this header
  BYTE NonResident;
  BYTE NameLength;
  UINT16 NameOffset;
  UINT16 Flags;
  UINT16 AttributeId;
  // Resident layout continues:
  //   UINT32 ContentLength; UINT16 ContentOffset; BYTE Indexed; BYTE Padding;
  // Non-resident layout continues:
  //   UINT64 StartingVcn; UINT64 LastVcn; UINT16 DataRunsOffset; UINT16 CompressionUnit;
  //   UINT32 Padding; UINT64 AllocatedSize; UINT64 RealSize; UINT64 InitializedSize;
  // Read via the accessor helpers below rather than a union so this header stays a flat,
  // easy-to-read-off-the-wire struct matching only the always-present bytes.
};

struct ATTRIBUTE_RESIDENT_TAIL {
  UINT32 ContentLength;
  UINT16 ContentOffset;
  BYTE Indexed;
  BYTE Padding;
};

struct ATTRIBUTE_NONRESIDENT_TAIL {
  UINT64 StartingVcn;
  UINT64 LastVcn;
  UINT16 DataRunsOffset;
  UINT16 CompressionUnit;
  UINT32 Padding;
  UINT64 AllocatedSize;
  UINT64 RealSize;
  UINT64 InitializedSize;
};

// $STANDARD_INFORMATION (type 0x10) resident content, leading fields only (this is the
// portion present since NTFS 1.0; the optional NTFS 3.0+ quota/USN tail is not needed here).
struct STANDARD_INFORMATION {
  UINT64 CreationTime;
  UINT64 AlteredTime;
  UINT64 MftChangedTime;
  UINT64 ReadTime;
  UINT32 DosFileAttributes;
  UINT32 MaximumVersions;
  UINT32 VersionNumber;
  UINT32 ClassId;
};

// $FILE_NAME (type 0x30) resident content header; the filename (UTF-16) follows immediately.
struct FILE_NAME_ATTRIBUTE {
  UINT64 ParentDirectory; // FRN of the parent directory (record number + sequence number)
  UINT64 CreationTime;
  UINT64 ModificationTime;
  UINT64 MftChangedTime;
  UINT64 ReadTime;
  UINT64 AllocatedSize;
  UINT64 RealSize;
  UINT32 Flags;
  UINT32 ReparseTag;
  BYTE FileNameLength; // in UTF-16 characters
  BYTE FileNameNamespace;
  // WCHAR FileName[FileNameLength] follows.
};

#pragma pack(pop)

static_assert(sizeof(NTFS_BOOT_SECTOR) == 512, "NTFS boot sector must be exactly one 512-byte layout");
static_assert(sizeof(FILE_RECORD_HEADER) == 42, "FILE_RECORD_HEADER layout changed unexpectedly");
static_assert(sizeof(ATTRIBUTE_RECORD_HEADER) == 16, "ATTRIBUTE_RECORD_HEADER common header layout changed unexpectedly");
static_assert(sizeof(STANDARD_INFORMATION) == 48, "STANDARD_INFORMATION layout changed unexpectedly");
static_assert(sizeof(FILE_NAME_ATTRIBUTE) == 66, "FILE_NAME_ATTRIBUTE layout changed unexpectedly");

constexpr UINT32 MFT_SIGNATURE_FILE = 0x454C4946; // "FILE" little-endian
constexpr UINT16 MFT_RECORD_FLAG_IN_USE = 0x0001;
constexpr UINT16 MFT_RECORD_FLAG_IS_DIRECTORY = 0x0002;

constexpr UINT32 ATTR_TYPE_STANDARD_INFORMATION = 0x10;
constexpr UINT32 ATTR_TYPE_ATTRIBUTE_LIST = 0x20;
constexpr UINT32 ATTR_TYPE_FILE_NAME = 0x30;
constexpr UINT32 ATTR_TYPE_DATA = 0x80;
constexpr UINT32 ATTR_TYPE_END = 0xFFFFFFFF;

enum FILE_NAME_NAMESPACE : BYTE { FILE_NAME_NAMESPACE_POSIX = 0, FILE_NAME_NAMESPACE_WIN32 = 1, FILE_NAME_NAMESPACE_DOS = 2, FILE_NAME_NAMESPACE_WIN32_AND_DOS = 3 };

// Reserved MFT record indices 0-15 (README/testcases M7): $MFT, $MFTMirr, $LogFile, $Volume,
// $AttrDef, root directory (5, the only one of the fifteen actually surfaced as a node — but
// implicitly, via INDEX_ROOT_PARENT, not as an explicit index entry; see mft_reader.cpp),
// $Bitmap, $Boot, $BadClus, $Secure, $UpCase, $Extend, and two reserved slots.
constexpr UINT64 MFT_FIRST_NON_RESERVED_RECORD = 16;
constexpr UINT64 MFT_ROOT_DIRECTORY_RECORD = 5;

// Extracts the volume geometry / $MFT bootstrap location out of a raw 512-byte boot sector.
// Validates the OEM id ("NTFS    ") and the 0xAA55 end-of-sector marker; a boot sector that
// fails either check is almost certainly not NTFS (or we mis-sized the read) and must not be
// trusted further (M10 fallback territory).
struct NTFS_BOOT_PARAMS {
  UINT32 cbBytesPerSector = 0;
  UINT32 cbCluster = 0;
  UINT64 ullMftStartLcn = 0;
  UINT32 cbMftRecordSize = 0;
  UINT32 cbIndexRecordSize = 0;
};

bool ParseBootSector(const BYTE *pSector, DWORD cbSector, NTFS_BOOT_PARAMS &params);

// Encodes the signed-byte "N clusters or 2^|N| bytes" rule shared by ClustersPerFileRecordSegment
// and ClustersPerIndexBuffer (README: "a signed byte field meaning either 'N clusters' or
// '2^|N| bytes' depending on sign").
UINT32 ResolveRecordOrIndexSize(INT8 nEncoded, UINT32 cbCluster);

// Applies the update sequence array (fixup) in place over pRecord (M5). Verifies every sector's
// last two bytes equal the USA's check value before restoring the real bytes; returns false
// (without modifying anything further) the instant a sector's signature does not match, since
// that means the record is torn/corrupt and must not be parsed any further.
bool ApplyFixup(BYTE *pRecord, DWORD cbRecord, UINT32 cbBytesPerSector);

// A single decoded, cumulative-VCN-resolved data run (README "data runs (run-length-encoded
// cluster lists)"). m_bSparse runs carry no physical allocation (all-zero logical content).
struct MFT_DATA_RUN {
  UINT64 ullVcnStart = 0;
  UINT64 cClusters = 0;
  UINT64 ullLcn = 0;
  bool bSparse = false;
};

// Decodes a non-resident attribute's data-run byte stream (attribute content starting at
// DataRunsOffset) into a run list with cumulative starting VCNs seeded from ullStartingVcn
// (the non-resident attribute header's own StartingVcn field). Returns false on a malformed
// run stream (truncated byte counts, etc.) rather than guessing.
bool ParseDataRuns(const BYTE *pRuns, DWORD cbRuns, UINT64 ullStartingVcn, std::vector<MFT_DATA_RUN> &rgRuns);

enum MFT_PARSE_RESULT {
  MFT_PARSE_OK = 0,
  MFT_PARSE_NOT_IN_USE,       // M4: deleted/free slot, header in-use bit clear
  MFT_PARSE_EXTENSION_RECORD, // M6 (partial): base file record reference != 0, not a standalone file
  MFT_PARSE_NO_FILE_NAME,     // no usable $FILE_NAME attribute found (e.g. lives in an $ATTRIBUTE_LIST — M6 gap)
  MFT_PARSE_MALFORMED,        // bad signature / fixup / truncated attribute walk (M10 trigger)
};

// A single live, in-use, base (non-extension) MFT record's extracted identity. Name points
// into the caller-owned record buffer (mirrors CUsnEnumerator/USN_RECORD_V2's own zero-copy
// convention) and is only valid while that buffer is alive.
struct MFT_PARSED_RECORD {
  ULONGLONG ullFrn = 0;
  ULONGLONG ullParentFrn = 0;
  const WCHAR *pwszName = nullptr;
  USHORT cchName = 0;
  bool bIsDirectory = false;
  DWORD dwAttributes = 0;
  // From the same $FILE_NAME attribute the name/parent came from (README: real file size and
  // last-modified time are already present there, so no extra attribute walk is needed).
  // ullFileSize is always 0 for a directory (NTFS doesn't track a meaningful "size" for one).
  ULONGLONG ullFileSize = 0;
  ULONGLONG ullModifiedTime = 0; // FILETIME (100ns intervals since 1601-01-01), 0 if unknown
};

// Parses one already fixed-up MFT record (see ApplyFixup) at record index ullRecordIndex.
// Walks $STANDARD_INFORMATION and every $FILE_NAME attribute, preferring the Win32 or
// Win32+DOS namespace name for display and skipping pure-DOS-namespace duplicates (README:
// "a file can have MULTIPLE $FILE_NAME attributes ... skip pure-DOS-namespace duplicates").
MFT_PARSE_RESULT ParseFileRecord(const BYTE *pRecord, DWORD cbRecord, UINT64 ullRecordIndex, MFT_PARSED_RECORD &out);

} // namespace ntfs
