// Ad-hoc diagnostic: load one real volume, start USN journal monitoring, then create/rename/
// delete a real test file on that volume and confirm the live USN records are actually observed
// (not part of the normal build; compiled and run manually).
#include "volume/volume_manager.h"

#include <cstdio>
#include <string>

namespace {

std::wstring ReasonToString(DWORD dwReason) {
  std::wstring wstr;
  if (dwReason & USN_REASON_FILE_CREATE) {
    wstr += L"CREATE ";
  }
  if (dwReason & USN_REASON_FILE_DELETE) {
    wstr += L"DELETE ";
  }
  if (dwReason & USN_REASON_RENAME_OLD_NAME) {
    wstr += L"RENAME_OLD ";
  }
  if (dwReason & USN_REASON_RENAME_NEW_NAME) {
    wstr += L"RENAME_NEW ";
  }
  if (dwReason & USN_REASON_DATA_EXTEND) {
    wstr += L"DATA_EXTEND ";
  }
  if (dwReason & USN_REASON_DATA_OVERWRITE) {
    wstr += L"DATA_OVERWRITE ";
  }
  if (dwReason & USN_REASON_CLOSE) {
    wstr += L"CLOSE ";
  }
  return wstr;
}

} // namespace

int wmain(int argc, wchar_t *argv[]) {
  WCHAR wchDrive = (argc > 1) ? argv[1][0] : L'G';

  wprintf(L"Diagnosing monitor on volume %c:\n", wchDrive);
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

  int cEventsSeen = 0;
  std::wstring wstrTestFileName = L"everything_diag_test_file.tmp";
  pVolume->SetRecordCallback([&](const USN_RECORD_V2 &record) {
    std::wstring wstrName(record.FileName, record.FileNameLength / sizeof(WCHAR));
    if (wstrName.find(L"everything_diag_test") != std::wstring::npos) {
      ++cEventsSeen;
      wprintf(L"  [USN EVENT] name=%s reason=%s\n", wstrName.c_str(), ReasonToString(record.Reason).c_str());
      fflush(stdout);
    }
  });

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
  wprintf(L"Load complete. Starting monitor...\n");
  fflush(stdout);

  pVolume->StartMonitorAsync(0);
  Sleep(1000);

  std::wstring wstrTestPath = std::wstring(1, wchDrive) + L":\\" + wstrTestFileName;
  std::wstring wstrRenamedPath = std::wstring(1, wchDrive) + L":\\everything_diag_test_renamed.tmp";

  wprintf(L"Creating %s\n", wstrTestPath.c_str());
  fflush(stdout);
  HANDLE hFile = CreateFileW(wstrTestPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    wprintf(L"CreateFile failed, error=%u\n", GetLastError());
    fflush(stdout);
    return 1;
  }
  const char szData[] = "hello everything";
  DWORD dwWritten = 0;
  WriteFile(hFile, szData, sizeof(szData), &dwWritten, nullptr);
  CloseHandle(hFile);
  Sleep(1500);

  wprintf(L"Renaming to %s\n", wstrRenamedPath.c_str());
  fflush(stdout);
  MoveFileW(wstrTestPath.c_str(), wstrRenamedPath.c_str());
  Sleep(1500);

  wprintf(L"Deleting %s\n", wstrRenamedPath.c_str());
  fflush(stdout);
  DeleteFileW(wstrRenamedPath.c_str());
  Sleep(1500);

  wprintf(L"Total test-file USN events observed: %d\n", cEventsSeen);
  fflush(stdout);

  manager.StopAllAndWait();
  wprintf(L"Done.\n");
  fflush(stdout);
  return cEventsSeen > 0 ? 0 : 2;
}
