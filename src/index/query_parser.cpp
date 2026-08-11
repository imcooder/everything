#include "index/query_parser.h"

#include "index/utf8_convert.h"

namespace index {

namespace {

WCHAR ToUpperDrive(WCHAR wch) {
  if (wch >= L'a' && wch <= L'z') {
    return static_cast<WCHAR>(wch - (L'a' - L'A'));
  }

  return wch;
}

void TrimInPlace(std::wstring &wstr) {
  while (!wstr.empty() && (wstr.front() == L' ' || wstr.front() == L'\t')) {
    wstr.erase(wstr.begin());
  }

  while (!wstr.empty() && (wstr.back() == L' ' || wstr.back() == L'\t')) {
    wstr.pop_back();
  }
}

bool StartsWithIgnoreCase(LPCWSTR wszText, LPCWSTR wszPrefix) {
  if (wszText == nullptr || wszPrefix == nullptr) {
    return false;
  }

  while (*wszPrefix != L'\0') {
    WCHAR wchText = *wszText;
    WCHAR wchPrefix = *wszPrefix;

    if (wchText >= L'a' && wchText <= L'z') {
      wchText = static_cast<WCHAR>(wchText - (L'a' - L'A'));
    }

    if (wchPrefix >= L'a' && wchPrefix <= L'z') {
      wchPrefix = static_cast<WCHAR>(wchPrefix - (L'a' - L'A'));
    }

    if (wchText != wchPrefix) {
      return false;
    }

    ++wszText;
    ++wszPrefix;
  }

  return true;
}

bool LooksLikeDrivePath(LPCWSTR wszQuery) {
  if (wszQuery == nullptr) {
    return false;
  }

  const WCHAR wch = wszQuery[0];
  if (!((wch >= L'a' && wch <= L'z') || (wch >= L'A' && wch <= L'Z'))) {
    return false;
  }

  return wszQuery[1] == L':';
}

bool PathUtf8FromWide(LPCWSTR wszPath, std::vector<char> &rgPathUtf8) {
  if (wszPath == nullptr || wszPath[0] == L'\0') {
    rgPathUtf8.clear();
    return true;
  }

  const UINT32 cch = static_cast<UINT32>(wcslen(wszPath));
  return WideNameToUtf8(wszPath, static_cast<USHORT>(cch), rgPathUtf8);
}

bool SetFilenameFilter(LPCWSTR wszFilename, CParsedQuery &plan) {
  if (wszFilename == nullptr || wszFilename[0] == L'\0') {
    plan.m_bHasFilenameFilter = false;
    return true;
  }

  std::vector<char> rgUtf8;
  const UINT32 cch = static_cast<UINT32>(wcslen(wszFilename));
  if (!WideNameToUtf8(wszFilename, static_cast<USHORT>(cch), rgUtf8)) {
    return false;
  }

  plan.m_filenameMatcher.SetQueryUtf8(std::string(rgUtf8.data(), rgUtf8.size()));
  plan.m_bHasFilenameFilter = true;
  return true;
}

WCHAR ToLowerAscii(WCHAR wch) {
  if (wch >= L'A' && wch <= L'Z') {
    return static_cast<WCHAR>(wch + (L'a' - L'A'));
  }
  return wch;
}

// Splits "txt;doc;PNG" into lowercased UTF-8 pieces {"txt", "doc", "png"}, skipping empty pieces
// (e.g. a trailing ';' or "ext:;txt").
bool SetExtensionFilter(const std::wstring &wstrExtList, CParsedQuery &plan) {
  plan.m_rgExtensionFiltersLower.clear();

  size_t idxStart = 0;
  while (idxStart <= wstrExtList.size()) {
    const size_t idxSep = wstrExtList.find(L';', idxStart);
    const size_t idxEnd = (idxSep == std::wstring::npos) ? wstrExtList.size() : idxSep;

    if (idxEnd > idxStart) {
      std::wstring wstrPiece = wstrExtList.substr(idxStart, idxEnd - idxStart);
      for (WCHAR &wch : wstrPiece) {
        wch = ToLowerAscii(wch);
      }

      std::vector<char> rgUtf8;
      if (!PathUtf8FromWide(wstrPiece.c_str(), rgUtf8)) {
        return false;
      }
      plan.m_rgExtensionFiltersLower.emplace_back(rgUtf8.data(), rgUtf8.size());
    }

    if (idxSep == std::wstring::npos) {
      break;
    }
    idxStart = idxSep + 1;
  }

  plan.m_bHasExtensionFilter = !plan.m_rgExtensionFiltersLower.empty();
  return true;
}

bool ParseDrivePath(LPCWSTR wszPath, CParsedQuery &plan) {
  if (wszPath == nullptr || wszPath[0] == L'\0') {
    return true;
  }

  plan.m_wchPathDrive = ToUpperDrive(wszPath[0]);

  LPCWSTR wszRemainder = wszPath + 2;
  if (wszRemainder[0] == L'\\' || wszRemainder[0] == L'/') {
    ++wszRemainder;
  }

  return PathUtf8FromWide(wszRemainder, plan.m_rgPathUtf8);
}

} // namespace

bool ParseSearchQuery(LPCWSTR wszQuery, CParsedQuery &plan) {
  plan = CParsedQuery{};

  if (wszQuery == nullptr || wszQuery[0] == L'\0') {
    return true;
  }

  std::wstring wstrQuery = wszQuery;
  TrimInPlace(wstrQuery);

  if (wstrQuery.empty()) {
    return true;
  }

  if (StartsWithIgnoreCase(wstrQuery.c_str(), L"parent:")) {
    std::wstring wstrRemainder = wstrQuery.substr(7);
    TrimInPlace(wstrRemainder);

    const size_t idxSpace = wstrRemainder.find(L' ');
    std::wstring wstrPath;
    std::wstring wstrFilename;

    if (idxSpace == std::wstring::npos) {
      wstrPath = wstrRemainder;
    } else {
      wstrPath = wstrRemainder.substr(0, idxSpace);
      wstrFilename = wstrRemainder.substr(idxSpace + 1);
      TrimInPlace(wstrFilename);
    }

    TrimInPlace(wstrPath);
    if (wstrPath.empty()) {
      return false;
    }

    if (!ParseDrivePath(wstrPath.c_str(), plan)) {
      return false;
    }

    if (!SetFilenameFilter(wstrFilename.empty() ? nullptr : wstrFilename.c_str(), plan)) {
      return false;
    }

    return true;
  }

  if (StartsWithIgnoreCase(wstrQuery.c_str(), L"ext:")) {
    std::wstring wstrRemainder = wstrQuery.substr(4);
    TrimInPlace(wstrRemainder);

    const size_t idxSpace = wstrRemainder.find(L' ');
    std::wstring wstrExtList;
    std::wstring wstrFilename;

    if (idxSpace == std::wstring::npos) {
      wstrExtList = wstrRemainder;
    } else {
      wstrExtList = wstrRemainder.substr(0, idxSpace);
      wstrFilename = wstrRemainder.substr(idxSpace + 1);
      TrimInPlace(wstrFilename);
    }

    TrimInPlace(wstrExtList);
    if (wstrExtList.empty()) {
      return false;
    }

    if (!SetExtensionFilter(wstrExtList, plan)) {
      return false;
    }

    if (!SetFilenameFilter(wstrFilename.empty() ? nullptr : wstrFilename.c_str(), plan)) {
      return false;
    }

    return true;
  }

  if (LooksLikeDrivePath(wstrQuery.c_str())) {
    const size_t idxSpace = wstrQuery.find(L' ');
    std::wstring wstrPath;
    std::wstring wstrFilename;

    if (idxSpace != std::wstring::npos) {
      wstrPath = wstrQuery.substr(0, idxSpace);
      wstrFilename = wstrQuery.substr(idxSpace + 1);
      TrimInPlace(wstrFilename);
    } else {
      wstrPath = wstrQuery;
    }

    TrimInPlace(wstrPath);
    if (!ParseDrivePath(wstrPath.c_str(), plan)) {
      return false;
    }

    if (!SetFilenameFilter(wstrFilename.empty() ? nullptr : wstrFilename.c_str(), plan)) {
      return false;
    }

    return true;
  }

  return SetFilenameFilter(wstrQuery.c_str(), plan);
}

} // namespace index
