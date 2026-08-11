// Manual real-volume diagnostic: load one or more real volumes with verbose, immediately-flushed
// per-second state output. Not run by CI (no admin/real-disk access there) — build and run by
// hand against real hardware. Pass one or more drive letters as argv (e.g.
// "diag_single_volume.exe C D"); defaults to C: alone.
#include "volume/volume_manager.h"

#include <cstdio>
#include <vector>

int wmain(int argc, wchar_t *argv[]) {
  std::vector<WCHAR> rgDrives;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] != L'\0') {
      rgDrives.push_back(argv[i][0]);
    }
  }
  if (rgDrives.empty()) {
    rgDrives.push_back(L'C');
  }

  wprintf(L"Diagnosing %zu volume(s):", rgDrives.size());
  for (WCHAR wch : rgDrives) {
    wprintf(L" %c:", wch);
  }
  wprintf(L"\n");
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

  std::vector<volume::CVolume *> rgVolumes;
  for (WCHAR wch : rgDrives) {
    volume::CVolume *pVolume = manager.GetVolume(wch);
    if (pVolume == nullptr) {
      wprintf(L"Volume %c: not found\n", wch);
      fflush(stdout);
      return 1;
    }

    wprintf(L"Opening volume %c:...\n", wch);
    fflush(stdout);
    if (!pVolume->Open()) {
      wprintf(L"Open failed for %c:\n", wch);
      fflush(stdout);
      return 1;
    }

    rgVolumes.push_back(pVolume);
  }

  wprintf(L"Starting load on all volumes...\n");
  fflush(stdout);
  for (volume::CVolume *pVolume : rgVolumes) {
    pVolume->StartLoadAsync();
  }

  for (int i = 0; i < 180; ++i) {
    Sleep(1000);

    bool bAllDone = true;
    wprintf(L"[t=%ds]", i + 1);
    for (size_t j = 0; j < rgVolumes.size(); ++j) {
      volume::CVolume *pVolume = rgVolumes[j];
      const volume::VOLUME_STATE state = pVolume->GetState();
      const DWORD cRecords = pVolume->GetEnumeratedRecordCount();
      wprintf(L" %c:state=%d,records=%u,ready=%d", rgDrives[j], static_cast<int>(state), cRecords, pVolume->IsReadyForSearch() ? 1 : 0);

      if (!pVolume->IsReadyForSearch() && state != volume::VOLUME_STATE_ERROR) {
        bAllDone = false;
      }
    }
    wprintf(L"\n");
    fflush(stdout);

    if (bAllDone) {
      break;
    }
  }

  for (size_t j = 0; j < rgVolumes.size(); ++j) {
    const index::INDEX_STATS stats = rgVolumes[j]->GetLastIndexStats();
    wprintf(L"Final %c: nodes=%u searchEntries=%u poolUsed=%u unresolvedParents=%u\n", rgDrives[j], stats.m_cNodes, stats.m_cSearchEntries, stats.m_cbPoolUsed, stats.m_cUnresolvedParents);
    fflush(stdout);
  }

  manager.StopAllAndWait();
  wprintf(L"Done.\n");
  fflush(stdout);
  return 0;
}
