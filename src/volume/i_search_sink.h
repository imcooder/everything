#pragma once

#include "core/platform.h"

#include "volume/search_types.h"

namespace volume {

// Live search result callbacks.

//

// Threading (SearchAsync — single volume):

//   All callbacks run on that volume's I/O thread, serially. Implementations need

//   not be thread-safe unless shared across searches.

//

// Threading (SearchAllAsync — multi volume):

//   Each volume invokes the sink from its own I/O thread. CVolumeManager wraps the

//   user sink in CSearchAllCoordinator, which serializes forwards with a mutex so

//   the inner sink never receives concurrent calls. UI code should still PostMessage

//   (or equivalent) to the UI thread before touching HWND / ListView state.

//

// IsCancelled may be polled from any volume I/O thread while a search is active;

// keep cancellation state thread-safe (e.g. std::atomic<bool>) if it is shared.

class ISearchSink {

public:
  virtual ~ISearchSink() = default;

  virtual bool IsCancelled(SEARCH_REQUEST_ID ullRequestId) const = 0;

  virtual void OnBatch(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, const UINT32 *rgNodeIds, UINT32 cNodeIds) = 0;

  virtual void OnInitialScanComplete(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter) = 0;

  virtual void OnAdded(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) = 0;

  virtual void OnRemoved(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) = 0;

  // A node already in the result set was renamed but still matches (e.g. delta-a.txt ->
  // delta-b.txt while searching "delta"). Neither OnAdded nor OnRemoved fires for this case
  // since set membership did not change; sinks that cache display strings per node must
  // refresh them here to avoid showing the stale name.
  virtual void OnUpdated(SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) = 0;

  virtual void OnComplete(SEARCH_REQUEST_ID ullRequestId, bool bCancelled) = 0;
};

constexpr UINT32 SEARCH_STREAM_BATCH_SIZE = 256;

constexpr UINT32 SEARCH_SCAN_CHUNK_SIZE = 8192;

} // namespace volume
