#pragma once

// Shared ATL/WTL bootstrap include order for every UI translation unit. WTL is
// header-only (vcpkg "wtl" port); ATL comes from the MSVC toolset itself. See
// scripts/setup + vcpkg.json ("wtl" depends on "atl"). Include this before any other
// atl*.h header.

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include <atlbase.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlcrack.h>
