#include "ntfs/usn_journal_monitor.h"

namespace ntfs {

namespace {
constexpr DWORD JOURNAL_BUFFER_SIZE = 256 * 1024;
}

CUsnJournalMonitor::CUsnJournalMonitor() : m_pVolume(nullptr), m_usnReadCursor(0), m_bStopRequested(FALSE), m_bRunning(FALSE) {
  ZeroMemory(&m_journalState, sizeof(m_journalState));
}

void CUsnJournalMonitor::SetVolumeHandle(CNtfsVolumeHandle *pVolume) {
  m_pVolume = pVolume;
}

void CUsnJournalMonitor::SetRecordCallback(core::UsnRecordCallback callback) {
  m_fnRecord = std::move(callback);
}

void CUsnJournalMonitor::SetErrorCallback(core::VolumeErrorCallback callback) {
  m_fnError = std::move(callback);
}

BOOL CUsnJournalMonitor::StartFromUsn(USN usnStart) {
  if (m_pVolume == nullptr || !m_pVolume->IsOpen()) {
    return FALSE;
  }

  if (!m_pVolume->QueryUsnJournalState(m_journalState)) {
    return FALSE;
  }

  m_usnReadCursor = usnStart;
  if (m_usnReadCursor == 0) {
    m_usnReadCursor = m_journalState.m_usnFirst;
  }

  m_bStopRequested = FALSE;
  m_bRunning = TRUE;
  return TRUE;
}

void CUsnJournalMonitor::RequestStop() {
  m_bStopRequested = TRUE;
}

BOOL CUsnJournalMonitor::IsRunning() const {
  return m_bRunning;
}

USN CUsnJournalMonitor::GetCurrentUsn() const {
  return m_usnReadCursor;
}

USN_POLL_STATUS CUsnJournalMonitor::PollOnce() {
  if (m_bStopRequested) {
    m_bRunning = FALSE;
    return USN_POLL_STOP;
  }

  if (m_pVolume == nullptr || !m_pVolume->IsOpen()) {
    m_bRunning = FALSE;
    return USN_POLL_ERROR;
  }

  HANDLE hVolume = m_pVolume->GetHandle();
  BYTE rgBuffer[JOURNAL_BUFFER_SIZE];

  READ_USN_JOURNAL_DATA_V0 readData = {};
  readData.UsnJournalID = m_journalState.m_ullJournalId;
  readData.StartUsn = m_usnReadCursor;
  readData.ReasonMask = 0xFFFFFFFF;
  readData.ReturnOnlyOnClose = FALSE;
  readData.Timeout = 0;
  readData.BytesToWaitFor = 0;

  DWORD dwReturned = 0;
  if (!DeviceIoControl(hVolume, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), rgBuffer, sizeof(rgBuffer), &dwReturned, nullptr)) {
    const DWORD dwError = GetLastError();
    if (dwError == ERROR_JOURNAL_ENTRY_DELETED) {
      if (m_fnError) {
        m_fnError(dwError, L"USN journal reset; full re-index required");
      }
      m_bRunning = FALSE;
      return USN_POLL_ERROR;
    }

    if (dwError == ERROR_HANDLE_EOF) {
      return USN_POLL_IDLE;
    }

    if (m_fnError) {
      m_fnError(dwError, L"FSCTL_READ_USN_JOURNAL failed");
    }

    return USN_POLL_IDLE;
  }

  if (dwReturned <= sizeof(USN)) {
    return USN_POLL_IDLE;
  }

  PUSN pNextUsn = reinterpret_cast<PUSN>(rgBuffer);
  m_usnReadCursor = *pNextUsn;

  PUSN_RECORD_V2 pRecord = reinterpret_cast<PUSN_RECORD_V2>(rgBuffer + sizeof(USN));

  while (reinterpret_cast<PBYTE>(pRecord) < rgBuffer + dwReturned) {
    if (m_fnRecord) {
      m_fnRecord(*pRecord);
    }

    pRecord = reinterpret_cast<PUSN_RECORD_V2>(reinterpret_cast<PBYTE>(pRecord) + pRecord->RecordLength);
  }

  return USN_POLL_OK;
}

} // namespace ntfs
