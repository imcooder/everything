#include "index/index_persistence.h"

#include <algorithm>
#include <cstring>

namespace index {

namespace {

// Absurdly-large sanity bounds so a corrupt header (random bytes read as counts) can't drive an
// attempted multi-terabyte allocation before the checksum check ever gets a chance to reject it.
constexpr UINT64 MAX_REASONABLE_NODES = 500ull * 1000 * 1000;
constexpr UINT64 MAX_REASONABLE_POOL_BYTES = 32ull * 1024 * 1024 * 1024;

UINT32 UpdateCrc32(UINT32 crc, const void *pData, size_t cbLen) {
  static UINT32 s_rgTable[256];
  static bool s_bInitialized = false;

  if (!s_bInitialized) {
    for (UINT32 i = 0; i < 256; ++i) {
      UINT32 c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) != 0 ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      s_rgTable[i] = c;
    }
    s_bInitialized = true;
  }

  const unsigned char *pBytes = static_cast<const unsigned char *>(pData);
  for (size_t i = 0; i < cbLen; ++i) {
    crc = s_rgTable[(crc ^ pBytes[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc;
}

UINT32 ComputeChecksum(const std::vector<INDEX_NODE> &rgNodes, const std::vector<char> &rgPoolBytes) {
  UINT32 crc = 0xFFFFFFFFu;
  if (!rgNodes.empty()) {
    crc = UpdateCrc32(crc, rgNodes.data(), rgNodes.size() * sizeof(INDEX_NODE));
  }
  if (!rgPoolBytes.empty()) {
    crc = UpdateCrc32(crc, rgPoolBytes.data(), rgPoolBytes.size());
  }
  return crc ^ 0xFFFFFFFFu;
}

bool WriteAll(HANDLE hFile, const void *pData, size_t cbLen) {
  const BYTE *pBytes = static_cast<const BYTE *>(pData);
  size_t cbRemaining = cbLen;

  while (cbRemaining > 0) {
    const DWORD cbChunk = static_cast<DWORD>(std::min<size_t>(cbRemaining, 64u * 1024 * 1024));
    DWORD cbWritten = 0;
    if (!WriteFile(hFile, pBytes, cbChunk, &cbWritten, nullptr) || cbWritten != cbChunk) {
      return false;
    }
    pBytes += cbWritten;
    cbRemaining -= cbWritten;
  }

  return true;
}

bool ReadAll(HANDLE hFile, void *pData, size_t cbLen) {
  BYTE *pBytes = static_cast<BYTE *>(pData);
  size_t cbRemaining = cbLen;

  while (cbRemaining > 0) {
    const DWORD cbChunk = static_cast<DWORD>(std::min<size_t>(cbRemaining, 64u * 1024 * 1024));
    DWORD cbReadActual = 0;
    if (!ReadFile(hFile, pBytes, cbChunk, &cbReadActual, nullptr)) {
      return false;
    }
    if (cbReadActual == 0) {
      // Hit EOF before reading the length the header promised: truncated/corrupt file (P5).
      return false;
    }
    pBytes += cbReadActual;
    cbRemaining -= cbReadActual;
  }

  return true;
}

std::wstring GetDirectoryPart(const std::wstring &wstrPath) {
  const size_t idxSep = wstrPath.find_last_of(L'\\');
  if (idxSep == std::wstring::npos) {
    return std::wstring();
  }
  return wstrPath.substr(0, idxSep);
}

// Creates every missing path segment under wstrDir (e.g. "C:\Users\x\AppData\Local\Everything\index"),
// tolerating segments that already exist. No SHCreateDirectoryEx dependency (that would pull in
// shell32 for the backend library, which only Everything.UI currently links).
bool EnsureDirectoryExists(const std::wstring &wstrDir) {
  if (wstrDir.empty()) {
    return false;
  }

  std::wstring wstrBuild;
  size_t idxStart = 0;

  if (wstrDir.size() >= 2 && wstrDir[1] == L':') {
    wstrBuild = wstrDir.substr(0, 2);
    idxStart = 2;
  }

  while (idxStart < wstrDir.size()) {
    size_t idxSep = wstrDir.find(L'\\', idxStart);
    if (idxSep == std::wstring::npos) {
      idxSep = wstrDir.size();
    }

    wstrBuild += wstrDir.substr(idxStart, idxSep - idxStart);

    if (!wstrBuild.empty() && !(wstrBuild.size() == 2 && wstrBuild[1] == L':')) {
      if (!CreateDirectoryW(wstrBuild.c_str(), nullptr)) {
        const DWORD dwError = GetLastError();
        if (dwError != ERROR_ALREADY_EXISTS) {
          return false;
        }
      }
    }

    wstrBuild += L'\\';
    idxStart = idxSep + 1;
  }

  return true;
}

} // namespace

std::wstring CIndexPersistence::BuildIndexFilePath(DWORD dwVolumeSerial) {
  WCHAR wszBaseDir[MAX_PATH] = {};
  std::wstring wstrBase;

  const DWORD cchLocalAppData = GetEnvironmentVariableW(L"LOCALAPPDATA", wszBaseDir, MAX_PATH);
  if (cchLocalAppData > 0 && cchLocalAppData < MAX_PATH) {
    wstrBase = wszBaseDir;
  } else {
    WCHAR wszTemp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, wszTemp) > 0) {
      wstrBase = wszTemp;
      if (!wstrBase.empty() && wstrBase.back() == L'\\') {
        wstrBase.pop_back();
      }
    }
  }

  WCHAR wszFileName[32] = {};
  swprintf_s(wszFileName, L"%08X.idx", dwVolumeSerial);

  return wstrBase + L"\\Everything\\index\\" + wszFileName;
}

bool CIndexPersistence::Save(const std::wstring &wstrPath, DWORD dwVolumeSerial, const CIndexStore &store, const INDEX_PERSIST_CHECKPOINT &checkpoint) {
  if (!EnsureDirectoryExists(GetDirectoryPart(wstrPath))) {
    return false;
  }

  const std::vector<INDEX_NODE> &rgNodes = store.GetNodesForPersist();

  std::vector<char> rgPoolBytes;
  const UINT32 cbPoolPhysical = store.GetNamePoolForPersist().ExportBytes(rgPoolBytes);
  const UINT32 cbPoolLogical = store.GetNamePoolForPersist().GetUsedBytes();

  INDEX_PERSIST_HEADER header = {};
  header.m_dwMagic = INDEX_PERSIST_MAGIC;
  header.m_dwVersion = INDEX_PERSIST_VERSION;
  header.m_dwVolumeSerial = dwVolumeSerial;
  header.m_ullJournalId = checkpoint.m_ullJournalId;
  header.m_llUsnNext = checkpoint.m_usnNext;
  header.m_llUsnFirst = checkpoint.m_usnFirst;
  header.m_llUsnLast = checkpoint.m_usnLast;
  header.m_cNodes = static_cast<UINT32>(rgNodes.size());
  header.m_cbPoolPhysical = cbPoolPhysical;
  header.m_cbPoolLogical = cbPoolLogical;
  header.m_dwChecksum = ComputeChecksum(rgNodes, rgPoolBytes);
  header.m_dwReserved = 0;

  const std::wstring wstrTempPath = wstrPath + L".tmp";

  HANDLE hFile = CreateFileW(wstrTempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  bool bOk = WriteAll(hFile, &header, sizeof(header));
  bOk = bOk && (rgNodes.empty() || WriteAll(hFile, rgNodes.data(), rgNodes.size() * sizeof(INDEX_NODE)));
  bOk = bOk && (rgPoolBytes.empty() || WriteAll(hFile, rgPoolBytes.data(), rgPoolBytes.size()));

  CloseHandle(hFile);

  if (!bOk) {
    DeleteFileW(wstrTempPath.c_str());
    return false;
  }

  if (!MoveFileExW(wstrTempPath.c_str(), wstrPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(wstrTempPath.c_str());
    return false;
  }

  return true;
}

INDEX_PERSIST_LOAD_RESULT CIndexPersistence::Load(const std::wstring &wstrPath, DWORD dwExpectedVolumeSerial, CIndexStore &store, INDEX_PERSIST_CHECKPOINT &outCheckpoint) {
  store.Reset();

  HANDLE hFile = CreateFileW(wstrPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    return INDEX_PERSIST_LOAD_NOT_FOUND;
  }

  INDEX_PERSIST_HEADER header = {};
  if (!ReadAll(hFile, &header, sizeof(header))) {
    CloseHandle(hFile);
    return INDEX_PERSIST_LOAD_CORRUPT;
  }

  if (header.m_dwMagic != INDEX_PERSIST_MAGIC) {
    CloseHandle(hFile);
    return INDEX_PERSIST_LOAD_CORRUPT;
  }

  if (header.m_dwVersion != INDEX_PERSIST_VERSION) {
    CloseHandle(hFile);
    return INDEX_PERSIST_LOAD_VERSION_MISMATCH;
  }

  if (header.m_dwVolumeSerial != dwExpectedVolumeSerial) {
    CloseHandle(hFile);
    return INDEX_PERSIST_LOAD_SERIAL_MISMATCH;
  }

  if (header.m_cNodes > MAX_REASONABLE_NODES || header.m_cbPoolPhysical > MAX_REASONABLE_POOL_BYTES) {
    CloseHandle(hFile);
    return INDEX_PERSIST_LOAD_CORRUPT;
  }

  std::vector<INDEX_NODE> rgNodes(header.m_cNodes);
  if (header.m_cNodes > 0 && !ReadAll(hFile, rgNodes.data(), rgNodes.size() * sizeof(INDEX_NODE))) {
    CloseHandle(hFile);
    return INDEX_PERSIST_LOAD_CORRUPT;
  }

  std::vector<char> rgPoolBytes(header.m_cbPoolPhysical);
  if (header.m_cbPoolPhysical > 0 && !ReadAll(hFile, rgPoolBytes.data(), rgPoolBytes.size())) {
    CloseHandle(hFile);
    return INDEX_PERSIST_LOAD_CORRUPT;
  }

  CloseHandle(hFile);

  const UINT32 dwComputedChecksum = ComputeChecksum(rgNodes, rgPoolBytes);
  if (dwComputedChecksum != header.m_dwChecksum) {
    return INDEX_PERSIST_LOAD_CORRUPT;
  }

  store.LoadPersistedState(std::move(rgNodes), rgPoolBytes.data(), header.m_cbPoolPhysical, header.m_cbPoolLogical);

  outCheckpoint.m_ullJournalId = header.m_ullJournalId;
  outCheckpoint.m_usnNext = header.m_llUsnNext;
  outCheckpoint.m_usnFirst = header.m_llUsnFirst;
  outCheckpoint.m_usnLast = header.m_llUsnLast;

  return INDEX_PERSIST_LOAD_OK;
}

} // namespace index
