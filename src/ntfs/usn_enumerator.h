#pragma once

#include "core/ntfs_types.h"
#include "ntfs/ntfs_volume_handle.h"

namespace ntfs {

// Enumerates all USN records on a volume via FSCTL_ENUM_USN_DATA (initial index load).
class CUsnEnumerator {
public:
  CUsnEnumerator();

  void SetVolumeHandle(CNtfsVolumeHandle *pVolume);
  void SetRecordCallback(core::UsnRecordCallback callback);
  void SetErrorCallback(core::VolumeErrorCallback callback);

  BOOL EnumerateAll();
  DWORD GetRecordCount() const;

private:
  BOOL EnumerateFrom(USN usnStartFrn);

  CNtfsVolumeHandle *m_pVolume;
  core::UsnRecordCallback m_fnRecord;
  core::VolumeErrorCallback m_fnError;
  DWORD m_dwRecordCount;
};

} // namespace ntfs
