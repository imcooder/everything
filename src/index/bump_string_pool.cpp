#include "index/bump_string_pool.h"

#include <cstring>

namespace index {

CBumpStringPool::CBumpStringPool(UINT32 cbChunkSize) : m_cbChunkSize(cbChunkSize), m_cbTotalAllocated(0), m_cbTotalUsed(0), m_cbLogicalUsed(0), m_cbDead(0) {}

UINT32 CBumpStringPool::Alloc(const char *pszUtf8, UINT32 cbLen) {
  if (pszUtf8 == nullptr || cbLen == 0) {
    return UINT32_MAX;
  }

  return AllocFromChunk(pszUtf8, cbLen);
}

const char *CBumpStringPool::GetPtr(UINT32 offset) const {
  if (offset == UINT32_MAX) {
    return nullptr;
  }

  UINT32 cbRemaining = offset;
  for (const CHUNK &chunk : m_rgChunks) {
    if (cbRemaining < chunk.m_cbUsed) {
      return chunk.m_data.data() + cbRemaining;
    }
    cbRemaining -= chunk.m_cbUsed;
  }

  return nullptr;
}

UINT32 CBumpStringPool::GetUsedBytes() const {
  return m_cbLogicalUsed;
}

UINT32 CBumpStringPool::GetAllocatedBytes() const {
  return m_cbTotalAllocated;
}

void CBumpStringPool::Reset() {
  m_rgChunks.clear();
  m_cbTotalAllocated = 0;
  m_cbTotalUsed = 0;
  m_cbLogicalUsed = 0;
  m_cbDead = 0;
}

bool CBumpStringPool::Compact() {
  // Phase 2: copy live strings into a new pool and remap node offsets.
  return false;
}

float CBumpStringPool::GetDeadRatio() const {
  if (m_cbTotalAllocated == 0) {
    return 0.0f;
  }

  return static_cast<float>(m_cbDead) / static_cast<float>(m_cbTotalAllocated);
}

UINT32 CBumpStringPool::AllocFromChunk(const char *pszUtf8, UINT32 cbLen) {
  // Reserve a trailing NUL after each string so GetPtr() (offset-only) always
  // returns a valid, correctly-bounded C string instead of running into the
  // next allocation's bytes.
  const UINT32 cbPhysical = cbLen + 1;
  const UINT32 offset = m_cbTotalUsed;

  if (m_rgChunks.empty() || m_rgChunks.back().m_cbUsed + cbPhysical > m_cbChunkSize) {
    CHUNK chunk;
    chunk.m_data.resize(m_cbChunkSize);
    chunk.m_cbUsed = 0;
    m_rgChunks.push_back(std::move(chunk));
    m_cbTotalAllocated += m_cbChunkSize;
  }

  CHUNK &chunk = m_rgChunks.back();
  memcpy(chunk.m_data.data() + chunk.m_cbUsed, pszUtf8, cbLen);
  chunk.m_data[chunk.m_cbUsed + cbLen] = '\0';
  chunk.m_cbUsed += cbPhysical;
  m_cbTotalUsed += cbPhysical;
  m_cbLogicalUsed += cbLen;

  return offset;
}

} // namespace index
