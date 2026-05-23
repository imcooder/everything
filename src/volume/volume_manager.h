#pragma once

#include "volume/volume.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace volume {

// Discovers NTFS fixed volumes and owns one CVolume per drive letter.
// Each CVolume serializes work on its own thread; the manager lock protects
// only the volume map, not per-disk index data.
class CVolumeManager {
public:
  CVolumeManager();
  ~CVolumeManager();

  CVolumeManager(const CVolumeManager &) = delete;
  CVolumeManager &operator=(const CVolumeManager &) = delete;

  void SetRecordCallback(core::UsnRecordCallback callback);
  void SetErrorCallback(core::VolumeErrorCallback callback);

  // Rescan logical drives; create new CVolume objects, drop removed drives.
  bool RefreshVolumes();

  // Open all known volumes in parallel (each volume uses its own I/O thread).
  void OpenAllAsync();

  void StartLoadAllAsync();
  void StartMonitorAllAsync();

  void StopAll();
  void StopAllAndWait();

  CVolume *GetVolume(WCHAR wchDriveLetter);
  std::vector<WCHAR> GetDriveLetters() const;

private:
  bool ShouldIncludeDrive(WCHAR wchDriveLetter) const;
  void ApplyCallbacks(CVolume &volume) const;

  mutable std::mutex m_mutex;
  std::map<WCHAR, std::unique_ptr<CVolume>> m_mapVolumes;
  core::UsnRecordCallback m_fnRecord;
  core::VolumeErrorCallback m_fnError;
};

} // namespace volume
