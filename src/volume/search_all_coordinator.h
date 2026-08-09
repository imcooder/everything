#pragma once

#include "volume/i_search_sink.h"

#include <atomic>

#include <memory>

#include <mutex>

namespace volume {

// Fan-out target: one ISearchSink shared by SearchAllAsync on each volume.

// Serializes forwards to the inner sink (mutex) so multi-disk callbacks never overlap.

// Aggregates OnComplete when every volume finishes.

class CSearchAllCoordinator : public ISearchSink {

public:
  CSearchAllCoordinator(SEARCH_REQUEST_ID ullRequestId, std::shared_ptr<ISearchSink> pInner, UINT32 cVolumes) : m_ullRequestId(ullRequestId), m_pInner(std::move(pInner)), m_cRemaining(cVolumes) {}

  bool IsCancelled(SEARCH_REQUEST_ID ullRequestId) const override {

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pInner == nullptr) {

      return true;
    }

    return m_pInner->IsCancelled(ullRequestId);
  }

  void OnBatch(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, const UINT32 *rgNodeIds, UINT32 cNodeIds) override {

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pInner != nullptr) {

      m_pInner->OnBatch(ullRequestId, wchDriveLetter, rgNodeIds, cNodeIds);
    }
  }

  void OnInitialScanComplete(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter) override {

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pInner != nullptr) {

      m_pInner->OnInitialScanComplete(ullRequestId, wchDriveLetter);
    }
  }

  void OnAdded(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) override {

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pInner != nullptr) {

      m_pInner->OnAdded(ullRequestId, wchDriveLetter, nodeId);
    }
  }

  void OnRemoved(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) override {

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pInner != nullptr) {

      m_pInner->OnRemoved(ullRequestId, wchDriveLetter, nodeId);
    }
  }

  void OnUpdated(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) override {

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pInner != nullptr) {

      m_pInner->OnUpdated(ullRequestId, wchDriveLetter, nodeId);
    }
  }

  void OnComplete(SEARCH_REQUEST_ID ullRequestId, bool bCancelled) override {

    if (bCancelled) {

      m_bAnyCancelled.store(true, std::memory_order_relaxed);
    }

    if (m_cRemaining.fetch_sub(1, std::memory_order_acq_rel) != 1) {

      return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pInner != nullptr) {

      m_pInner->OnComplete(ullRequestId, m_bAnyCancelled.load(std::memory_order_relaxed));
    }
  }

private:
  SEARCH_REQUEST_ID m_ullRequestId;

  std::shared_ptr<ISearchSink> m_pInner;

  std::atomic<UINT32> m_cRemaining;

  std::atomic<bool> m_bAnyCancelled{false};

  mutable std::mutex m_mutex;
};

} // namespace volume
