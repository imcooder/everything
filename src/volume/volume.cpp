#include "volume/volume.h"

#include "index/utf8_convert.h"

#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <thread>

namespace volume {

CVolume::CVolume(WCHAR wchDriveLetter) : m_wchDriveLetter(wchDriveLetter), m_state(VOLUME_STATE_IDLE), m_dwEnumeratedCount(0), m_ullLatestSearchId(0) {
  ZeroMemory(&m_identity, sizeof(m_identity));
  ZeroMemory(&m_lastIndexStats, sizeof(m_lastIndexStats));
  m_identity.m_wchDriveLetter = wchDriveLetter;
}

CVolume::~CVolume() {
  StopAndWait();
}

WCHAR CVolume::GetDriveLetter() const {
  return m_wchDriveLetter;
}

DWORD CVolume::GetSerialNumber() const {
  return m_identity.m_dwSerialNumber;
}

VOLUME_STATE CVolume::GetState() const {
  return m_state;
}

bool CVolume::Open() {
  if (m_pVolumeHandle != nullptr && m_pVolumeHandle->IsOpen()) {
    return true;
  }

  m_state = VOLUME_STATE_OPENING;

  m_pVolumeHandle = std::make_unique<ntfs::CNtfsVolumeHandle>();
  if (!m_pVolumeHandle->Open(m_wchDriveLetter)) {
    m_state = VOLUME_STATE_ERROR;
    return false;
  }

  m_identity.m_dwSerialNumber = m_pVolumeHandle->GetSerialNumber();

  m_enumerator.SetVolumeHandle(m_pVolumeHandle.get());
  m_monitor.SetVolumeHandle(m_pVolumeHandle.get());
  WireUsnCallbacks();

  const std::wstring threadName = BuildThreadName(L"Io");
  m_pIoService = std::make_unique<io::CIoService>();
  if (!m_pIoService->Start(threadName.c_str())) {
    m_pVolumeHandle.reset();
    m_pIoService.reset();
    m_state = VOLUME_STATE_ERROR;
    return false;
  }

  m_state = VOLUME_STATE_IDLE;
  return true;
}

void CVolume::Close() {
  StopAndWait();
}

void CVolume::SetRecordCallback(core::UsnRecordCallback callback) {
  m_fnRecord = std::move(callback);
}

void CVolume::SetErrorCallback(core::VolumeErrorCallback callback) {
  m_fnError = std::move(callback);
  m_enumerator.SetErrorCallback(m_fnError);
  m_monitor.SetErrorCallback(m_fnError);
}

void CVolume::StartLoadAsync() {
  if (m_pIoService == nullptr) {
    return;
  }

  m_pIoService->Post([this]() { DoLoad(); });
}

void CVolume::StartMonitorAsync(USN usnStart) {
  if (m_pIoService == nullptr) {
    return;
  }

  m_pIoService->Post([this, usnStart]() { DoMonitor(usnStart); });
}

void CVolume::StopAsync() {
  if (m_pIoService == nullptr) {
    return;
  }

  m_pIoService->Post([this]() { DoStop(); });
}

void CVolume::StopAndWait() {
  m_bUsnMonitorActive = false;
  m_monitor.RequestStop();

  if (m_pUsnPollTimer != nullptr) {
    m_pUsnPollTimer->cancel();
  }

  for (int i = 0; i < 50 && m_monitor.IsRunning(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (m_pIoService != nullptr) {
    m_pIoService->Stop();
    m_pIoService.reset();
  }

  m_pUsnPollTimer.reset();
  m_bLiveSearchActive = false;
  m_liveSearch = LIVE_SEARCH{};

  if (m_pVolumeHandle != nullptr) {
    m_pVolumeHandle->Close();
    m_pVolumeHandle.reset();
  }

  m_state = VOLUME_STATE_IDLE;
}

DWORD CVolume::GetEnumeratedRecordCount() const {
  return m_dwEnumeratedCount;
}

index::INDEX_STATS CVolume::GetLastIndexStats() const {
  return m_lastIndexStats;
}

bool CVolume::IsReadyForSearch() const {
  const VOLUME_STATE state = m_state;
  return state == VOLUME_STATE_READY || state == VOLUME_STATE_MONITORING;
}

void CVolume::SearchAsync(SEARCH_REQUEST_ID ullRequestId, LPCWSTR wszQuery, std::shared_ptr<ISearchSink> pSink) {
  if (m_pIoService == nullptr || ullRequestId == SEARCH_REQUEST_ID_INVALID || pSink == nullptr) {
    return;
  }

  AtomicMaxSearchId(m_ullLatestSearchId, ullRequestId);

  m_pIoService->Post([this, ullRequestId, wstrQuery = std::wstring(wszQuery ? wszQuery : L""), pSink = std::move(pSink)]() mutable {
    UpdateSearchSlot(ullRequestId, std::move(wstrQuery), std::move(pSink));
    TryDispatchSearch();
  });
}

void CVolume::CancelSearch(SEARCH_REQUEST_ID ullRequestId) {
  if (m_pIoService == nullptr || ullRequestId == SEARCH_REQUEST_ID_INVALID) {
    return;
  }

  m_pIoService->Post([this, ullRequestId]() { DoCancelSearch(ullRequestId); });
}

void CVolume::AtomicMaxSearchId(std::atomic<SEARCH_REQUEST_ID> &target, SEARCH_REQUEST_ID value) {
  SEARCH_REQUEST_ID current = target.load(std::memory_order_relaxed);
  while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_release, std::memory_order_relaxed)) {
  }
}

bool CVolume::IsSearchCancelled(SEARCH_REQUEST_ID ullRequestId) const {
  return m_setCancelledSearchIds.find(ullRequestId) != m_setCancelledSearchIds.end();
}

bool CVolume::IsSearchStale(SEARCH_REQUEST_ID ullRequestId) const {
  return ullRequestId < m_ullLatestSearchId.load(std::memory_order_acquire);
}

void CVolume::DoCancelSearch(SEARCH_REQUEST_ID ullRequestId) {
  m_setCancelledSearchIds.insert(ullRequestId);

  if (m_searchSlot.m_bValid && m_searchSlot.m_ullRequestId == ullRequestId) {
    m_searchSlot = SEARCH_SLOT{};
  }

  if (m_bLiveSearchActive && m_liveSearch.m_ullRequestId == ullRequestId) {
    FinishLiveSearch(true);
  }

  TryDispatchSearch();
}

void CVolume::UpdateSearchSlot(SEARCH_REQUEST_ID ullRequestId, std::wstring wstrQuery, std::shared_ptr<ISearchSink> pSink) {
  if (m_bLiveSearchActive && m_liveSearch.m_ullRequestId == ullRequestId && m_liveSearch.m_wstrQuery == wstrQuery) {
    return;
  }

  if (m_bLiveSearchActive) {
    FinishLiveSearch(true);
  }

  m_searchSlot.m_ullRequestId = ullRequestId;
  m_searchSlot.m_wstrQuery = std::move(wstrQuery);
  m_searchSlot.m_pSink = std::move(pSink);
  m_searchSlot.m_bValid = true;
}

void CVolume::TryDispatchSearch() {
  if (!m_searchSlot.m_bValid) {
    return;
  }

  if (m_bLiveSearchActive) {
    return;
  }

  SEARCH_SLOT slot = std::move(m_searchSlot);
  m_searchSlot = SEARCH_SLOT{};

  if (!IsReadyForSearch() || ShouldStopSearch(slot.m_ullRequestId, slot.m_pSink.get())) {
    FinishSearch(slot.m_ullRequestId, slot.m_pSink, true);
    m_setCancelledSearchIds.erase(slot.m_ullRequestId);
    return;
  }

  StartLiveSearch(slot.m_ullRequestId, slot.m_wstrQuery.c_str(), slot.m_pSink);
}

void CVolume::StartLiveSearch(SEARCH_REQUEST_ID ullRequestId, LPCWSTR wszQuery, const std::shared_ptr<ISearchSink> &pSink) {
  m_liveSearch = LIVE_SEARCH{};
  m_liveSearch.m_ullRequestId = ullRequestId;
  m_liveSearch.m_wstrQuery = wszQuery != nullptr ? std::wstring(wszQuery) : std::wstring();
  m_liveSearch.m_pSink = pSink;
  m_liveSearch.m_phase = LIVE_SEARCH_PHASE_SCANNING;
  m_liveSearch.m_cScanCursor = 0;
  m_liveSearch.m_cHits = 0;

  index::ParseSearchQuery(wszQuery, m_liveSearch.m_query);
  m_index.ResolveParsedQuery(m_wchDriveLetter, m_liveSearch.m_query);

  m_bLiveSearchActive = true;

  if (m_liveSearch.m_query.m_pathScope == index::PATH_SCOPE_NONE) {
    m_liveSearch.m_phase = LIVE_SEARCH_PHASE_LIVE;
    m_setCancelledSearchIds.erase(ullRequestId);

    if (pSink != nullptr) {
      pSink->OnInitialScanComplete(ullRequestId, m_wchDriveLetter);
    }

    return;
  }

  PostScanChunk();
}

void CVolume::PostScanChunk() {
  if (m_pIoService == nullptr) {
    return;
  }

  m_pIoService->Post([this]() { RunScanChunk(); });
}

void CVolume::RunScanChunk() {
  if (!m_bLiveSearchActive || m_liveSearch.m_phase != LIVE_SEARCH_PHASE_SCANNING) {
    return;
  }

  const SEARCH_REQUEST_ID ullRequestId = m_liveSearch.m_ullRequestId;
  const std::shared_ptr<ISearchSink> pSink = m_liveSearch.m_pSink;

  if (ShouldStopSearch(ullRequestId, pSink.get())) {
    FinishLiveSearch(true);
    m_setCancelledSearchIds.erase(ullRequestId);
    TryDispatchSearch();
    return;
  }

  const UINT32 cTotal = m_index.GetSearchEntryCount();
  std::vector<UINT32> rgBatch;
  rgBatch.reserve(SEARCH_STREAM_BATCH_SIZE);

  UINT32 cProcessed = 0;
  while (m_liveSearch.m_cScanCursor < cTotal && cProcessed < SEARCH_SCAN_CHUNK_SIZE) {
    if (m_liveSearch.m_cHits >= SEARCH_MAX_RESULTS) {
      m_liveSearch.m_cScanCursor = cTotal;
      break;
    }

    const UINT32 nodeId = m_index.GetSearchEntryNodeId(m_liveSearch.m_cScanCursor++);
    ++cProcessed;

    if (m_index.NodeMatchesParsedQuery(nodeId, m_liveSearch.m_query)) {
      if (m_liveSearch.m_setResults.insert(nodeId).second) {
        rgBatch.push_back(nodeId);
        ++m_liveSearch.m_cHits;

        if (rgBatch.size() >= SEARCH_STREAM_BATCH_SIZE) {
          pSink->OnBatch(ullRequestId, m_wchDriveLetter, rgBatch.data(), static_cast<UINT32>(rgBatch.size()));
          rgBatch.clear();
        }
      }
    }
  }

  if (!rgBatch.empty() && pSink != nullptr) {
    pSink->OnBatch(ullRequestId, m_wchDriveLetter, rgBatch.data(), static_cast<UINT32>(rgBatch.size()));
  }

  if (m_liveSearch.m_cScanCursor >= cTotal) {
    m_liveSearch.m_phase = LIVE_SEARCH_PHASE_LIVE;
    m_setCancelledSearchIds.erase(ullRequestId);

    if (pSink != nullptr) {
      pSink->OnInitialScanComplete(ullRequestId, m_wchDriveLetter);
    }

    TryDispatchSearch();
    return;
  }

  PostScanChunk();
}

void CVolume::UpdateLiveSearchForNode(UINT32 nodeId, DWORD dwUsnReason) {
  if (!m_bLiveSearchActive || m_liveSearch.m_phase != LIVE_SEARCH_PHASE_LIVE) {
    return;
  }

  const SEARCH_REQUEST_ID ullRequestId = m_liveSearch.m_ullRequestId;
  const std::shared_ptr<ISearchSink> pSink = m_liveSearch.m_pSink;

  if (ShouldStopSearch(ullRequestId, pSink.get())) {
    FinishLiveSearch(true);
    m_setCancelledSearchIds.erase(ullRequestId);
    TryDispatchSearch();
    return;
  }

  const bool bMatches = m_index.NodeMatchesParsedQuery(nodeId, m_liveSearch.m_query);
  const bool bInResults = m_liveSearch.m_setResults.find(nodeId) != m_liveSearch.m_setResults.end();

  if (bMatches && !bInResults) {
    if (m_liveSearch.m_cHits >= SEARCH_MAX_RESULTS) {
      return;
    }

    m_liveSearch.m_setResults.insert(nodeId);
    ++m_liveSearch.m_cHits;
    if (pSink != nullptr) {
      pSink->OnAdded(ullRequestId, m_wchDriveLetter, nodeId);
    }
    return;
  }

  if (!bMatches && bInResults) {
    m_liveSearch.m_setResults.erase(nodeId);
    --m_liveSearch.m_cHits;
    if (pSink != nullptr) {
      pSink->OnRemoved(ullRequestId, m_wchDriveLetter, nodeId);
    }
    return;
  }

  // Still matches, still in the result set: a rename that keeps the node matching (e.g.
  // delta-a.txt -> delta-b.txt while searching "delta") changes neither membership nor hit
  // count, so neither branch above fires. Tell the sink to refresh any cached display text
  // for this node so the UI doesn't keep showing the pre-rename name.
  if (bMatches && bInResults && pSink != nullptr && (dwUsnReason & (USN_REASON_RENAME_NEW_NAME | USN_REASON_RENAME_OLD_NAME)) != 0) {
    pSink->OnUpdated(ullRequestId, m_wchDriveLetter, nodeId);
  }
}

void CVolume::FinishLiveSearch(bool bCancelled) {
  if (!m_bLiveSearchActive) {
    return;
  }

  const SEARCH_REQUEST_ID ullRequestId = m_liveSearch.m_ullRequestId;
  const std::shared_ptr<ISearchSink> pSink = m_liveSearch.m_pSink;
  m_bLiveSearchActive = false;
  m_liveSearch = LIVE_SEARCH{};
  FinishSearch(ullRequestId, pSink, bCancelled);
}

bool CVolume::ShouldStopSearch(SEARCH_REQUEST_ID ullRequestId, const ISearchSink *pSink) const {
  if (IsSearchCancelled(ullRequestId) || IsSearchStale(ullRequestId)) {
    return true;
  }

  return pSink != nullptr && pSink->IsCancelled(ullRequestId);
}

void CVolume::FinishSearch(SEARCH_REQUEST_ID ullRequestId, const std::shared_ptr<ISearchSink> &pSink, bool bCancelled) {
  if (pSink == nullptr) {
    return;
  }

  pSink->OnComplete(ullRequestId, bCancelled);
}

void CVolume::WireUsnCallbacks() {
  core::UsnRecordCallback fn = [this](const USN_RECORD_V2 &record) { OnUsnRecord(record); };
  m_enumerator.SetRecordCallback(fn);
  m_monitor.SetRecordCallback(fn);
}

void CVolume::OnUsnRecord(const USN_RECORD_V2 &record) {
  index::INDEX_USN_CHANGE change = {};
  m_index.ApplyUsnRecord(record, &change);

  if (change.m_nodeId != index::INDEX_INVALID_NODE) {
    UpdateLiveSearchForNode(change.m_nodeId, record.Reason);
  }

  if (m_fnRecord) {
    m_fnRecord(record);
  }
}

void CVolume::DoLoad() {
  if (m_pVolumeHandle == nullptr || !m_pVolumeHandle->IsOpen()) {
    m_state = VOLUME_STATE_ERROR;
    return;
  }

  m_state = VOLUME_STATE_ENUMERATING;
  m_dwEnumeratedCount = 0;
  m_index.Reset();
  m_index.BeginBulkLoad();

  if (m_enumerator.EnumerateAll()) {
    m_dwEnumeratedCount = m_enumerator.GetRecordCount();
    m_index.FinalizeInitialLoad();
    m_lastIndexStats = m_index.GetStats();
    m_state = VOLUME_STATE_READY;
  } else {
    m_state = VOLUME_STATE_ERROR;
  }
}

void CVolume::DoMonitor(USN usnStart) {
  if (m_pVolumeHandle == nullptr || !m_pVolumeHandle->IsOpen()) {
    m_state = VOLUME_STATE_ERROR;
    return;
  }

  if (!m_monitor.StartFromUsn(usnStart)) {
    m_state = VOLUME_STATE_ERROR;
    return;
  }

  m_state = VOLUME_STATE_MONITORING;
  m_bUsnMonitorActive = true;
  m_pUsnPollTimer = std::make_unique<boost::asio::steady_timer>(m_pIoService->GetContext());
  ScheduleUsnPoll(0);
}

void CVolume::DoStop() {
  m_state = VOLUME_STATE_STOPPING;
  m_bUsnMonitorActive = false;
  m_monitor.RequestStop();

  if (m_pUsnPollTimer != nullptr) {
    m_pUsnPollTimer->cancel();
  }
}

void CVolume::ScheduleUsnPoll(DWORD msDelay) {
  if (!m_bUsnMonitorActive || m_pUsnPollTimer == nullptr || m_pIoService == nullptr) {
    return;
  }

  m_pUsnPollTimer->expires_after(std::chrono::milliseconds(msDelay));
  m_pUsnPollTimer->async_wait([this](const boost::system::error_code &ec) {
    if (ec || !m_bUsnMonitorActive) {
      return;
    }

    RunUsnPollOnce();
  });
}

void CVolume::RunUsnPollOnce() {
  if (!m_bUsnMonitorActive) {
    return;
  }

  const ntfs::USN_POLL_STATUS status = m_monitor.PollOnce();

  switch (status) {
  case ntfs::USN_POLL_OK:
    ScheduleUsnPoll(0);
    break;
  case ntfs::USN_POLL_IDLE:
    ScheduleUsnPoll(200);
    break;
  case ntfs::USN_POLL_STOP:
    m_bUsnMonitorActive = false;
    m_lastIndexStats = m_index.GetStats();
    if (m_state == VOLUME_STATE_MONITORING) {
      m_state = VOLUME_STATE_READY;
    }
    break;
  case ntfs::USN_POLL_ERROR:
    m_bUsnMonitorActive = false;
    m_lastIndexStats = m_index.GetStats();
    m_state = VOLUME_STATE_ERROR;
    break;
  }
}

std::wstring CVolume::BuildThreadName(LPCWSTR wszSuffix) const {
  std::wstring name = L"Everything Vol ";
  name.push_back(m_wchDriveLetter);
  name.push_back(L':');
  name.push_back(L' ');
  name.append(wszSuffix);
  return name;
}

bool CVolume::MaterializePathUtf8(UINT32 nodeId, std::vector<char> &rgPathUtf8) const {
  return m_index.MaterializePathUtf8(nodeId, rgPathUtf8);
}

bool CVolume::MaterializeFullPathUtf8(UINT32 nodeId, std::vector<char> &rgPathUtf8) const {
  std::vector<char> rgRelative;
  if (!m_index.MaterializePathUtf8(nodeId, rgRelative)) {
    return false;
  }

  rgPathUtf8.clear();
  rgPathUtf8.push_back(static_cast<char>(m_wchDriveLetter));
  rgPathUtf8.push_back(':');
  if (!rgRelative.empty()) {
    rgPathUtf8.push_back('\\');
    rgPathUtf8.insert(rgPathUtf8.end(), rgRelative.begin(), rgRelative.end());
  }

  return true;
}

} // namespace volume
