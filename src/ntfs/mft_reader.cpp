#include "ntfs/mft_reader.h"

#include <cstring>

namespace ntfs {

namespace {

const ATTRIBUTE_RECORD_HEADER *FindAttribute(const BYTE *pRecord, DWORD cbUsedSize, DWORD firstAttrOffset, UINT32 typeCode) {
  DWORD offset = firstAttrOffset;

  while (offset + sizeof(ATTRIBUTE_RECORD_HEADER) <= cbUsedSize) {
    const ATTRIBUTE_RECORD_HEADER *pAttr = reinterpret_cast<const ATTRIBUTE_RECORD_HEADER *>(pRecord + offset);

    if (pAttr->TypeCode == ATTR_TYPE_END) {
      break;
    }

    if (pAttr->Length < sizeof(ATTRIBUTE_RECORD_HEADER) || pAttr->Length > cbUsedSize - offset) {
      break;
    }

    if (pAttr->TypeCode == typeCode) {
      return pAttr;
    }

    offset += pAttr->Length;
  }

  return nullptr;
}

} // namespace

CMftReader::CMftReader() : m_pVolume(nullptr), m_dwRecordCount(0), m_ullCachedLcn(0), m_bHaveCachedCluster(false) {}

void CMftReader::SetVolumeHandle(CNtfsVolumeHandle *pVolume) {
  m_pVolume = pVolume;
}

void CMftReader::SetRecordCallback(MftRecordCallback callback) {
  m_fnRecord = std::move(callback);
}

void CMftReader::SetErrorCallback(core::VolumeErrorCallback callback) {
  m_fnError = std::move(callback);
}

DWORD CMftReader::GetRecordCount() const {
  return m_dwRecordCount;
}

void CMftReader::ReportError(DWORD dwError, LPCWSTR wszMessage) {
  if (m_fnError) {
    m_fnError(dwError, wszMessage);
  }
}

BOOL CMftReader::ReadBytesAt(ULONGLONG ullByteOffset, BYTE *pBuffer, DWORD cbBuffer) {
  if (m_pVolume == nullptr || !m_pVolume->IsOpen()) {
    return FALSE;
  }

  HANDLE hVolume = m_pVolume->GetHandle();

  LARGE_INTEGER liOffset;
  liOffset.QuadPart = static_cast<LONGLONG>(ullByteOffset);
  if (!SetFilePointerEx(hVolume, liOffset, nullptr, FILE_BEGIN)) {
    ReportError(GetLastError(), L"SetFilePointerEx failed while reading the volume for direct MFT parsing");
    return FALSE;
  }

  DWORD cbRead = 0;
  if (!ReadFile(hVolume, pBuffer, cbBuffer, &cbRead, nullptr) || cbRead != cbBuffer) {
    ReportError(GetLastError(), L"ReadFile failed while reading the volume for direct MFT parsing");
    return FALSE;
  }

  return TRUE;
}

BOOL CMftReader::ReadMftSelfRecord(std::vector<BYTE> &rgRecordBuffer) {
  BYTE rgBootSector[512];
  if (!ReadBytesAt(0, rgBootSector, sizeof(rgBootSector))) {
    return FALSE;
  }

  if (!ParseBootSector(rgBootSector, sizeof(rgBootSector), m_bootParams)) {
    ReportError(0, L"Boot sector failed validation (OEM id / 0xAA55 marker) — not treating this volume as directly parseable NTFS");
    return FALSE;
  }

  const ULONGLONG ullByteOffset = m_bootParams.ullMftStartLcn * m_bootParams.cbCluster;
  rgRecordBuffer.assign(m_bootParams.cbMftRecordSize, 0);

  if (!ReadBytesAt(ullByteOffset, rgRecordBuffer.data(), m_bootParams.cbMftRecordSize)) {
    return FALSE;
  }

  if (!ApplyFixup(rgRecordBuffer.data(), m_bootParams.cbMftRecordSize, m_bootParams.cbBytesPerSector)) {
    ReportError(0, L"MFT record 0 ($MFT itself) failed fixup validation");
    return FALSE;
  }

  return TRUE;
}

BOOL CMftReader::ResolveMftRuns(const std::vector<BYTE> &rgSelfRecord, std::vector<MFT_DATA_RUN> &rgRuns, UINT64 &cTotalRecords) {
  const BYTE *pRecord = rgSelfRecord.data();
  const DWORD cbRecord = static_cast<DWORD>(rgSelfRecord.size());
  const FILE_RECORD_HEADER *pHeader = reinterpret_cast<const FILE_RECORD_HEADER *>(pRecord);
  const DWORD cbUsedSize = pHeader->UsedSize <= cbRecord ? pHeader->UsedSize : cbRecord;

  if (pHeader->FirstAttributeOffset < sizeof(FILE_RECORD_HEADER) || pHeader->FirstAttributeOffset >= cbUsedSize) {
    ReportError(0, L"MFT self-record has an invalid first-attribute offset");
    return FALSE;
  }

  if (FindAttribute(pRecord, cbUsedSize, pHeader->FirstAttributeOffset, ATTR_TYPE_ATTRIBUTE_LIST) != nullptr) {
    // M6 (documented gap): $MFT's own metadata spilled into an $ATTRIBUTE_LIST — only happens
    // on enormous/heavily fragmented MFTs. Fully supporting this means resolving $DATA run
    // fragments listed in extension records, which this first cut does not implement. Fail
    // safely so the caller falls back to USN enumeration instead of reading a truncated $MFT
    // and silently under-indexing the volume.
    ReportError(0, L"MFT self-record uses an $ATTRIBUTE_LIST for its $DATA attribute (unsupported in this version)");
    return FALSE;
  }

  const ATTRIBUTE_RECORD_HEADER *pData = FindAttribute(pRecord, cbUsedSize, pHeader->FirstAttributeOffset, ATTR_TYPE_DATA);
  if (pData == nullptr || pData->NonResident == 0) {
    ReportError(0, L"MFT $DATA attribute missing or unexpectedly resident");
    return FALSE;
  }

  if (pData->Length < sizeof(ATTRIBUTE_RECORD_HEADER) + sizeof(ATTRIBUTE_NONRESIDENT_TAIL)) {
    ReportError(0, L"MFT $DATA non-resident header truncated");
    return FALSE;
  }

  const ATTRIBUTE_NONRESIDENT_TAIL *pTail = reinterpret_cast<const ATTRIBUTE_NONRESIDENT_TAIL *>(reinterpret_cast<const BYTE *>(pData) + sizeof(ATTRIBUTE_RECORD_HEADER));

  const DWORD runsOffset = pTail->DataRunsOffset;
  if (runsOffset >= pData->Length) {
    ReportError(0, L"MFT $DATA run list offset out of range");
    return FALSE;
  }

  const BYTE *pRuns = reinterpret_cast<const BYTE *>(pData) + runsOffset;
  const DWORD cbRuns = pData->Length - runsOffset;

  if (!ParseDataRuns(pRuns, cbRuns, pTail->StartingVcn, rgRuns) || rgRuns.empty()) {
    ReportError(0, L"MFT $DATA run list failed to parse");
    return FALSE;
  }

  if (m_bootParams.cbMftRecordSize == 0) {
    return FALSE;
  }

  cTotalRecords = pTail->RealSize / m_bootParams.cbMftRecordSize;
  if (cTotalRecords == 0) {
    ReportError(0, L"MFT reports zero records");
    return FALSE;
  }

  return TRUE;
}

BOOL CMftReader::LocateVcn(UINT64 ullVcn, const MFT_DATA_RUN *&pOutRun) const {
  if (m_rgMftRuns.empty()) {
    return FALSE;
  }

  size_t lo = 0;
  size_t hi = m_rgMftRuns.size();

  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const MFT_DATA_RUN &run = m_rgMftRuns[mid];

    if (ullVcn < run.ullVcnStart) {
      hi = mid;
    } else if (ullVcn >= run.ullVcnStart + run.cClusters) {
      lo = mid + 1;
    } else {
      pOutRun = &m_rgMftRuns[mid];
      return TRUE;
    }
  }

