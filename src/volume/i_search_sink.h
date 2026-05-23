#pragma once

#include "core/platform.h"
#include "volume/search_types.h"

namespace volume {

// Callback surface for a live search session (volume I/O thread).
// OnBatch / OnInitialScanComplete populate the initial result set; OnAdded / OnRemoved
// keep it current while the USN journal updates the index. OnComplete ends the session.
class ISearchSink {
public:
  virtual ~ISearchSink() = default;

  virtual bool IsCancelled(SEARCH_REQUEST_ID ullRequestId) const = 0;
  virtual void OnBatch(SEARCH_REQUEST_ID ullRequestId, const UINT32 *rgNodeIds, UINT32 cNodeIds) = 0;
  virtual void OnInitialScanComplete(SEARCH_REQUEST_ID ullRequestId) = 0;
  virtual void OnAdded(SEARCH_REQUEST_ID ullRequestId, UINT32 nodeId) = 0;
  virtual void OnRemoved(SEARCH_REQUEST_ID ullRequestId, UINT32 nodeId) = 0;
  virtual void OnComplete(SEARCH_REQUEST_ID ullRequestId, bool bCancelled) = 0;
};

constexpr UINT32 SEARCH_STREAM_BATCH_SIZE = 256;
constexpr UINT32 SEARCH_SCAN_CHUNK_SIZE = 8192;

} // namespace volume
