#pragma once

#include "core/ntfs_types.h"
#include "ntfs/mft_record.h"
#include "ntfs/ntfs_volume_handle.h"

#include <functional>
#include <vector>

namespace ntfs {

// (FRN, parentFRN, name, isDirectory, attributes) — shaped so CVolume::DoLoad can feed each
// call into CIndexStore the same way it currently consumes USN_RECORD_V2 rows. wszName points
// into a reader-owned buffer valid only for the duration of the call (mirrors CUsnEnumerator's
// own zero-copy USN_RECORD_V2 convention) — copy out anything you need to keep.
using MftRecordCallback = std::function<void(ULONGLONG ullFrn, ULONGLONG ullParentFrn, LPCWSTR wszName, USHORT cchName, bool bIsDirectory, DWORD dwAttributes, ULONGLONG ullFileSize, ULONGLONG ullModifiedTime)>;

// Loads the initial index by parsing the NTFS Master File Table directly (README §1) instead
// of FSCTL_ENUM_USN_DATA. Bulk-reads $MFT's own data runs from its record-0 $DATA attribute
// and walks every in-use, non-extension, non-reserved record — the "resolve $MFT's own data
// runs and read clusters directly" approach (README's "approach (a)"), chosen over per-record
// FSCTL_GET_NTFS_FILE_RECORD because that per-FRN approach issues one DeviceIoControl per file
// and would not exercise the cold-start performance goal this feature exists for.
//
// Read-only by construction: every I/O call this class makes is ReadFile or SetFilePointerEx
// against the already-open volume handle supplied via SetVolumeHandle (opened once, read-only
// intent, by CVolume/CNtfsVolumeHandle) — no DeviceIoControl call anywhere in this class, no
// second handle opened, no write of any kind (M11).
//
// ReadAll() returns FALSE on ANY failure — boot sector that doesn't validate, $MFT run
// resolution failure, a read error partway through, an implausible record count — so the
// caller (CVolume::DoLoad) can fall back to CUsnEnumerator::EnumerateAll() automatically
// (M10). A malformed *individual* record (bad fixup, not in use, extension record, no usable
// $FILE_NAME) is instead just skipped and the walk continues, since one bad MFT slot must
// degrade the index, not the whole load.
class CMftReader {
public:
  CMftReader();

  void SetVolumeHandle(CNtfsVolumeHandle *pVolume);
  void SetRecordCallback(MftRecordCallback callback);
  void SetErrorCallback(core::VolumeErrorCallback callback);

  BOOL ReadAll();
  DWORD GetRecordCount() const;

private:
  BOOL ReadBytesAt(ULONGLONG ullByteOffset, BYTE *pBuffer, DWORD cbBuffer);
  BOOL ReadMftSelfRecord(std::vector<BYTE> &rgRecordBuffer);
  BOOL ResolveMftRuns(const std::vector<BYTE> &rgSelfRecord, std::vector<MFT_DATA_RUN> &rgRuns, UINT64 &cTotalRecords);
  BOOL ReadLogicalMftBytes(UINT64 ullByteOffset, DWORD cbLength, BYTE *pOut);
  BOOL LocateVcn(UINT64 ullVcn, const MFT_DATA_RUN *&pOutRun) const;
  void ReportError(DWORD dwError, LPCWSTR wszMessage);

  CNtfsVolumeHandle *m_pVolume;
  MftRecordCallback m_fnRecord;
  core::VolumeErrorCallback m_fnError;
  DWORD m_dwRecordCount;

  NTFS_BOOT_PARAMS m_bootParams;
  std::vector<MFT_DATA_RUN> m_rgMftRuns;

  // Single-cluster read cache: consecutive MFT records typically share a cluster (record size
  // normally divides cluster size evenly, e.g. 1024 into 4096), so caching the last physical
  // cluster avoids re-issuing a ReadFile syscall for every single record.
  std::vector<BYTE> m_rgClusterCache;
  UINT64 m_ullCachedLcn;
  bool m_bHaveCachedCluster;
};

} // namespace ntfs