  return FALSE;
}

BOOL CMftReader::ReadLogicalMftBytes(UINT64 ullByteOffset, DWORD cbLength, BYTE *pOut) {
  const UINT32 cbCluster = m_bootParams.cbCluster;
  if (cbCluster == 0) {
    return FALSE;
  }

  DWORD cbFilled = 0;

  while (cbFilled < cbLength) {
    const UINT64 ullAbsByte = ullByteOffset + cbFilled;
    const UINT64 ullVcn = ullAbsByte / cbCluster;
    const UINT32 offsetInCluster = static_cast<UINT32>(ullAbsByte % cbCluster);

    const MFT_DATA_RUN *pRun = nullptr;
    if (!LocateVcn(ullVcn, pRun)) {
      return FALSE; // past the end of the resolved $MFT data runs
    }

    const DWORD cbAvailableInCluster = cbCluster - offsetInCluster;
    const DWORD cbRemaining = cbLength - cbFilled;
    const DWORD cbToCopy = cbRemaining < cbAvailableInCluster ? cbRemaining : cbAvailableInCluster;

    if (pRun->bSparse) {
      memset(pOut + cbFilled, 0, cbToCopy);
    } else {
      const UINT64 ullLcn = pRun->ullLcn + (ullVcn - pRun->ullVcnStart);

      if (!m_bHaveCachedCluster || m_ullCachedLcn != ullLcn) {
        m_rgClusterCache.assign(cbCluster, 0);
        const ULONGLONG ullClusterByteOffset = ullLcn * cbCluster;

        if (!ReadBytesAt(ullClusterByteOffset, m_rgClusterCache.data(), cbCluster)) {
          m_bHaveCachedCluster = false;
          return FALSE;
        }

        m_ullCachedLcn = ullLcn;
        m_bHaveCachedCluster = true;
      }

      memcpy(pOut + cbFilled, m_rgClusterCache.data() + offsetInCluster, cbToCopy);
    }

    cbFilled += cbToCopy;
  }

  return TRUE;
}

