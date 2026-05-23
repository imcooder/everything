#include "core/platform.h"
#include "index/bump_string_pool.h"

#include <gtest/gtest.h>

#include <cstring>

TEST(BumpStringPool, AllocAndReadBack) {
  index::CBumpStringPool pool(256);
  const char *pszText = "hello";
  const UINT32 offset = pool.Alloc(pszText, static_cast<UINT32>(strlen(pszText)));
  ASSERT_NE(offset, UINT32_MAX);

  const char *pszStored = pool.GetPtr(offset);
  ASSERT_NE(pszStored, nullptr);
  EXPECT_STREQ(pszStored, pszText);
  EXPECT_EQ(pool.GetUsedBytes(), strlen(pszText));
}

TEST(BumpStringPool, ResetClearsAllocations) {
  index::CBumpStringPool pool(256);
  pool.Alloc("a", 1);
  pool.Reset();
  EXPECT_EQ(pool.GetUsedBytes(), 0u);
  EXPECT_EQ(pool.GetAllocatedBytes(), 0u);
}

TEST(BumpStringPool, MultipleStrings) {
  index::CBumpStringPool pool(256);
  const UINT32 offA = pool.Alloc("alpha", 5);
  const UINT32 offB = pool.Alloc("beta", 4);
  ASSERT_NE(offA, offB);
  EXPECT_STREQ(pool.GetPtr(offA), "alpha");
  EXPECT_STREQ(pool.GetPtr(offB), "beta");
}
