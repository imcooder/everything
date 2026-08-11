#pragma once

#include "core/ntfs_types.h"
#include "index/index_store.h"
#include "index/query_parser.h"
#include "index/index_types.h"
#include "io/io_service.h"
#include "ntfs/mft_reader.h"
#include "ntfs/ntfs_volume_handle.h"
#include "ntfs/usn_enumerator.h"
#include "ntfs/usn_journal_monitor.h"
#include "volume/i_search_sink.h"
#include "volume/search_types.h"

#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace volume {

enum VOLUME_STATE { VOLUME_STATE_IDLE = 0, VOLUME_STATE_OPENING, VOLUME_STATE_ENUMERATING, VOLUME_STATE_READY, VOLUME_STATE_MONITORING, VOLUME_STATE_STOPPING, VOLUME_STATE_ERROR };

enum LIVE_SEARCH_PHASE { LIVE_SEARCH_PHASE_SCANNING = 0, LIVE_SEARCH_PHASE_LIVE };

// One NTFS fixed disk: all load, monitor, and index work run on a single
// dedicated I/O thread. No mutex is required for per-volume index or USN state.
class CVolume {
public:
  explicit CVolume(WCHAR wchDriveLetter);
  ~CVolume();

  CVolume(const CVolume &) = delete;
  CVolume &operator=(const CVolume &) = delete;

  WCHAR GetDriveLetter() const;
  DWORD GetSerialNumber() const;
  VOLUME_STATE GetState() const;

  bool Open();
  void Close();

  void SetRecordCallback(core::UsnRecordCallback callback);
  void SetErrorCallback(core::VolumeErrorCallback callback);

  void StartLoadAsync();
  void StartMonitorAsync(USN usnStart = 0);
  void StopAsync();
  void StopAndWait();

  DWORD GetEnumeratedRecordCount() const;
  index::INDEX_STATS GetLastIndexStats() const;

  // true after initial USN enumeration and index finalize (also while monitoring).
  bool IsReadyForSearch() const;

  // Starts a live search session: initial scan streams OnBatch, then OnAdded/OnRemoved
  // as the USN journal updates the index until cancel, stale id, or a newer query.
  void SearchAsync(SEARCH_REQUEST_ID ullRequestId, LPCWSTR wszQuery, std::shared_ptr<ISearchSink> pSink);

  void CancelSearch(SEARCH_REQUEST_ID ullRequestId);

  // Volume I/O thread only.
  bool MaterializePathUtf8(UINT32 nodeId, std::vector<char> &rgPathUtf8) const;
  bool MaterializeFullPathUtf8(UINT32 nodeId, std::vector<char> &rgPathUtf8) const;
  bool GetNodeMetadata(UINT32 nodeId, index::INDEX_NODE_METADATA &out) const;

private:
  void WireUsnCallbacks();
  void OnUsnRecord(const USN_RECORD_V2 &record);
  void DoLoad();
  void DoMonitor(USN usnStart);
  void DoStop();

  // Direct MFT read (README §1 / ntfs::CMftReader), tried before the FSCTL_ENUM_USN_DATA
  // fallback in DoLoad(). Returns false on ANY failure (open, parse, low-confidence record
  // count, ...) so DoLoad() can fall back to m_enumerator.EnumerateAll() automatically (M10).
  bool TryLoadViaMftDirectRead();