BOOL CMftReader::ReadAll() {
  m_dwRecordCount = 0;
  m_rgMftRuns.clear();
  m_bHaveCachedCluster = false;

  if (m_pVolume == nullptr || !m_pVolume->IsOpen()) {
    return FALSE;
  }

  std::vector<BYTE> rgSelfRecord;
  if (!ReadMftSelfRecord(rgSelfRecord)) {
    return FALSE;
  }

  UINT64 cTotalRecords = 0;
  if (!ResolveMftRuns(rgSelfRecord, m_rgMftRuns, cTotalRecords)) {
    return FALSE;
  }

  // Sanity bound: an implausible record count (corrupt parse of RealSize, or a run list that
  // resolved to nonsense) must fail cleanly here rather than turn into an effectively unbounded
  // read loop — M10 is about failing safely, not just failing eventually.
  constexpr UINT64 kMaxPlausibleRecords = 500ull * 1000ull * 1000ull;
  if (cTotalRecords > kMaxPlausibleRecords) {
    ReportError(0, L"MFT record count implausibly large; treating as a parse failure");
    return FALSE;
  }

  std::vector<BYTE> rgRecordBuffer(m_bootParams.cbMftRecordSize);

  for (UINT64 recordIndex = MFT_FIRST_NON_RESERVED_RECORD; recordIndex < cTotalRecords; ++recordIndex) {
    const UINT64 ullByteOffset = recordIndex * m_bootParams.cbMftRecordSize;

    if (!ReadLogicalMftBytes(ullByteOffset, m_bootParams.cbMftRecordSize, rgRecordBuffer.data())) {
      // A read failure partway through (real I/O error, or a byte range the resolved run list
      // doesn't cover) aborts the whole direct-MFT load: this feature's safety contract prefers
      // falling back to the proven USN enumeration path over shipping a partial/truncated index.
      ReportError(0, L"Failed to read an MFT record's clusters; aborting direct MFT load");
      return FALSE;
    }

    const FILE_RECORD_HEADER *pRawHeader = reinterpret_cast<const FILE_RECORD_HEADER *>(rgRecordBuffer.data());
    if (pRawHeader->Signature != MFT_SIGNATURE_FILE) {
      // Never-allocated slot (all zero, or otherwise not a FILE record at all) — not the same
      // as a used-then-deleted record, but the outcome is identical: skip it (M4).
      continue;
    }

    if (!ApplyFixup(rgRecordBuffer.data(), m_bootParams.cbMftRecordSize, m_bootParams.cbBytesPerSector)) {
      // M5 / M10: this one record is torn/corrupt. Skip just this record — one bad slot
      // degrades the index, it must never crash or misparse garbage bytes as a name.
      continue;
    }

    MFT_PARSED_RECORD parsed;
    const MFT_PARSE_RESULT result = ParseFileRecord(rgRecordBuffer.data(), m_bootParams.cbMftRecordSize, recordIndex, parsed);

    if (result != MFT_PARSE_OK) {
      continue;
    }

    if (m_fnRecord) {
      m_fnRecord(parsed.ullFrn, parsed.ullParentFrn, parsed.pwszName, parsed.cchName, parsed.bIsDirectory, parsed.dwAttributes, parsed.ullFileSize, parsed.ullModifiedTime);
    }

    ++m_dwRecordCount;
  }

  return TRUE;
}

} // namespace ntfs
