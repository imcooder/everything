#include "index/utf8_convert.h"

namespace index {

bool WideNameToUtf8(LPCWSTR wszName, USHORT cchName, std::vector<char> &rgUtf8) {
  if (wszName == nullptr || cchName == 0) {
    rgUtf8.clear();
    return false;
  }

  const int cbRequired = WideCharToMultiByte(CP_UTF8, 0, wszName, cchName, nullptr, 0, nullptr, nullptr);

  if (cbRequired <= 0) {
    return false;
  }

  rgUtf8.resize(static_cast<size_t>(cbRequired));
  const int cbWritten = WideCharToMultiByte(CP_UTF8, 0, wszName, cchName, rgUtf8.data(), cbRequired, nullptr, nullptr);

  return cbWritten == cbRequired;
}

} // namespace index
