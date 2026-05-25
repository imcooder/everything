#include "volume/volume_manager.h"

#include "volume/search_all_coordinator.h"

#include "core/platform.h"

namespace volume {

CVolumeManager::CVolumeManager() = default;

CVolumeManager::~CVolumeManager() {
  StopAllAndWait();
}

void CVolumeManager::SetRecordCallback(core::UsnRecordCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_fnRecord = std::move(callback);

  for (auto &pair : m_mapVolumes) {
    if (pair.second != nullptr) {
      ApplyCallbacks(*pair.second);
    }
  }
}

void CVolumeManager::SetErrorCallback(core::VolumeErrorCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_fnError = std::move(callback);

  for (auto &pair : m_mapVolumes) {
    if (pair.second != nullptr) {
      ApplyCallbacks(*pair.second);
    }
  }
}

bool CVolumeManager::RefreshVolumes() {
  std::lock_guard<std::mutex> lock(m_mutex);

  const DWORD dwDrives = GetLogicalDrives();
  std::map<WCHAR, bool> mapPresent;

  for (WCHAR wch = L'A'; wch <= L'Z'; ++wch) {
    const DWORD dwMask = 1u << (wch - L'A');
    if ((dwDrives & dwMask) == 0) {
      continue;
    }

    if (!ShouldIncludeDrive(wch)) {
      continue;
    }

    mapPresent[wch] = true;

    if (m_mapVolumes.find(wch) == m_mapVolumes.end()) {
      auto pVolume = std::make_unique<CVolume>(wch);
      ApplyCallbacks(*pVolume);
      m_mapVolumes.emplace(wch, std::move(pVolume));
    }
  }

  for (auto it = m_mapVolumes.begin(); it != m_mapVolumes.end();) {
    if (mapPresent.find(it->first) == mapPresent.end()) {
      if (it->second != nullptr) {
        it->second->StopAndWait();
      }
      it = m_mapVolumes.erase(it);
    } else {
      ++it;
    }
  }

  return true;
}

void CVolumeManager::OpenAllAsync() {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto &pair : m_mapVolumes) {
    if (pair.second != nullptr) {
      pair.second->Open();
    }
  }
}

void CVolumeManager::StartLoadAllAsync() {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto &pair : m_mapVolumes) {
    if (pair.second != nullptr) {
      if (!pair.second->Open()) {
        continue;
      }
      pair.second->StartLoadAsync();
    }
  }
}

void CVolumeManager::StartMonitorAllAsync() {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto &pair : m_mapVolumes) {
    if (pair.second != nullptr) {
      if (!pair.second->Open()) {
        continue;
      }
      pair.second->StartMonitorAsync(0);
    }
  }
}

void CVolumeManager::StopAll() {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto &pair : m_mapVolumes) {
    if (pair.second != nullptr) {
      pair.second->StopAsync();
    }
  }
}

void CVolumeManager::StopAllAndWait() {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto &pair : m_mapVolumes) {
    if (pair.second != nullptr) {
      pair.second->StopAndWait();
    }
  }
}

CVolume *CVolumeManager::GetVolume(WCHAR wchDriveLetter) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto it = m_mapVolumes.find(wchDriveLetter);
  if (it == m_mapVolumes.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::vector<WCHAR> CVolumeManager::GetDriveLetters() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<WCHAR> letters;
  letters.reserve(m_mapVolumes.size());

  for (const auto &pair : m_mapVolumes) {
    letters.push_back(pair.first);
  }

  return letters;
}

void CVolumeManager::SearchAllAsync(SEARCH_REQUEST_ID ullRequestId, LPCWSTR wszQuery, std::shared_ptr<ISearchSink> pSink) {
  if (ullRequestId == SEARCH_REQUEST_ID_INVALID || pSink == nullptr) {
    return;
  }

  std::vector<CVolume *> rgReadyVolumes;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    rgReadyVolumes.reserve(m_mapVolumes.size());

    for (auto &pair : m_mapVolumes) {
      if (pair.second != nullptr && pair.second->IsReadyForSearch()) {
        rgReadyVolumes.push_back(pair.second.get());
      }
    }
  }

  if (rgReadyVolumes.empty()) {
    pSink->OnComplete(ullRequestId, true);
    return;
  }

  const auto pCoordinator = std::make_shared<CSearchAllCoordinator>(ullRequestId, std::move(pSink), static_cast<UINT32>(rgReadyVolumes.size()));

  for (CVolume *pVolume : rgReadyVolumes) {
    pVolume->SearchAsync(ullRequestId, wszQuery, pCoordinator);
  }
}

void CVolumeManager::CancelSearchAll(SEARCH_REQUEST_ID ullRequestId) {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto &pair : m_mapVolumes) {
    if (pair.second != nullptr) {
      pair.second->CancelSearch(ullRequestId);
    }
  }
}

bool CVolumeManager::ShouldIncludeDrive(WCHAR wchDriveLetter) const {
  return core::IsNtfsDriveLetter(wchDriveLetter);
}

void CVolumeManager::ApplyCallbacks(CVolume &volume) const {
  if (m_fnRecord) {
    volume.SetRecordCallback(m_fnRecord);
  }
  if (m_fnError) {
    volume.SetErrorCallback(m_fnError);
  }
}

} // namespace volume
