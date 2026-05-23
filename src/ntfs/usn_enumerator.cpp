#include "ntfs/usn_enumerator.h"

namespace ntfs {

namespace {
constexpr DWORD ENUM_BUFFER_SIZE = 256 * 1024;
}

CUsnEnumerator::CUsnEnumerator() : m_pVolume(nullptr), m_dwRecordCount(0) {}

void CUsnEnumerator::SetVolumeHandle(CNtfsVolumeHandle *pVolume) {
  m_pVolume = pVolume;
}

void CUsnEnumerator::SetRecordCallback(core::UsnRecordCallback callback) {
  m_fnRecord = std::move(callback);
}

void CUsnEnumerator::SetErrorCallback(core::VolumeErrorCallback callback) {
  m_fnError = std::move(callback);
}

DWORD CUsnEnumerator::GetRecordCount() const {
  return m_dwRecordCount;
}

BOOL CUsnEnumerator::EnumerateAll() {
  m_dwRecordCount = 0;

  if (m_pVolume == nullptr || !m_pVolume->IsOpen()) {
    return FALSE;
  }

  return EnumerateFrom(0);
}

BOOL CUsnEnumerator::EnumerateFrom(USN usnStartFrn) {
  HANDLE hVolume = m_pVolume->GetHandle();
  MFT_ENUM_DATA_V1 enumData = {};
  enumData.StartFileReferenceNumber = usnStartFrn;
  enumData.LowUsn = 0;

  BYTE rgBuffer[ENUM_BUFFER_SIZE];

  while (TRUE) {
    DWORD dwReturned = 0;
    if (!DeviceIoControl(hVolume, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), rgBuffer, sizeof(rgBuffer), &dwReturned, nullptr)) {
      const DWORD dwError = GetLastError();
      if (m_fnError) {
        m_fnError(dwError, L"FSCTL_ENUM_USN_DATA failed");
      }
      return FALSE;
    }

    if (dwReturned <= sizeof(USN)) {
      break;
    }

    enumData.StartFileReferenceNumber = *reinterpret_cast<PUSN>(rgBuffer);
    PUSN_RECORD_V2 pRecord = reinterpret_cast<PUSN_RECORD_V2>(rgBuffer + sizeof(USN));

    while (reinterpret_cast<PBYTE>(pRecord) < rgBuffer + dwReturned) {
      if (m_fnRecord) {
        m_fnRecord(*pRecord);
      }

      ++m_dwRecordCount;
      pRecord = reinterpret_cast<PUSN_RECORD_V2>(reinterpret_cast<PBYTE>(pRecord) + pRecord->RecordLength);
    }
  }

  return TRUE;
}

} // namespace ntfs
