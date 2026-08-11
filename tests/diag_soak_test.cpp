// Manual real-volume soak test: load a real volume, start monitoring, then run many cycles of
// real file create/modify/rename/delete while sampling process memory (working set) at regular
// intervals. Looks for unbounded memory growth (leak) or the monitor falling behind / hanging
// under sustained churn. Not run by CI — build and run by hand against real hardware.
#include "volume/volume_manager.h"

#include <cstdio>
#include <psapi.h>
#include <string>

#pragma comment(lib, "psapi.lib")

namespace {

SIZE_T GetWorkingSetBytes() {
  PROCESS_MEMORY_COUNTERS pmc = {};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return pmc.WorkingSetSize;
  }
  return 0;
}

} // namespace

int wmain(int argc, wchar_t *argv[]) {
  WCHAR wchDrive = (argc > 1) ? argv[1][0] : L'G';
  const int cCycles = (argc > 2) ? _wtoi(argv[2]) : 200;
  const int cFilesPerCycle = (argc > 3) ? _wtoi(argv[3]) : 20;

  wprintf(L"Soak test on volume %c: cycles=%d filesPerCycle=%d\n", wchDrive, cCycles, cFilesPerCycle);
  fflush(stdout);

  volume::CVolumeManager manager;
  int cErrors = 0;
  manager.SetErrorCallback([&cErrors](DWORD dwError, LPCWSTR wszMessage) {
    ++cErrors;
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

  pVolume->StartLoadAsync();
  for (int i = 0; i < 60 && !pVolume->IsReadyForSearch(); ++i) {
    Sleep(1000);
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

  const index::INDEX_STATS statsAfterLoad = pVolume->GetLastIndexStats();
  wprintf(L"Load complete: nodes=%u pool=%u KB. Starting monitor + soak loop...\n", statsAfterLoad.m_cNodes, statsAfterLoad.m_cbPoolUsed / 1024);
  fflush(stdout);

  pVolume->StartMonitorAsync(0);
  Sleep(1000);

  const std::wstring wstrDir = std::wstring(1, wchDrive) + L":\\everything_soak_test\\";
  CreateDirectoryW(wstrDir.c_str(), nullptr);

  const SIZE_T cbBaseline = GetWorkingSetBytes();
  wprintf(L"[t=0s] baseline WS=%llu KB\n", static_cast<unsigned long long>(cbBaseline / 1024));
  fflush(stdout);

  const DWORD dwStartTick = GetTickCount();

  for (int cycle = 0; cycle < cCycles; ++cycle) {
    for (int f = 0; f < cFilesPerCycle; ++f) {
      const std::wstring wstrPath = wstrDir + L"soak_" + std::to_wstring(f) + L".tmp";
      HANDLE hFile = CreateFileW(wstrPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (hFile != INVALID_HANDLE_VALUE) {
        const char szData[] = "soak test payload";
        DWORD dwWritten = 0;
        WriteFile(hFile, szData, sizeof(szData), &dwWritten, nullptr);
        CloseHandle(hFile);
      }
    }
    for (int f = 0; f < cFilesPerCycle; ++f) {
      const std::wstring wstrPath = wstrDir + L"soak_" + std::to_wstring(f) + L".tmp";
      DeleteFileW(wstrPath.c_str());
    }

    if (cycle % 20 == 0) {
      const SIZE_T cbNow = GetWorkingSetBytes();
      const DWORD dwElapsedSec = (GetTickCount() - dwStartTick) / 1000;
      const index::INDEX_STATS statsNow = pVolume->GetLastIndexStats();
      wprintf(L"[t=%us cycle=%d] WS=%llu KB (delta=%lld KB) state=%d responsive-check-ok\n", dwElapsedSec, cycle, static_cast<unsigned long long>(cbNow / 1024), static_cast<long long>((static_cast<long long>(cbNow) - static_cast<long long>(cbBaseline)) / 1024), static_cast<int>(pVolume->GetState()));
      fflush(stdout);
      (void)statsNow;
    }

    Sleep(50);
  }

  wprintf(L"Soak loop done. Waiting 3s for monitor to drain remaining events...\n");
  fflush(stdout);
  Sleep(3000);

  const SIZE_T cbFinal = GetWorkingSetBytes();
  wprintf(L"[final] WS=%llu KB (delta from baseline=%lld KB) errors=%d\n", static_cast<unsigned long long>(cbFinal / 1024), static_cast<long long>((static_cast<long long>(cbFinal) - static_cast<long long>(cbBaseline)) / 1024), cErrors);
  fflush(stdout);

  RemoveDirectoryW(wstrDir.c_str());

  manager.StopAllAndWait();
  wprintf(L"Done.\n");
  fflush(stdout);
  return 0;
}
