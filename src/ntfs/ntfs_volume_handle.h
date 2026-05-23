#pragma once

#include "core/ntfs_types.h"

namespace ntfs {

class CNtfsVolumeHandle {
public:
  CNtfsVolumeHandle();
  ~CNtfsVolumeHandle();

  CNtfsVolumeHandle(const CNtfsVolumeHandle &) = delete;
  CNtfsVolumeHandle &operator=(const CNtfsVolumeHandle &) = delete;

  BOOL Open(WCHAR wchDriveLetter);
  void Close();

  BOOL IsOpen() const;
  WCHAR GetDriveLetter() const;
  HANDLE GetHandle() const;
  DWORD GetSerialNumber() const;

  BOOL QueryUsnJournalState(core::USN_JOURNAL_STATE &state) const;

private:
  WCHAR m_wchDriveLetter;
  WCHAR m_wszDevicePath[16];
  HANDLE m_hVolume;
  DWORD m_dwSerialNumber;
};

} // namespace ntfs
