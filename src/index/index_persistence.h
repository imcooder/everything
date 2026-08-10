#pragma once

#include "core/ntfs_types.h"
#include "core/platform.h"
#include "index/index_store.h"
#include "index/index_types.h"

#include <string>

namespace index {

// FourCC 'E','V','I','X' stored little-endian so a hex dump of the file reads "EVIX..." at
// offset 0 — purely a debugging convenience, not part of the validation logic.
constexpr UINT32 INDEX_PERSIST_MAGIC = static_cast<UINT32>('E') | (static_cast<UINT32>('V') << 8) | (static_cast<UINT32>('I') << 16) | (static_cast<UINT32>('X') << 24);

// Bump on ANY change to INDEX_PERSIST_HEADER, the node table layout (INDEX_NODE), or the string
// pool byte format. CIndexPersistence::Load() refuses to interpret a file whose version doesn't
// match exactly (docs/index-persistence-testcases.md, case P6) rather than guess.
constexpr UINT32 INDEX_PERSIST_VERSION = 1;

// On-disk layout (single file per volume, see CIndexPersistence::BuildIndexFilePath):
//
//   [INDEX_PERSIST_HEADER]                      fixed size, see below
//   [INDEX_NODE * header.m_cNodes]               raw flat array, no pointers (index_types.h)
//   [char * header.m_cbPoolPhysical]              CBumpStringPool::ExportBytes() blob
//
// #pragma pack(push, 1) removes any compiler-specific inter-member padding so the header's
// on-disk size is exactly the sum of its members' sizes on every toolchain that reads it (also
// asserted below). This is a fixed record read as one blob by Load(), not a struct handed to any
// Win32 API, so there is no alignment requirement pulling the other way.
#pragma pack(push, 1)
struct INDEX_PERSIST_HEADER {
  UINT32 m_dwMagic;
  UINT32 m_dwVersion;
  DWORD m_dwVolumeSerial;   // core::VOLUME_IDENTITY::m_dwSerialNumber; rejects drive-letter reassignment mismatches
  ULONGLONG m_ullJournalId; // core::USN_JOURNAL_STATE::m_ullJournalId at persist time
  LONGLONG m_llUsnNext;     // resume cursor for FSCTL_READ_USN_JOURNAL on next load (checkpoint)
  LONGLONG m_llUsnFirst;    // journal's oldest retained USN at persist time (informational)
  LONGLONG m_llUsnLast;     // journal's newest USN at persist time (informational)
  UINT32 m_cNodes;          // element count of the node table that follows
  UINT32 m_cbPoolPhysical;  // byte length of the string pool blob that follows (drives GetPtr offsets)
  UINT32 m_cbPoolLogical;   // CBumpStringPool::GetUsedBytes() at persist time (stats only, informational)
  UINT32 m_dwChecksum;      // CRC-32 over [node table bytes][pool bytes], computed with this field as 0
  UINT32 m_dwReserved;      // zero; room for a future flags field without bumping the version
};
#pragma pack(pop)

static_assert(sizeof(INDEX_PERSIST_HEADER) == 64, "INDEX_PERSIST_HEADER size changed; bump INDEX_PERSIST_VERSION and update every offset assumption in index_persistence.cpp");

enum INDEX_PERSIST_LOAD_RESULT {
  INDEX_PERSIST_LOAD_OK = 0,
  INDEX_PERSIST_LOAD_NOT_FOUND,        // no file at this path yet (first run for this volume) — P1
  INDEX_PERSIST_LOAD_VERSION_MISMATCH, // magic ok, version differs — P6
  INDEX_PERSIST_LOAD_SERIAL_MISMATCH,  // header's volume serial != the volume being opened
  INDEX_PERSIST_LOAD_CORRUPT,          // bad magic, truncated read, or checksum mismatch — P5
};

// Everything a caller needs to decide whether a delta USN replay is safe (P3, P4, P7, P8) instead
// of a full FSCTL_ENUM_USN_DATA re-enumeration.
struct INDEX_PERSIST_CHECKPOINT {
  ULONGLONG m_ullJournalId = 0;
  USN m_usnNext = 0;
  USN m_usnFirst = 0;
  USN m_usnLast = 0;
};

// Persists one volume's CIndexStore (node table + string pool) to a compact binary file so a
// later CVolume::Open() can skip the full MFT scan and instead replay only the USN delta since
// m_usnNext (README §1/§2). See docs/index-persistence-testcases.md for the acceptance matrix
// this class exists to satisfy.
class CIndexPersistence {
public:
  // %LOCALAPPDATA%\Everything\index\<serial-hex8>.idx (falls back to %TEMP%\Everything\index if
  // LOCALAPPDATA is unset). Keyed by volume serial number, not drive letter, so a persisted index
  // survives a drive-letter reassignment and P11 (independent per-volume files) holds naturally.
  static std::wstring BuildIndexFilePath(DWORD dwVolumeSerial);

  // Writes to "<wstrPath>.tmp" then atomically replaces wstrPath via MoveFileExW, so a crash or
  // killed process mid-write (P10, P12) can only ever leave behind a stray .tmp file — the
  // canonical path always stays either absent or fully valid from a prior successful Save().
  // Returns false on any failure (disk full, unwritable directory, etc.); callers must treat this
  // as non-fatal and keep serving search from the in-memory index (P12).
  static bool Save(const std::wstring &wstrPath, DWORD dwVolumeSerial, const CIndexStore &store, const INDEX_PERSIST_CHECKPOINT &checkpoint);

  // On INDEX_PERSIST_LOAD_OK, replaces the contents of `store` via CIndexStore::LoadPersistedState()
  // and fills `outCheckpoint`. On any other result, `store` is left Reset() and the caller must
  // fall back to a full FSCTL_ENUM_USN_DATA scan (P4, P5, P6) — this function never partially
  // populates the store.
  static INDEX_PERSIST_LOAD_RESULT Load(const std::wstring &wstrPath, DWORD dwExpectedVolumeSerial, CIndexStore &store, INDEX_PERSIST_CHECKPOINT &outCheckpoint);
};

} // namespace index