  // Index persistence (index::CIndexPersistence). All three run on the volume I/O thread.
  bool TryLoadPersistedIndex();
  void PersistIndexNonFatal(const core::USN_JOURNAL_STATE &journalState, USN usnResumeCursor);
  void PersistIndexOnShutdown();
  void DoCancelSearch(SEARCH_REQUEST_ID ullRequestId);
  void UpdateSearchSlot(SEARCH_REQUEST_ID ullRequestId, std::wstring wstrQuery, std::shared_ptr<ISearchSink> pSink);
  void TryDispatchSearch();
  void StartLiveSearch(SEARCH_REQUEST_ID ullRequestId, LPCWSTR wszQuery, const std::shared_ptr<ISearchSink> &pSink);
  void PostScanChunk();
  void RunScanChunk();
  void UpdateLiveSearchForNode(UINT32 nodeId, DWORD dwUsnReason);
  void FinishLiveSearch(bool bCancelled);
  void FinishSearch(SEARCH_REQUEST_ID ullRequestId, const std::shared_ptr<ISearchSink> &pSink, bool bCancelled);
  bool ShouldStopSearch(SEARCH_REQUEST_ID ullRequestId, const ISearchSink *pSink) const;
  bool IsSearchCancelled(SEARCH_REQUEST_ID ullRequestId) const;
  bool IsSearchStale(SEARCH_REQUEST_ID ullRequestId) const;
  static void AtomicMaxSearchId(std::atomic<SEARCH_REQUEST_ID> &target, SEARCH_REQUEST_ID value);
  void ScheduleUsnPoll(DWORD msDelay);
  void RunUsnPollOnce();

  std::wstring BuildThreadName(LPCWSTR wszSuffix) const;

  struct SEARCH_SLOT {
    SEARCH_REQUEST_ID m_ullRequestId = SEARCH_REQUEST_ID_INVALID;
    std::wstring m_wstrQuery;
    std::shared_ptr<ISearchSink> m_pSink;
    bool m_bValid = false;
  };

  struct LIVE_SEARCH {
    SEARCH_REQUEST_ID m_ullRequestId = SEARCH_REQUEST_ID_INVALID;
    std::wstring m_wstrQuery;
    index::CParsedQuery m_query;
    std::unordered_set<UINT32> m_setResults;
    std::shared_ptr<ISearchSink> m_pSink;
    UINT32 m_cScanCursor = 0;
    UINT32 m_cHits = 0;
    LIVE_SEARCH_PHASE m_phase = LIVE_SEARCH_PHASE_SCANNING;
  };

  static constexpr UINT32 SEARCH_MAX_RESULTS = 10000;

  WCHAR m_wchDriveLetter;
  core::VOLUME_IDENTITY m_identity;
  VOLUME_STATE m_state;
  std::unique_ptr<io::CIoService> m_pIoService;
  std::unique_ptr<ntfs::CNtfsVolumeHandle> m_pVolumeHandle;
  ntfs::CUsnEnumerator m_enumerator;
  ntfs::CUsnJournalMonitor m_monitor;
  index::CIndexStore m_index;
  index::INDEX_STATS m_lastIndexStats;
  core::UsnRecordCallback m_fnRecord;
  core::VolumeErrorCallback m_fnError;
  std::atomic<DWORD> m_dwEnumeratedCount;
  std::atomic<SEARCH_REQUEST_ID> m_ullLatestSearchId;
  std::unordered_set<SEARCH_REQUEST_ID> m_setCancelledSearchIds;
  SEARCH_SLOT m_searchSlot;
  bool m_bLiveSearchActive = false;
  LIVE_SEARCH m_liveSearch;
  // Written from StopAndWait() on the caller's thread, read from ScheduleUsnPoll/RunUsnPollOnce
  // on this volume's own io thread — must be atomic, not a plain bool, to guarantee the io thread
  // observes the stop request promptly instead of on an unspecified later iteration.
  std::atomic<bool> m_bUsnMonitorActive{false};
  std::unique_ptr<boost::asio::steady_timer> m_pUsnPollTimer;

  // Per-volume persisted-index file (index::CIndexPersistence::BuildIndexFilePath), set once the
  // volume serial number is known in Open(). Empty means persistence is unavailable this session
  // (e.g. LOCALAPPDATA and %TEMP% both failed to resolve) and load/save become silent no-ops.
  std::wstring m_wstrIndexFilePath;

  // Set by TryLoadPersistedIndex() on a successful warm start; DoMonitor() falls back to this
  // when called with usnStart == 0 (the value every current caller passes), so resuming from a
  // persisted checkpoint requires no change to CVolumeManager/main.cpp call sites.
  USN m_usnPersistedResumeCursor = 0;
};

} // namespace volume
