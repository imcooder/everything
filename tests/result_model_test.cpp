#include "app/ui/result_model.h"

#include <gtest/gtest.h>

namespace ui {
namespace {

ROW_DATA MakeRow(WCHAR wchDrive, UINT32 nodeId, const std::wstring &wstrName, const std::wstring &wstrFullPath) {
  ROW_DATA row;
  row.m_wchDrive = wchDrive;
  row.m_nodeId = nodeId;
  row.m_wstrName = wstrName;
  row.m_wstrFolder = L"C:\\Temp";
  row.m_wstrFullPath = wstrFullPath;
  return row;
}

TEST(ResultModelSort, SortByNameAscendingIsCaseInsensitive) {
  CResultModel model;
  model.AddRowIfAbsent(MakeRow(L'C', 1, L"banana.txt", L"C:\\banana.txt"));
  model.AddRowIfAbsent(MakeRow(L'C', 2, L"Apple.txt", L"C:\\Apple.txt"));
  model.AddRowIfAbsent(MakeRow(L'C', 3, L"cherry.txt", L"C:\\cherry.txt"));

  model.SortBy(CResultModel::SORT_BY_NAME, true);

  ASSERT_EQ(model.GetCount(), 3u);
  EXPECT_EQ(model.GetRow(0)->m_wstrName, L"Apple.txt");
  EXPECT_EQ(model.GetRow(1)->m_wstrName, L"banana.txt");
  EXPECT_EQ(model.GetRow(2)->m_wstrName, L"cherry.txt");
}

TEST(ResultModelSort, SortByNameDescending) {
  CResultModel model;
  model.AddRowIfAbsent(MakeRow(L'C', 1, L"banana.txt", L"C:\\banana.txt"));
  model.AddRowIfAbsent(MakeRow(L'C', 2, L"apple.txt", L"C:\\apple.txt"));
  model.AddRowIfAbsent(MakeRow(L'C', 3, L"cherry.txt", L"C:\\cherry.txt"));

  model.SortBy(CResultModel::SORT_BY_NAME, false);

  ASSERT_EQ(model.GetCount(), 3u);
  EXPECT_EQ(model.GetRow(0)->m_wstrName, L"cherry.txt");
  EXPECT_EQ(model.GetRow(1)->m_wstrName, L"banana.txt");
  EXPECT_EQ(model.GetRow(2)->m_wstrName, L"apple.txt");
}

TEST(ResultModelSort, SortByPathUsesFullPathNotName) {
  CResultModel model;
  model.AddRowIfAbsent(MakeRow(L'C', 1, L"same.txt", L"C:\\z_folder\\same.txt"));
  model.AddRowIfAbsent(MakeRow(L'C', 2, L"same.txt", L"C:\\a_folder\\same.txt"));

  model.SortBy(CResultModel::SORT_BY_PATH, true);

  ASSERT_EQ(model.GetCount(), 2u);
  EXPECT_EQ(model.GetRow(0)->m_wstrFullPath, L"C:\\a_folder\\same.txt");
  EXPECT_EQ(model.GetRow(1)->m_wstrFullPath, L"C:\\z_folder\\same.txt");
}

// SortBy rebuilds the key->index map after reordering rows; AddRowIfAbsent/RemoveRow/
// UpdateRowIfPresent all depend on that map staying correct or they'd silently corrupt state
// (duplicate rows, failed removals) the next time a live search event touches the model.
TEST(ResultModelSort, KeyToIndexMapStaysCorrectAfterSort) {
  CResultModel model;
  model.AddRowIfAbsent(MakeRow(L'C', 1, L"banana.txt", L"C:\\banana.txt"));
  model.AddRowIfAbsent(MakeRow(L'C', 2, L"apple.txt", L"C:\\apple.txt"));
  model.AddRowIfAbsent(MakeRow(L'C', 3, L"cherry.txt", L"C:\\cherry.txt"));

  model.SortBy(CResultModel::SORT_BY_NAME, true);
  ASSERT_EQ(model.GetRow(0)->m_wstrName, L"apple.txt");

  // Re-adding an existing key after the sort must still be recognized as a duplicate.
  EXPECT_FALSE(model.AddRowIfAbsent(MakeRow(L'C', 2, L"apple.txt", L"C:\\apple.txt")));
  EXPECT_EQ(model.GetCount(), 3u);

  // Removing the row that moved to index 0 after sort must succeed via its real key.
  EXPECT_TRUE(model.RemoveRow(PackRowKey(L'C', 2)));
  EXPECT_EQ(model.GetCount(), 2u);

  ROW_DATA updated = MakeRow(L'C', 3, L"cherry-renamed.txt", L"C:\\cherry-renamed.txt");
  EXPECT_TRUE(model.UpdateRowIfPresent(updated));
}

TEST(ResultModelSort, EmptyModelSortIsNoOp) {
  CResultModel model;
  model.SortBy(CResultModel::SORT_BY_NAME, true);
  EXPECT_EQ(model.GetCount(), 0u);
}

} // namespace
} // namespace ui
