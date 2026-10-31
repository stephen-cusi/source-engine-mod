//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#define lvgui_controls_cpp

#include "cbase.h"
#include "lua.hpp"
#include "lControls.h"
#include "lPanel.h"
#include "vgui/IInput.h"
#include "vgui/IPanel.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Panel.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/*
** ===========================================================================
** HL2SB: vgui.Create( className, parent, name )
**
** Garry's Mod implements this in C++ and its own
** lua/includes/extensions/client/panel/scriptedpanels.lua wraps it:
**
**     vgui.CreateX = vgui.Create                     -- captures the C++ one
**     function vgui.Create( classname, parent, name ) -- the class registry
**       ... return vgui.CreateX( classname, parent, name or classname )
**     end
**
** HL2SB's vgui table only ever had per-class factories (vgui.Panel,
** vgui.Label, vgui.Frame, ...), so `vgui.CreateX = vgui.Create` captured nil
** and GMod's scriptedpanels.lua could not run.  This is the dispatch it needs.
**
** The trailing argument differs per factory, so it is spelled out:
**
**     Panel (parent, name)                 ModelPanel      (parent, name)
**     TextEntry (parent, name)             Label           (parent, name, text)
**     Frame (parent, name, bShowClose)     CheckButton     (parent, name, text)
**     Button (parent, name, text, ...)     EditablePanel   (parent, name)
**
** Only Button hard-requires argument 3 (luaL_checkstring(L, 3) in
** luasrc_Button); the others take it optionally or ignore it, so passing "" or
** true is safe for every one of them.
** ===========================================================================
*/
static void lua_vgui_RememberPanel (lua_State *L, int nIndex);

static int lua_vgui_Create (lua_State *L) {
  const char *pszClass = luaL_checkstring(L, 1);
  const int nTop = lua_gettop(L);

  lua_getglobal(L, "vgui");
  if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }

  lua_getfield(L, -1, pszClass);
  lua_remove(L, -2);
  if (!lua_isfunction(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }

  const int nBase = lua_gettop(L);                       // [factory]
  for (int i = 2; i <= nTop && i <= 3; ++i)
    lua_pushvalue(L, i);                                 // parent, name
  while (lua_gettop(L) < nBase + 2)
    lua_pushnil(L);                                      // pad a missing parent/name

  if (!Q_stricmp(pszClass, "Frame"))                        lua_pushboolean(L, 1);
  else if (!Q_stricmp(pszClass, "Button"))                  lua_pushstring(L, "");
  else if (!Q_stricmp(pszClass, "Label"))                   lua_pushstring(L, "");
  else if (!Q_stricmp(pszClass, "CheckButton"))             lua_pushstring(L, "");

  lua_call(L, lua_gettop(L) - nBase, 1);
  lua_vgui_RememberPanel(L, -1);
  if (lua_isuserdata(L, -1)) {
    // Hover notifications are native; do not wrap a partially built base class.
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "HL2SBTooltipWired");
  }
  return 1;
}

/*
** ===========================================================================
** HL2SB: vgui.GetAll()
**
** Garry's Mod's engine returns a table of every panel created through
** vgui.Create / vgui.CreateX.  Its own Lua layer needs it in derma.lua's
** FindPanelsByClass() -- the "this control definition is being reloaded, so
** re-apply the new methods to every live instance" path -- and addons use it to
** find and close their own windows.
**
** This engine had no panel registry at all (no CUtlVector of Lua panels exists
** in public/lua/vgui_controls), so one is kept here: a weak-VALUED table in the
** Lua registry, appended to by the vgui.Create dispatch above.  Weak values mean
** a panel that was closed and collected drops out by itself, which is exactly
** what GMod's list does -- it only ever holds live panels.
** ===========================================================================
*/
static const char *s_pVguiPanelRegistry = "hl2sb_vgui_panels";

static void lua_vgui_RememberPanel (lua_State *L, int nIndex) {
  if (nIndex < 0) nIndex = lua_gettop(L) + nIndex + 1;
  if (!lua_istable(L, nIndex) && !lua_isuserdata(L, nIndex))
    return;                                     // Create returned nil (bad class name)

  lua_pushstring(L, s_pVguiPanelRegistry);
  lua_rawget(L, LUA_REGISTRYINDEX);
  if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

  lua_pushvalue(L, nIndex);
  lua_rawseti(L, -2, (int)lua_objlen(L, -2) + 1);
  lua_pop(L, 1);
}

