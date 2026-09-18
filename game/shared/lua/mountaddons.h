//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ===========//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef MOUNTADDONS_H
#define MOUNTADDONS_H
#ifdef _WIN32
#pragma once
#endif

struct lua_State;

void MountAddons();

// HL2SB: live addon enable/disable for the main-menu Addons dialog.
//
// HL2SB_SetAddonEnabled() updates addons_disabled.txt and -- when no map is
// loaded -- remounts the addon's search path right away, answering true when
// the change is already in effect and false when it waits for the next start.
bool HL2SB_SetAddonEnabled( const char *pszAddonName, bool bEnabled );
bool HL2SB_AddonsApplyLive( void );

// Registers the two globals the Addons dialog uses (hl2sb_setaddon /
// hl2sb_addons_live) into a Lua state.  Only the GameUI state gets them.
void HL2SB_LuaRegisterAddons( lua_State *L );

#endif // MOUNTADDONS_H
