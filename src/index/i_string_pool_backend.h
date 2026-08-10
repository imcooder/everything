#pragma once

#include "core/platform.h"

#include <cstdint>
#include <vector>

namespace index {

// Append-only string pool; compaction/reuse added in a later phase.
class IStringPoolBackend {
public:
  virtual ~IStringPoolBackend() = default;

  virtual UINT32 Alloc(const char *pszUtf8, UINT32 cbLen) = 0;
  virtual const char *GetPtr(UINT32 offset) const = 0;
  virtual UINT32 GetUsedBytes() const = 0;
  virtual UINT32 GetAllocatedBytes() const = 0;

  virtual void Reset() = 0;

  virtual bool Compact() {
    return true;
  }
  virtual float GetDeadRatio() const {
    return 0.0f;
  }

  // On-disk persistence hooks (index::CIndexPersistence). ExportBytes() writes out the pool's
  // physical bytes in offset order (including any per-entry terminators the backend adds) and
  // returns the physical length; ImportBytes() must reconstruct a pool where GetPtr(offset)
  // returns byte-identical content, for every offset that was valid before export, without the
  // caller needing to know anything about chunk boundaries. cbLogicalLen is informational only
  // (feeds GetUsedBytes() after import) and does not affect GetPtr() correctness. Default no-op
  // bodies keep the interface backward compatible for any future backend that never persists.
  virtual UINT32 ExportBytes(std::vector<char> &rgOut) const {
    rgOut.clear();
    return 0;
  }
  virtual bool ImportBytes(const char *pData, UINT32 cbPhysicalLen, UINT32 cbLogicalLen) {
    (void)pData;
    (void)cbPhysicalLen;
    (void)cbLogicalLen;
    return false;
  }
};

} // namespace index