/*
** HL2SB: build the registry table (with a weak-value metatable) once, when the
** vgui library is opened.  Stack balanced: everything pushed here is popped.
*/
static void lua_vgui_CreatePanelRegistry (lua_State *L) {
  lua_newtable(L);                              // [panels]
  lua_newtable(L);                              // [panels metatable]
  lua_pushstring(L, "v");
  lua_setfield(L, -2, "__mode");                // metatable.__mode = "v"
  lua_setmetatable(L, -2);                      // [panels]
  lua_pushstring(L, s_pVguiPanelRegistry);
  lua_pushvalue(L, -2);                         // [panels panels]
  lua_rawset(L, LUA_REGISTRYINDEX);             // registry[name] = panels, [panels]
  lua_pop(L, 1);                                // []
}

static int lua_vgui_GetAll (lua_State *L) {
  lua_pushstring(L, s_pVguiPanelRegistry);
  lua_rawget(L, LUA_REGISTRYINDEX);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);                            // never nil: derma.lua pairs() it
  }
  return 1;
}

/*
** HL2SB: vgui.FocusedHasParent( panel )
**
** GMod's DFrame:IsActive() (lua/vgui/DFrame.lua:115) asks whether the keyboard
** focus currently sits on the frame or on one of its children; without this
** every DFrame reports itself inactive and its title bar renders with the
** "inactive" colours.  Walk up the focus chain instead of adding anything to
** ISurface.
*/
static int lua_vgui_FocusedHasParent (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);

  if (!pPanel || !vgui::input() || !vgui::ipanel()) {
    lua_pushboolean(L, 0);
    return 1;
  }

  VPANEL hTarget = pPanel->GetVPanel();
  VPANEL hFocused = vgui::input()->GetFocus();
  bool bFound = false;

  while (hFocused != 0) {
    if (hFocused == hTarget) { bFound = true; break; }
    hFocused = vgui::ipanel()->GetParent(hFocused);
  }

  lua_pushboolean(L, bFound ? 1 : 0);
  return 1;
}

static int lua_vgui_CursorVisible (lua_State *L) {
  lua_pushboolean(L, vgui::surface() && vgui::surface()->IsCursorVisible());
  return 1;
}

static int lua_vgui_GetKeyboardFocus (lua_State *L) {
  VPANEL focused = vgui::input()->GetFocus();
  if (!focused) return 0;
  lua_pushpanel(L, focused);
  if (!lua_topanel(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  return 1;
}

static const luaL_Reg vgui_funcs[] = {
  {"Create", lua_vgui_Create},
  {"GetAll", lua_vgui_GetAll},
  {"FocusedHasParent", lua_vgui_FocusedHasParent},
  {"GetKeyboardFocus", lua_vgui_GetKeyboardFocus},
  {"CursorVisible", lua_vgui_CursorVisible},
  {NULL, NULL}
};

extern int HL2SB_LuaCreateHTML(lua_State *L);
extern int HL2SB_LuaCreateEditablePanel(lua_State *L);

/*
** Open vgui library
*/
LUALIB_API int luaopen_vgui (lua_State *L) {
  lua_vgui_CreatePanelRegistry(L);

  luaopen_vgui_Button(L);
  luaopen_vgui_EditablePanel(L);
  luaopen_vgui_Panel(L);
  luaopen_vgui_CheckButton(L);
  luaopen_vgui_Frame(L);
  luaopen_vgui_PropertyDialog(L);
  luaopen_vgui_PropertyPage(L);
  luaopen_vgui_ModelPanel(L);

  luaL_register(L, "vgui", vgui_funcs);
  lua_pop(L, 1);

  // EditablePanel retains scripted callbacks and owns its keyboard subfocus.
  lua_getglobal(L, "vgui");
  if (lua_istable(L, -1)) {
    lua_pushcfunction(L, HL2SB_LuaCreateHTML);
    lua_setfield(L, -2, "HTML");
    lua_pushcfunction(L, HL2SB_LuaCreateEditablePanel);
    lua_setfield(L, -2, "EditablePanel");
  }
  lua_pop(L, 1);

  return 0;
}

