// Ad-hoc diagnostic: load a real volume, then run a real search query against it and print the
// actual matched paths (not part of the normal build; compiled and run manually).
#include "volume/volume_manager.h"

#include <cstdio>
#include <vector>

namespace {

class CDiagSearchSink : public volume::ISearchSink {
public:
  explicit CDiagSearchSink(volume::CVolume *pVolume) : m_pVolume(pVolume) {}

  bool IsCancelled(volume::SEARCH_REQUEST_ID) const override {
    return false;
  }

  void OnBatch(volume::SEARCH_REQUEST_ID, WCHAR wchDriveLetter, const UINT32 *rgNodeIds, UINT32 cNodeIds) override {
    for (UINT32 i = 0; i < cNodeIds; ++i) {
      ++m_cHits;
      std::vector<char> rgPath;
      if (m_pVolume->MaterializeFullPathUtf8(rgNodeIds[i], rgPath)) {
        rgPath.push_back('\0');
        if (m_cHits <= 10) {
          wprintf(L"  match %c: %hs\n", wchDriveLetter, rgPath.data());
        }
      }
    }
    fflush(stdout);
  }

  void OnInitialScanComplete(volume::SEARCH_REQUEST_ID, WCHAR) override {
    m_bScanComplete = true;
  }

  void OnAdded(volume::SEARCH_REQUEST_ID, WCHAR, UINT32) override {}
  void OnRemoved(volume::SEARCH_REQUEST_ID, WCHAR, UINT32) override {}
  void OnUpdated(volume::SEARCH_REQUEST_ID, WCHAR, UINT32) override {}

  void OnComplete(volume::SEARCH_REQUEST_ID, bool bCancelled) override {
    m_bComplete = true;
    m_bCancelled = bCancelled;
  }

  UINT32 GetHitCount() const {
    return m_cHits;
  }
  bool IsComplete() const {
    return m_bComplete;
  }

private:
  volume::CVolume *m_pVolume;
  UINT32 m_cHits = 0;
  bool m_bScanComplete = false;
  bool m_bComplete = false;
  bool m_bCancelled = false;
};

} // namespace

int wmain(int argc, wchar_t *argv[]) {
  WCHAR wchDrive = (argc > 1) ? argv[1][0] : L'C';
  LPCWSTR wszQuery = (argc > 2) ? argv[2] : L"*.exe";

  wprintf(L"Diagnosing search on volume %c: query=\"%s\"\n", wchDrive, wszQuery);
  fflush(stdout);

  volume::CVolumeManager manager;
  manager.SetErrorCallback([](DWORD dwError, LPCWSTR wszMessage) {
    wprintf(L"[ERROR %u] %s\n", dwError, wszMessage != nullptr ? wszMessage : L"");
    fflush(stdout);
  });

  if (!manager.RefreshVolumes()) {
    wprintf(L"RefreshVolumes failed\n");
    fflush(stdout);
    return 1;
  }

  volume::CVolume *pVolume = manager.GetVolume(wchDrive);
  if (pVolume == nullptr) {
    wprintf(L"Volume %c: not found\n", wchDrive);
    fflush(stdout);
    return 1;
  }

  if (!pVolume->Open()) {
    wprintf(L"Open failed\n");
    fflush(stdout);
    return 1;
  }

  wprintf(L"Loading...\n");
  fflush(stdout);
  pVolume->StartLoadAsync();

  for (int i = 0; i < 60; ++i) {
    Sleep(1000);
    if (pVolume->IsReadyForSearch()) {
      break;
    }
    if (pVolume->GetState() == volume::VOLUME_STATE_ERROR) {
      wprintf(L"Load failed\n");
      fflush(stdout);
      return 1;
    }
  }

  if (!pVolume->IsReadyForSearch()) {
    wprintf(L"Load did not complete in time\n");
    fflush(stdout);
    return 1;
  }

  const index::INDEX_STATS stats = pVolume->GetLastIndexStats();
  wprintf(L"Load complete: %u nodes indexed. Searching...\n", stats.m_cNodes);
  fflush(stdout);

  auto pSink = std::make_shared<CDiagSearchSink>(pVolume);
  pVolume->SearchAsync(1, wszQuery, pSink);

  for (int i = 0; i < 30 && !pSink->IsComplete(); ++i) {
    Sleep(500);
  }

  wprintf(L"Search complete: %u total matches (showing up to 10 above)\n", pSink->GetHitCount());
  fflush(stdout);

  manager.StopAllAndWait();
  wprintf(L"Done.\n");
  fflush(stdout);
  return pSink->GetHitCount() > 0 ? 0 : 2;
}
