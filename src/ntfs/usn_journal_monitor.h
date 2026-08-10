#pragma once

#include "core/ntfs_types.h"
#include "ntfs/ntfs_volume_handle.h"

#include <atomic>

namespace ntfs {

enum USN_POLL_STATUS { USN_POLL_OK = 0, USN_POLL_IDLE, USN_POLL_STOP, USN_POLL_ERROR };

// Tails the USN change journal via FSCTL_READ_USN_JOURNAL on the volume I/O thread.
class CUsnJournalMonitor {
public:
  CUsnJournalMonitor();

  void SetVolumeHandle(CNtfsVolumeHandle *pVolume);
  void SetRecordCallback(core::UsnRecordCallback callback);
  void SetErrorCallback(core::VolumeErrorCallback callback);

  BOOL StartFromUsn(USN usnStart);
  void RequestStop();
  BOOL IsRunning() const;
  USN GetCurrentUsn() const;

  // Journal id/first/last/next as of the most recent StartFromUsn() query. Used to persist a
  // resumable checkpoint (index::CIndexPersistence) without re-issuing FSCTL_QUERY_USN_JOURNAL.
  core::USN_JOURNAL_STATE GetJournalState() const;

  // Non-blocking: read one journal buffer, invoke record callbacks, return status.
  USN_POLL_STATUS PollOnce();

private:
  CNtfsVolumeHandle *m_pVolume;
  core::UsnRecordCallback m_fnRecord;
  core::VolumeErrorCallback m_fnError;
  core::USN_JOURNAL_STATE m_journalState;
  USN m_usnReadCursor;
  std::atomic<BOOL> m_bStopRequested;
  std::atomic<BOOL> m_bRunning;
};

} // namespace ntfs
