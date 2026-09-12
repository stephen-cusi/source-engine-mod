//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Basic header for using vgui
//
// $NoKeywords: $
//=============================================================================//

#ifndef LVGUI_H
#define LVGUI_H

#ifdef _WIN32
#pragma once
#endif

/* type for HScheme functions */
typedef vgui::HScheme lua_HScheme;


/* type for HFont functions */
typedef vgui::HFont lua_HFont;



/*
** access functions (stack -> C)
*/

LUA_API lua_HScheme     (lua_toscheme) (lua_State *L, int idx);
LUA_API lua_HFont     (lua_tofont) (lua_State *L, int idx);


/*
** push functions (C -> stack)
*/
LUA_API void  (lua_pushscheme) (lua_State *L, lua_HScheme hScheme);
LUA_API void  (lua_pushfont) (lua_State *L, lua_HFont hFont);



LUALIB_API lua_HScheme (luaL_checkscheme) (lua_State *L, int narg);
LUALIB_API lua_HFont (luaL_checkfont) (lua_State *L, int narg);

/*
** HL2SB: resolve a font NAME to an HFont the same way surface.SetFont does --
** the per-Lua-state registry filled in by surface.CreateFont, then the active
** scheme's font table, then the scheme's "Default".
**
** Defined in LISurface.cpp (that is where the registry lives) and used by
** luaL_checkfont() in LVGUI.cpp, so EVERY binding that takes a font -- on any
** object class -- accepts GMod's string form.  Returns 0 (INVALID_FONT) when
** nothing matches.
**
** lua_HFont, not bare HFont: this header is included before `using namespace
** vgui`, so a bare HFont is not even a known type here (and is ambiguous in
** LVGUI.cpp, which has both ::HFont and vgui::HFont in scope).
*/
LUA_API lua_HFont (LuaFont_ResolveByName) (lua_State *L, const char *szName);


#endif // LVGUI_H
