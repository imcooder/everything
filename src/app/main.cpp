#include "volume/volume_manager.h"

#include <cstdio>
#include <memory>
#include <set>
#include <vector>

namespace {

void OnVolumeError(DWORD dwError, LPCWSTR wszMessage) {
  wprintf(L"[error %u] %s\n", dwError, wszMessage != nullptr ? wszMessage : L"");
}

void PrintIndexStats(WCHAR wchDrive, const index::INDEX_STATS &stats) {
  wprintf(L"  %c: index nodes=%u search=%u pool=%u KB / %u KB unresolved=%u\n", wchDrive, stats.m_cNodes, stats.m_cSearchEntries, stats.m_cbPoolUsed / 1024, stats.m_cbPoolAllocated / 1024, stats.m_cUnresolvedParents);
}

// Sample sink for SearchAllAsync. Coordinator serializes callbacks; path materialization in
// OnAdded runs on the matching volume I/O thread (required by CVolume::MaterializeFullPathUtf8).
class CSampleSearchSink : public volume::ISearchSink {
public:
  explicit CSampleSearchSink(volume::CVolumeManager *pManager) : m_pManager(pManager) {}

  bool IsCancelled(volume::SEARCH_REQUEST_ID ullRequestId) const override {
    UNREFERENCED_PARAMETER(ullRequestId);
    return false;
  }

  void OnBatch(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, const UINT32 *rgNodeIds, UINT32 cNodeIds) override {
    UNREFERENCED_PARAMETER(rgNodeIds);
    m_ullRequestId = ullRequestId;
    m_cHits += cNodeIds;
    wprintf(L"  [%llu] %c: batch +%u (total %u)\n", static_cast<unsigned long long>(ullRequestId), wchDriveLetter, cNodeIds, m_cHits);
  }

  void OnInitialScanComplete(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter) override {
    wprintf(L"  [%llu] %c: initial scan complete (total %u)\n", static_cast<unsigned long long>(ullRequestId), wchDriveLetter, m_cHits);
  }

  void OnAdded(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) override {
    ++m_cHits;

    // Safe: coordinator delivers this on the source volume's I/O thread.
    if (volume::CVolume *pVolume = m_pManager->GetVolume(wchDriveLetter)) {
      std::vector<char> rgPath;
      if (pVolume->MaterializeFullPathUtf8(nodeId, rgPath)) {
        rgPath.push_back('\0');
        wprintf(L"  [%llu] %c: +added %hs (total %u)\n", static_cast<unsigned long long>(ullRequestId), wchDriveLetter, rgPath.data(), m_cHits);
        return;
      }
    }

    wprintf(L"  [%llu] %c: +added node=%u (total %u)\n", static_cast<unsigned long long>(ullRequestId), wchDriveLetter, nodeId, m_cHits);
  }

  void OnRemoved(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) override {
    if (m_cHits > 0) {
      --m_cHits;
    }
    wprintf(L"  [%llu] %c: -removed node=%u (total %u)\n", static_cast<unsigned long long>(ullRequestId), wchDriveLetter, nodeId, m_cHits);
  }

  void OnComplete(volume::SEARCH_REQUEST_ID ullRequestId, bool bCancelled) override {
    wprintf(L"  [%llu] all volumes done: cancelled=%d total=%u\n", static_cast<unsigned long long>(ullRequestId), bCancelled ? 1 : 0, m_cHits);
  }

private:
  volume::CVolumeManager *m_pManager;
  volume::SEARCH_REQUEST_ID m_ullRequestId = volume::SEARCH_REQUEST_ID_INVALID;
  UINT32 m_cHits = 0;
};

} // namespace

int wmain(int argc, wchar_t *argv[]) {
  UNREFERENCED_PARAMETER(argc);
  UNREFERENCED_PARAMETER(argv);

  wprintf(L"Everything.Core %hs — per-volume USN index (single-threaded per disk)\n", EVERYTHING_VERSION);
  wprintf(L"Run as Administrator.\n\n");

  volume::CVolumeManager manager;
  manager.SetErrorCallback(OnVolumeError);

  if (!manager.RefreshVolumes()) {
    wprintf(L"No NTFS fixed volumes found.\n");
    return 1;
  }

  const auto letters = manager.GetDriveLetters();
  wprintf(L"Volumes (%zu):\n", letters.size());
  for (WCHAR wch : letters) {
    wprintf(L"  %c:\n", wch);
  }
  wprintf(L"\n");

  wprintf(L"Loading USN records into per-volume index...\n");
  manager.StartLoadAllAsync();

  std::set<WCHAR> setCompleted;
  for (;;) {
    Sleep(500);

    bool bAllDone = true;
    for (WCHAR wch : letters) {
      volume::CVolume *pVolume = manager.GetVolume(wch);
      if (pVolume == nullptr) {
        continue;
      }

      const volume::VOLUME_STATE state = pVolume->GetState();
      if (state == volume::VOLUME_STATE_ENUMERATING || state == volume::VOLUME_STATE_OPENING) {
        bAllDone = false;
      } else if (pVolume->IsReadyForSearch() && setCompleted.find(wch) == setCompleted.end()) {
        setCompleted.insert(wch);
        wprintf(L"  %c: USN records=%u\n", wch, pVolume->GetEnumeratedRecordCount());
        PrintIndexStats(wch, pVolume->GetLastIndexStats());
      } else if (state == volume::VOLUME_STATE_ERROR) {
        bAllDone = false;
      }
    }

    if (bAllDone && setCompleted.size() == letters.size()) {
      break;
    }
  }

  wprintf(L"\nStarting USN journal monitors (non-blocking, interleaved with search)...\n");
  manager.StartMonitorAllAsync();

  wprintf(L"\nLive search all volumes: path + glob (override with argv[1])\n");
  LPCWSTR wszSearchQuery = (argc > 1 && argv[1] != nullptr && argv[1][0] != L'\0') ? argv[1] : L"parent:c:\\Windows *.dll";
  wprintf(L"  query: %s\n", wszSearchQuery);
  manager.SearchAllAsync(1, wszSearchQuery, std::make_shared<CSampleSearchSink>(&manager));

  for (int i = 0; i < 15; ++i) {
    Sleep(1000);
    wprintf(L"  waiting for USN-driven live updates... %d/15\r", i + 1);
  }
  wprintf(L"\n");

  manager.CancelSearchAll(1);
  manager.StopAllAndWait();
  wprintf(L"Done.\n");
  return 0;
}
