#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#include <cstdio>

namespace core {

inline bool IsNtfsDriveLetter(WCHAR wchDrive) {
  if (wchDrive < L'A' || wchDrive > L'Z') {
    return false;
  }

  WCHAR wszRoot[4] = {wchDrive, L':', L'\\', L'\0'};
  if (GetDriveTypeW(wszRoot) != DRIVE_FIXED) {
    return false;
  }

  WCHAR wszFsName[16] = {};
  if (!GetVolumeInformationW(wszRoot, nullptr, 0, nullptr, nullptr, nullptr, wszFsName, static_cast<DWORD>(sizeof(wszFsName) / sizeof(WCHAR)))) {
    return false;
  }

  return wcscmp(wszFsName, L"NTFS") == 0;
}

inline void BuildVolumeDevicePath(WCHAR wchDrive, WCHAR *pwszOut, DWORD cchOut) {
  swprintf_s(pwszOut, cchOut, L"\\\\.\\%c:", wchDrive);
}

} // namespace core
