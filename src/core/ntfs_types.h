#pragma once

#include "core/platform.h"

#include <functional>

namespace core {

using UsnRecordCallback = std::function<void(const USN_RECORD_V2 &record)>;
using VolumeErrorCallback = std::function<void(DWORD dwError, LPCWSTR wszMessage)>;

struct VOLUME_IDENTITY {
  WCHAR m_wchDriveLetter;
  DWORD m_dwSerialNumber;
  ULONGLONG m_ullVolumeSerialFromBoot;
};

struct USN_JOURNAL_STATE {
  ULONGLONG m_ullJournalId;
  USN m_usnNext;
  USN m_usnFirst;
  USN m_usnLast;
};

} // namespace core
