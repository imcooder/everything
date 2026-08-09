#pragma once

#include "core/platform.h"

#include <cstdint>

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
};

} // namespace index
