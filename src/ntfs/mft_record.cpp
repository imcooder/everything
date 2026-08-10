#include "ntfs/mft_record.h"

#include <cstring>

namespace ntfs {

namespace {

int FileNameNamespacePriority(BYTE bNamespace) {
  // Lower is better. Prefer Win32 (or Win32+DOS, which is also a full-length name usable for
  // display), then POSIX, then last a pure-DOS-namespace short name (README: "skip
  // pure-DOS-namespace duplicates so you don't produce two nodes for one file").
  switch (bNamespace) {
  case FILE_NAME_NAMESPACE_WIN32:
    return 0;
  case FILE_NAME_NAMESPACE_WIN32_AND_DOS:
    return 1;
  case FILE_NAME_NAMESPACE_POSIX:
    return 2;
  case FILE_NAME_NAMESPACE_DOS:
    return 3;
  default:
    return 4;
  }
}

bool ResidentTailFits(const ATTRIBUTE_RECORD_HEADER *pAttr) {
  return pAttr->Length >= sizeof(ATTRIBUTE_RECORD_HEADER) + sizeof(ATTRIBUTE_RESIDENT_TAIL);
}

} // namespace

bool ParseBootSector(const BYTE *pSector, DWORD cbSector, NTFS_BOOT_PARAMS &params) {
  params = NTFS_BOOT_PARAMS{};

  if (pSector == nullptr || cbSector < sizeof(NTFS_BOOT_SECTOR)) {
    return false;
  }

  const NTFS_BOOT_SECTOR *pBoot = reinterpret_cast<const NTFS_BOOT_SECTOR *>(pSector);

  static const BYTE kOemId[8] = {'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '};
  if (memcmp(pBoot->OemId, kOemId, sizeof(kOemId)) != 0) {
    return false;
  }

  if (pBoot->EndMarker != 0xAA55) {
    return false;
  }

  if (pBoot->BytesPerSector == 0 || pBoot->SectorsPerCluster == 0) {
    return false;
  }

  params.cbBytesPerSector = pBoot->BytesPerSector;
  params.cbCluster = static_cast<UINT32>(pBoot->BytesPerSector) * static_cast<UINT32>(pBoot->SectorsPerCluster);
  params.ullMftStartLcn = pBoot->MftStartLcn;
  params.cbMftRecordSize = ResolveRecordOrIndexSize(pBoot->ClustersPerFileRecordSegment, params.cbCluster);
  params.cbIndexRecordSize = ResolveRecordOrIndexSize(pBoot->ClustersPerIndexBuffer, params.cbCluster);

  if (params.cbCluster == 0 || params.cbMftRecordSize == 0) {
    return false;
  }

  return true;
}

UINT32 ResolveRecordOrIndexSize(INT8 nEncoded, UINT32 cbCluster) {
  if (nEncoded > 0) {
    return static_cast<UINT32>(nEncoded) * cbCluster;
  }

  // Negative: record/index size is 2^|nEncoded| bytes, independent of cluster size (README:
  // "a signed byte field meaning either 'N clusters' or '2^|N| bytes' depending on sign — get
  // this encoding right, it trips up naive implementations").
  const int nShift = -static_cast<int>(nEncoded);
  if (nShift <= 0 || nShift >= 32) {
    return 0;
  }

  return 1u << nShift;
}

bool ApplyFixup(BYTE *pRecord, DWORD cbRecord, UINT32 cbBytesPerSector) {
  if (pRecord == nullptr || cbBytesPerSector == 0 || cbRecord < sizeof(FILE_RECORD_HEADER)) {
    return false;
  }

  const FILE_RECORD_HEADER *pHeader = reinterpret_cast<const FILE_RECORD_HEADER *>(pRecord);
  if (pHeader->Signature != MFT_SIGNATURE_FILE) {
    return false;
  }

  const UINT32 usaOffset = pHeader->UpdateSequenceArrayOffset;
  const UINT32 usaSize = pHeader->UpdateSequenceArraySize; // word count: 1 check word + 1 per sector

  if (usaSize == 0 || cbBytesPerSector < sizeof(UINT16)) {
    return false;
  }

  const DWORD cSectors = cbRecord / cbBytesPerSector;
  if (usaSize != cSectors + 1) {
    // Not necessarily corrupt data — more likely we mis-sized the record (wrong record-size
    // decode) or mis-sized the sector — either way this record cannot be trusted as-is.
    return false;
  }

  const DWORD cbUsaBytes = usaSize * static_cast<DWORD>(sizeof(UINT16));
  if (usaOffset > cbRecord || cbUsaBytes > cbRecord - usaOffset) {
    return false;
  }

  const UINT16 *pUsa = reinterpret_cast<const UINT16 *>(pRecord + usaOffset);
  const UINT16 usnCheck = pUsa[0];

  for (DWORD iSector = 0; iSector < cSectors; ++iSector) {
    const DWORD cbSectorEnd = (iSector + 1) * cbBytesPerSector;
    UINT16 *pSectorLastWord = reinterpret_cast<UINT16 *>(pRecord + cbSectorEnd - sizeof(UINT16));

    if (*pSectorLastWord != usnCheck) {
      // M5: the on-disk sector-end signature does not match the update sequence array's check
      // value, meaning this record is torn/corrupt (or the fixup was skipped/miscomputed).
      // Reject outright rather than restoring only some sectors and parsing the rest.
      return false;
    }

    *pSectorLastWord = pUsa[1 + iSector];
  }

  return true;
}

bool ParseDataRuns(const BYTE *pRuns, DWORD cbRuns, UINT64 ullStartingVcn, std::vector<MFT_DATA_RUN> &rgRuns) {
  rgRuns.clear();

  if (pRuns == nullptr) {
    return false;
  }

  DWORD idx = 0;
  UINT64 ullVcn = ullStartingVcn;
  INT64 llLcn = 0; // running physical LCN; each run's offset is a delta from the previous one

  while (idx < cbRuns) {
    const BYTE header = pRuns[idx];
    if (header == 0) {
      break; // end-of-runlist marker
    }

    const DWORD cbLength = header & 0x0F;
    const DWORD cbOffset = (header >> 4) & 0x0F;
    ++idx;

    if (cbLength == 0 || cbLength > 8 || cbOffset > 8) {
      return false;
    }

    if (cbLength + cbOffset > cbRuns - idx) {
      return false; // truncated run stream
    }

    UINT64 cClusters = 0;
    for (DWORD i = 0; i < cbLength; ++i) {
      cClusters |= static_cast<UINT64>(pRuns[idx + i]) << (8 * i);
    }
    idx += cbLength;

    MFT_DATA_RUN run;
    run.ullVcnStart = ullVcn;
    run.cClusters = cClusters;

    if (cbOffset == 0) {
      // Sparse run: this VCN range has no physical allocation (logically all zero).
      run.bSparse = true;
      run.ullLcn = 0;
    } else {
      UINT64 ullRaw = 0;
      for (DWORD i = 0; i < cbOffset; ++i) {
        ullRaw |= static_cast<UINT64>(pRuns[idx + i]) << (8 * i);
      }

      INT64 llDelta = static_cast<INT64>(ullRaw);
      if (cbOffset < 8) {
        // Sign-extend the cbOffset-byte two's-complement value up to full 64 bits.
        const UINT64 signBit = static_cast<UINT64>(1) << (8 * cbOffset - 1);
        if ((ullRaw & signBit) != 0) {
          llDelta = static_cast<INT64>(ullRaw | (~static_cast<UINT64>(0) << (8 * cbOffset)));
        }
      }

      llLcn += llDelta;
      if (llLcn < 0) {
        return false; // malformed: resolves to a negative physical cluster number
      }

      run.bSparse = false;
      run.ullLcn = static_cast<UINT64>(llLcn);
    }

    idx += cbOffset;
    ullVcn += cClusters;
    rgRuns.push_back(run);
  }

  return true;
}

MFT_PARSE_RESULT ParseFileRecord(const BYTE *pRecord, DWORD cbRecord, UINT64 ullRecordIndex, MFT_PARSED_RECORD &out) {
  out = MFT_PARSED_RECORD{};

  if (pRecord == nullptr || cbRecord < sizeof(FILE_RECORD_HEADER)) {
    return MFT_PARSE_MALFORMED;
  }

  const FILE_RECORD_HEADER *pHeader = reinterpret_cast<const FILE_RECORD_HEADER *>(pRecord);
  if (pHeader->Signature != MFT_SIGNATURE_FILE) {
    return MFT_PARSE_MALFORMED;
  }

  if ((pHeader->Flags & MFT_RECORD_FLAG_IN_USE) == 0) {
    return MFT_PARSE_NOT_IN_USE; // M4
  }

  if (pHeader->BaseFileRecordReference != 0) {
    // M6 (partial support): extension records hold overflow attributes for a base record
    // elsewhere and never carry their own $FILE_NAME/$STANDARD_INFORMATION, so they are never
    // a standalone file/folder. Detected and skipped, not crashed on or misparsed.
    return MFT_PARSE_EXTENSION_RECORD;
  }

  const DWORD cbUsedSize = pHeader->UsedSize <= cbRecord ? pHeader->UsedSize : cbRecord;
  const DWORD firstAttrOffset = pHeader->FirstAttributeOffset;

  if (firstAttrOffset < sizeof(FILE_RECORD_HEADER) || firstAttrOffset >= cbUsedSize) {
    return MFT_PARSE_MALFORMED;
  }

  DWORD dwAttributes = 0;
  bool bHaveStandardInfo = false;

  const WCHAR *pwszBestName = nullptr;
  USHORT cchBestName = 0;
  ULONGLONG ullBestParentFrn = 0;
  int nBestPriority = 5;

  DWORD offset = firstAttrOffset;

  while (offset + sizeof(ATTRIBUTE_RECORD_HEADER) <= cbUsedSize) {
    const ATTRIBUTE_RECORD_HEADER *pAttr = reinterpret_cast<const ATTRIBUTE_RECORD_HEADER *>(pRecord + offset);

    if (pAttr->TypeCode == ATTR_TYPE_END) {
      break;
    }

    // offset < cbUsedSize is guaranteed by the loop condition above, so this subtraction can't
    // underflow; comparing this way (rather than offset + Length > cbUsedSize) avoids a DWORD
    // wraparound if Length were ever a corrupt, very large value.
    if (pAttr->Length < sizeof(ATTRIBUTE_RECORD_HEADER) || pAttr->Length > cbUsedSize - offset) {
      return MFT_PARSE_MALFORMED;
    }

    if (pAttr->NonResident == 0 && ResidentTailFits(pAttr)) {
      const ATTRIBUTE_RESIDENT_TAIL *pTail = reinterpret_cast<const ATTRIBUTE_RESIDENT_TAIL *>(reinterpret_cast<const BYTE *>(pAttr) + sizeof(ATTRIBUTE_RECORD_HEADER));
      const DWORD contentOffset = pTail->ContentOffset;

      if (pAttr->TypeCode == ATTR_TYPE_STANDARD_INFORMATION) {
        if (contentOffset <= pAttr->Length && sizeof(STANDARD_INFORMATION) <= pAttr->Length - contentOffset) {
          const STANDARD_INFORMATION *pInfo = reinterpret_cast<const STANDARD_INFORMATION *>(reinterpret_cast<const BYTE *>(pAttr) + contentOffset);
          dwAttributes = pInfo->DosFileAttributes;
          bHaveStandardInfo = true;
        }
      } else if (pAttr->TypeCode == ATTR_TYPE_FILE_NAME) {
        if (contentOffset <= pAttr->Length && sizeof(FILE_NAME_ATTRIBUTE) <= pAttr->Length - contentOffset) {
          const FILE_NAME_ATTRIBUTE *pFileName = reinterpret_cast<const FILE_NAME_ATTRIBUTE *>(reinterpret_cast<const BYTE *>(pAttr) + contentOffset);
          const DWORD cbNameStart = contentOffset + static_cast<DWORD>(sizeof(FILE_NAME_ATTRIBUTE));
          const DWORD cbName = static_cast<DWORD>(pFileName->FileNameLength) * sizeof(WCHAR);

          if (pFileName->FileNameLength > 0 && cbNameStart <= pAttr->Length && cbName <= pAttr->Length - cbNameStart) {
            const int nPriority = FileNameNamespacePriority(pFileName->FileNameNamespace);
            if (nPriority < nBestPriority) {
              nBestPriority = nPriority;
              pwszBestName = reinterpret_cast<const WCHAR *>(reinterpret_cast<const BYTE *>(pAttr) + cbNameStart);
              cchBestName = pFileName->FileNameLength;
              ullBestParentFrn = pFileName->ParentDirectory;
            }
          }
        }
      }
    }

    offset += pAttr->Length;
  }

  if (pwszBestName == nullptr) {
    return MFT_PARSE_NO_FILE_NAME; // M6 gap: name lives elsewhere (e.g. $ATTRIBUTE_LIST) or is absent
  }

  out.ullFrn = (static_cast<ULONGLONG>(pHeader->SequenceNumber) << 48) | (ullRecordIndex & 0x0000FFFFFFFFFFFFull);
  out.ullParentFrn = ullBestParentFrn;
  out.pwszName = pwszBestName;
  out.cchName = cchBestName;
  out.bIsDirectory = (pHeader->Flags & MFT_RECORD_FLAG_IS_DIRECTORY) != 0;
  out.dwAttributes = bHaveStandardInfo ? dwAttributes : 0;

  return MFT_PARSE_OK;
}

} // namespace ntfs
