#pragma once

#include "core/platform.h"

#include <string>
#include <vector>

namespace index {

bool WideNameToUtf8(LPCWSTR wszName, USHORT cchName, std::vector<char> &rgUtf8);

} // namespace index
