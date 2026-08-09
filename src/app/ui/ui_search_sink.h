#pragma once

// ISearchSink implementation for the WTL UI. Every method here executes ON THE MATCHING
// VOLUME'S OWN I/O THREAD (see src/app/main.cpp CSampleSearchSink comment,
// src/volume/i_search_sink.h threading notes, and CSearchAllCoordinator which serializes
// but does not change the calling thread). This class must never touch a WTL/Win32
// control; it only builds plain data and PostMessage()s it to the UI thread — see
// app_messages.h for the message contract and CMainFrame for the receiving handlers.

#include "app/ui/app_messages.h"
#include "volume/i_search_sink.h"
#include "volume/volume_manager.h"

#include <atomic>
#include <memory>

namespace ui {

class CUiSearchSink : public volume::ISearchSink {
public:
  CUiSearchSink(HWND hWndTarget, volume::CVolumeManager *pManager, std::shared_ptr<std::atomic<volume::SEARCH_REQUEST_ID>> pActiveRequestId);

  bool IsCancelled(volume::SEARCH_REQUEST_ID ullRequestId) const override;
  void OnBatch(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, const UINT32 *rgNodeIds, UINT32 cNodeIds) override;
  void OnInitialScanComplete(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter) override;
  void OnAdded(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) override;
  void OnRemoved(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) override;
  void OnUpdated(volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDriveLetter, UINT32 nodeId) override;
  void OnComplete(volume::SEARCH_REQUEST_ID ullRequestId, bool bCancelled) override;

private:
  // Materializes the full path for nodeId (safe here: we're on that volume's own I/O
  // thread) and splits it into leaf name / containing folder for display. Returns false
  // if the node can no longer be resolved (e.g. deleted between match and here).
  bool BuildRow(WCHAR wchDrive, UINT32 nodeId, ROW_DATA &outRow) const;

  void PostSingleRow(int message, volume::SEARCH_REQUEST_ID ullRequestId, WCHAR wchDrive, UINT32 nodeId) const;

  HWND m_hWndTarget;
  volume::CVolumeManager *m_pManager;
  std::shared_ptr<std::atomic<volume::SEARCH_REQUEST_ID>> m_pActiveRequestId;
};

} // namespace ui
