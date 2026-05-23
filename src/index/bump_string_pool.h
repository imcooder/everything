#pragma once

#include "index/i_string_pool_backend.h"

#include <memory>
#include <vector>

namespace index {

class CBumpStringPool : public IStringPoolBackend {
public:
  explicit CBumpStringPool(UINT32 cbChunkSize = 4 * 1024 * 1024);

  UINT32 Alloc(const char *pszUtf8, UINT32 cbLen) override;
  const char *GetPtr(UINT32 offset) const override;
  UINT32 GetUsedBytes() const override;
  UINT32 GetAllocatedBytes() const override;
  void Reset() override;

  bool Compact() override;
  float GetDeadRatio() const override;

private:
  struct CHUNK {
    std::vector<char> m_data;
    UINT32 m_cbUsed;
  };

  UINT32 AllocFromChunk(const char *pszUtf8, UINT32 cbLen);

  UINT32 m_cbChunkSize;
  std::vector<CHUNK> m_rgChunks;
  UINT32 m_cbTotalAllocated;
  UINT32 m_cbTotalUsed;
  UINT32 m_cbDead;
};

} // namespace index
