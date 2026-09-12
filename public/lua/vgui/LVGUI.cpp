//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Basic header for using vgui
//
// $NoKeywords: $
//=============================================================================//

#define lvgui_cpp

#include "cbase.h"
#include "lua.hpp"
#include "luasrclib.h"
#include "LVGUI.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


using namespace vgui;


/*
** access functions (stack -> C)
*/


LUA_API lua_HScheme lua_toscheme (lua_State *L, int idx) {
  lua_HScheme hScheme = *(lua_HScheme *)lua_touserdata(L, idx);
  return hScheme;
}


LUA_API lua_HFont lua_tofont (lua_State *L, int idx) {
  lua_HFont hFont = *(lua_HFont *)lua_touserdata(L, idx);
  return hFont;
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushscheme (lua_State *L, HScheme hScheme) {
  lua_HScheme *phScheme = (lua_HFont *)lua_newuserdata(L, sizeof(hScheme));
  *phScheme = hScheme;
  luaL_getmetatable(L, "HScheme");
  lua_setmetatable(L, -2);
}


LUA_API void lua_pushfont (lua_State *L, HFont hFont) {
  lua_HFont *phFont = (lua_HFont *)lua_newuserdata(L, sizeof(hFont));
  *phFont = hFont;
  luaL_getmetatable(L, "HFont");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_HScheme luaL_checkscheme (lua_State *L, int narg) {
  lua_HScheme *d = (lua_HScheme *)luaL_checkudata(L, narg, "HScheme");
  return *d;
}


LUALIB_API lua_HFont luaL_checkfont (lua_State *L, int narg) {
  //-----------------------------------------------------------------------------
  // HL2SB: accept GMod's font NAME as well as an HFont userdata.
  //
  // Every font binding in this fork goes through this one function --
  // public/lua/vgui/LISurface.cpp (DrawSetTextFont :305, GetTextSize :648,
  // SetFontGlyphSet :831, ...) and game/client/lua/scripted_controls/*.cpp
  // (lLabel.cpp:173 Label:SetFont, lTextEntry.cpp:318 TextEntry:SetFont,
  // lTextEntry.cpp:638 its fallback font, ...) -- so resolving the name HERE
  // fixes the whole class at once instead of adding one wrapper per object
  // class.  GMod's Lua passes names everywhere:
  //
  //     lua/derma/init.lua            surface.CreateFont( "DermaDefault", {...} )
  //     lua/vgui/dtextentry.lua:60    self:SetFont( "DermaDefault" )
  //     lua/vgui/dlabel.lua:39        self:SetFont( "DermaDefault" )
  //     lua/vgui/dbutton.lua          self:SetFont( "DermaDefault" )
  //
  // Before this, a string produced
  //     bad argument #1 to 'SetFont' (HFont expected, got string)
  // which is what aborted the spawnmenu build (spawnmenu.lua:151 ToolToggle
  // x66 is the half-built panel's Think, not a second bug).
  //
  // lua/includes/init.lua had already papered over this for the Panel and Label
  // metatables only; that is exactly why DTextEntry -- whose base is the
  // engine's TextEntry -- still died.  Those Lua wrappers stay (they are
  // harmless: they resolve to an HFont first and this accepts either), but they
  // are no longer load-bearing.
  //
  // Resolution and caching: LuaFont_ResolveByName (LISurface.cpp) consults the
  // per-Lua-state "hl2sb_lua_fonts" registry that surface.CreateFont( name,
  // fontData ) fills -- so a Derma font costs one hash lookup -- then the active
  // scheme, then the scheme's "Default".  Returns 0 only if even that is gone.
  //-----------------------------------------------------------------------------
  if ( lua_type( L, narg ) == LUA_TSTRING ) {
    const char *szName = lua_tostring( L, narg );
    lua_HFont hFont = LuaFont_ResolveByName( L, szName );

    if ( hFont != 0 )
      return hFont;

    // Nothing by that name anywhere: keep the old, explicit failure.
    char szMsg[256];
    Q_snprintf( szMsg, sizeof( szMsg ), "unknown font name '%s'", szName );
    luaL_argerror( L, narg, szMsg );
    return 0;  // not reached
  }

  lua_HFont *d = (lua_HFont *)luaL_checkudata(L, narg, "HFont");
  return *d;
}


static int HScheme___tostring (lua_State *L) {
  HScheme hScheme = luaL_checkscheme(L, 1);
  lua_pushfstring(L, "HScheme: %d", hScheme);
  return 1;
}


static const luaL_Reg HSchememeta[] = {
  {"__tostring", HScheme___tostring},
  {NULL, NULL}
};


/*
** Open HScheme object
*/
LUALIB_API int luaopen_HScheme (lua_State *L) {
  luaL_newmetatable(L, LUA_HSCHEMELIBNAME);
  luaL_register(L, NULL, HSchememeta);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  lua_pushstring(L, "scheme");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "scheme" */
  return 1;
}


static int HFont___tostring (lua_State *L) {
  HFont hFont = luaL_checkfont(L, 1);
  if (hFont == INVALID_FONT)
    lua_pushstring(L, "INVALID_FONT");
  else
    lua_pushfstring(L, "HFont: %d", hFont);
  return 1;
}


static const luaL_Reg HFontmeta[] = {
  {"__tostring", HFont___tostring},
  {NULL, NULL}
};


/*
** Open HFont object
*/
LUALIB_API int luaopen_HFont (lua_State *L) {
  luaL_newmetatable(L, LUA_FONTLIBNAME);
  luaL_register(L, NULL, HFontmeta);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  lua_pushstring(L, "font");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "font" */
  lua_pushfont(L, INVALID_FONT);
  lua_setglobal(L, "INVALID_FONT");  /* set global INVALID_FONT */
  return 1;
}
