#include "ntfs/ntfs_volume_handle.h"

#include "core/platform.h"

namespace ntfs {

CNtfsVolumeHandle::CNtfsVolumeHandle() : m_wchDriveLetter(L'\0'), m_hVolume(INVALID_HANDLE_VALUE), m_dwSerialNumber(0) {
  m_wszDevicePath[0] = L'\0';
}

CNtfsVolumeHandle::~CNtfsVolumeHandle() {
  Close();
}

BOOL CNtfsVolumeHandle::Open(WCHAR wchDriveLetter) {
  Close();

  if (!core::IsNtfsDriveLetter(wchDriveLetter)) {
    return FALSE;
  }

  core::BuildVolumeDevicePath(wchDriveLetter, m_wszDevicePath, 16);

  m_hVolume = CreateFileW(m_wszDevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

  if (m_hVolume == INVALID_HANDLE_VALUE) {
    return FALSE;
  }

  WCHAR wszRoot[4] = {wchDriveLetter, L':', L'\\', L'\0'};
  if (!GetVolumeInformationW(wszRoot, nullptr, 0, &m_dwSerialNumber, nullptr, nullptr, nullptr, 0)) {
    Close();
    return FALSE;
  }

  m_wchDriveLetter = wchDriveLetter;
  return TRUE;
}

void CNtfsVolumeHandle::Close() {
  if (m_hVolume != INVALID_HANDLE_VALUE) {
    CloseHandle(m_hVolume);
    m_hVolume = INVALID_HANDLE_VALUE;
  }

  m_wchDriveLetter = L'\0';
  m_dwSerialNumber = 0;
  m_wszDevicePath[0] = L'\0';
}

BOOL CNtfsVolumeHandle::IsOpen() const {
  return m_hVolume != INVALID_HANDLE_VALUE;
}

WCHAR CNtfsVolumeHandle::GetDriveLetter() const {
  return m_wchDriveLetter;
}

HANDLE CNtfsVolumeHandle::GetHandle() const {
  return m_hVolume;
}

DWORD CNtfsVolumeHandle::GetSerialNumber() const {
  return m_dwSerialNumber;
}

BOOL CNtfsVolumeHandle::QueryUsnJournalState(core::USN_JOURNAL_STATE &state) const {
  if (!IsOpen()) {
    return FALSE;
  }

  USN_JOURNAL_DATA journalData = {};
  DWORD dwReturned = 0;

  if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journalData, sizeof(journalData), &dwReturned, nullptr)) {
    return FALSE;
  }

  state.m_ullJournalId = journalData.UsnJournalID;
  state.m_usnNext = journalData.NextUsn;
  state.m_usnFirst = journalData.FirstUsn;
  state.m_usnLast = journalData.MaxUsn;
  return TRUE;
}

} // namespace ntfs
