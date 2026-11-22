//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: MFC-free precompiled-header replacement for the hammer2/core data
//			model library. Derived from hammer/stdafx.h with all afx* includes
//			removed; only the pieces the model layer actually uses remain.
//			Portable: Windows, Linux and macOS (no unconditional windows.h).
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef HC_STDAFX_H
#define HC_STDAFX_H

#if _MSC_VER > 1000
#pragma once
#endif

#define NO_THREAD_LOCAL 1

//
// Platform shell. The data model only needs the handful of Win32 base types
// its original MFC-era headers exposed (BOOL, DWORD, COLORREF, TCHAR types).
// On POSIX the engine's tier0 headers supply most of them (basetypes.h:
// BOOL/TRUE/FALSE/BYTE; platform.h: DWORD, stricmp -> strcasecmp); the few
// remaining GDI/TCHAR types are defined below with the same semantics as
// their Win32 counterparts.
//
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>

#elif defined(__APPLE__)

// macOS: POSIX path. Provide the Win32-side types the model layer inherited.
#include <stdint.h>
#include <string.h>
#include <strings.h>	// strcasecmp, used by tier0/platform.h's stricmp mapping
typedef uint32_t COLORREF;
typedef const char *LPCTSTR;
typedef char *LPTSTR;
typedef unsigned int UINT;
#define RGB(r,g,b)		((COLORREF)(((uint8_t)(r)) | (((uint32_t)(g)) << 8) | (((uint32_t)(b)) << 16)))
#define GetRValue(color)	((uint8_t)((color) & 0xFF))
#define GetGValue(color)	((uint8_t)(((color) >> 8) & 0xFF))
#define GetBValue(color)	((uint8_t)(((color) >> 16) & 0xFF))

#else

// POSIX (Linux, BSD, ...): same as the macOS branch.
#include <stdint.h>
#include <string.h>
#include <strings.h>	// strcasecmp, used by tier0/platform.h's stricmp mapping
typedef uint32_t COLORREF;
typedef const char *LPCTSTR;
typedef char *LPTSTR;
typedef unsigned int UINT;
#define RGB(r,g,b)		((COLORREF)(((uint8_t)(r)) | (((uint32_t)(g)) << 8) | (((uint32_t)(b)) << 16)))
#define GetRValue(color)	((uint8_t)((color) & 0xFF))
#define GetGValue(color)	((uint8_t)(((color) >> 8) & 0xFF))
#define GetBValue(color)	((uint8_t)(((color) >> 16) & 0xFF))

#endif

#include "tier0/wchartypes.h"
#include "tier0/platform.h"
#include "tier0/basetypes.h"
#include "tier0/dbg.h"
#include "tier0/vprof.h"

// Some VS header files provoke this warning
#pragma warning(disable : 4201) // warning C4201: nonstandard extension used : nameless struct/union

#pragma warning(push)
#pragma warning(disable:4018)
#pragma warning(push, 1)
#pragma warning(disable:4701 4702 4530)
#include <fstream>
#pragma warning(pop)
#pragma warning(pop)

#endif // HC_STDAFX_H
