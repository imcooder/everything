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

TEST(ResultModelSort, SortBySizeIsNumericNotLexicographic) {
  CResultModel model;
  ROW_DATA small = MakeRow(L'C', 1, L"small.txt", L"C:\\small.txt");
  small.m_ullFileSize = 9;
  ROW_DATA big = MakeRow(L'C', 2, L"big.txt", L"C:\\big.txt");
  big.m_ullFileSize = 100; // lexicographically "100" < "9", numerically 100 > 9
  model.AddRowIfAbsent(small);
  model.AddRowIfAbsent(big);

  model.SortBy(CResultModel::SORT_BY_SIZE, true);

  ASSERT_EQ(model.GetCount(), 2u);
  EXPECT_EQ(model.GetRow(0)->m_wstrName, L"small.txt");
  EXPECT_EQ(model.GetRow(1)->m_wstrName, L"big.txt");
}

TEST(ResultModelSort, SortByTypeGroupsDirectoriesBeforeFilesAscending) {
  CResultModel model;
  ROW_DATA file = MakeRow(L'C', 1, L"report.txt", L"C:\\report.txt");
  ROW_DATA dir = MakeRow(L'C', 2, L"Backups", L"C:\\Backups");
  dir.m_bIsDirectory = true;
  model.AddRowIfAbsent(file);
  model.AddRowIfAbsent(dir);

  model.SortBy(CResultModel::SORT_BY_TYPE, true);

  ASSERT_EQ(model.GetCount(), 2u);
  EXPECT_EQ(model.GetRow(0)->m_wstrName, L"Backups");
  EXPECT_EQ(model.GetRow(1)->m_wstrName, L"report.txt");
}

TEST(ResultModelSort, SortByTypeFallsBackToExtension) {
  CResultModel model;
  ROW_DATA docx = MakeRow(L'C', 1, L"report.docx", L"C:\\report.docx");
  ROW_DATA txt = MakeRow(L'C', 2, L"notes.txt", L"C:\\notes.txt");
  model.AddRowIfAbsent(docx);
  model.AddRowIfAbsent(txt);

  model.SortBy(CResultModel::SORT_BY_TYPE, true);

  ASSERT_EQ(model.GetCount(), 2u);
  EXPECT_EQ(model.GetRow(0)->m_wstrName, L"report.docx"); // "docx" < "txt"
  EXPECT_EQ(model.GetRow(1)->m_wstrName, L"notes.txt");
}

TEST(ResultModelSort, SortByDateModifiedIsNumeric) {
  CResultModel model;
  ROW_DATA older = MakeRow(L'C', 1, L"older.txt", L"C:\\older.txt");
  older.m_ullModifiedTime = 100;
  ROW_DATA newer = MakeRow(L'C', 2, L"newer.txt", L"C:\\newer.txt");
  newer.m_ullModifiedTime = 200;
  model.AddRowIfAbsent(newer);
  model.AddRowIfAbsent(older);

  model.SortBy(CResultModel::SORT_BY_DATE_MODIFIED, true);

  ASSERT_EQ(model.GetCount(), 2u);
  EXPECT_EQ(model.GetRow(0)->m_wstrName, L"older.txt");
  EXPECT_EQ(model.GetRow(1)->m_wstrName, L"newer.txt");
}

// OnUpdated (rename-in-place or a metadata-only touch) must refresh size/date/isDirectory too,
// not just the path fields — otherwise a live content change would leave stale Size/Date
// Modified columns displayed until the next full reload.
TEST(ResultModelUpdate, UpdateRowIfPresentRefreshesMetadataFields) {
  CResultModel model;
  ROW_DATA original = MakeRow(L'C', 1, L"report.txt", L"C:\\report.txt");
  original.m_ullFileSize = 100;
  original.m_ullModifiedTime = 1000;
  original.m_bIsDirectory = false;
  model.AddRowIfAbsent(original);

  ROW_DATA updated = MakeRow(L'C', 1, L"report.txt", L"C:\\report.txt");
  updated.m_ullFileSize = 999;
  updated.m_ullModifiedTime = 2000;
  updated.m_bIsDirectory = false;
  ASSERT_TRUE(model.UpdateRowIfPresent(updated));

  const ROW_DATA *pRow = model.GetRow(0);
  ASSERT_NE(pRow, nullptr);
  EXPECT_EQ(pRow->m_ullFileSize, 999u);
  EXPECT_EQ(pRow->m_ullModifiedTime, 2000u);
}

} // namespace
} // namespace ui
