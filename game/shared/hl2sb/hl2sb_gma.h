//========== Copyleft (c) 2026, HL2SB, Some rights reserved. ===========//
//
// Purpose: Garry's Mod .gma addon archive support for HL2SB.
//
// $NoKeywords: $
//===========================================================================//

#ifndef HL2SB_GMA_H
#define HL2SB_GMA_H
#ifdef _WIN32
#pragma once
#endif

// Scans addons/*.gma (pathID "MOD") and mounts every archive READ-ONLY and IN
// PLACE, GMod style: the filesystem treats each .gma as a search path of its
// own (CGmaPackFile in filesystem_stdio.dll) and reads files on demand straight
// out of the archive - nothing is extracted to disk.
//
//   * each archive is added to the MOD and GAME search paths, so Lua,
//     materials, models and everything else resolve in the same session,
//   * mounting is idempotent (same archive + same path ID = no-op).
//
// Everything is defensive: a truncated, malformed or hostile archive produces a
// warning inside the filesystem and is skipped.
//
// Safe (and harmless) to call from both the client and the server DLL, and
// again whenever an addon is re-enabled in the main menu.  Both realms must
// call it before their Lua passes so an addon's Lua is picked up as early as
// possible.
void HL2SB_MountGMAAddons();

// HL2SB: is this addon switched off in <gamedir>/addons_disabled.txt?  The list
// is written by the main menu's Addons dialog (lua/gameui/addonsdialog.lua);
// MountAddons() uses it to skip folders, HL2SB_MountGMAAddons() to skip
// archives.
bool HL2SB_IsAddonDisabled( const char *pszAddonName );

#endif // HL2SB_GMA_H
