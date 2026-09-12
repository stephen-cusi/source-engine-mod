//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#define lpanel_cpp

#include "cbase.h"
#include <vgui_controls/Panel.h>
#include "c_vguiscreen.h"
#include "iclientmode.h"
#include "ienginevgui.h"
#include <vgui/IVGui.h>
#include <vgui/IInput.h>
#include <vgui/ISurface.h>
#include <vgui/IPanel.h>
#include <vgui/Cursor.h>
#include "panelmetaclassmgr.h"
#include <vgui_controls/PHandle.h>
#include "luamanager.h"
#include "vgui_int.h"
#include "lPanel.h"
#include <scripted_controls/lPanel.h>
#include "lColor.h"

using namespace vgui;


/*
** access functions (stack -> C)
*/


LUA_API lua_Panel *lua_topanel (lua_State *L, int idx) {
  PHandle *phPanel = dynamic_cast<PHandle *>((PHandle *)lua_touserdata(L, idx));
  if (phPanel == NULL)
    return NULL;
  return (lua_Panel *)phPanel->Get();
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushpanel (lua_State *L, Panel *pPanel) {
  LPanel *plPanel = dynamic_cast<LPanel *>(pPanel);
  if (plPanel)
    ++plPanel->m_nRefCount;
  PHandle *phPanel = (PHandle *)lua_newuserdata(L, sizeof(PHandle));
  phPanel->Set(pPanel);
  luaL_getmetatable(L, "Panel");
  lua_setmetatable(L, -2);
}


LUA_API void lua_pushpanel (lua_State *L, VPANEL panel) {
  PHandle *phPanel = (PHandle *)lua_newuserdata(L, sizeof(PHandle));
  phPanel->Set(ivgui()->PanelToHandle(panel));
  luaL_getmetatable(L, "Panel");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_Panel *luaL_checkpanel (lua_State *L, int narg) {
  lua_Panel *d = lua_topanel(L, narg);
  if (d == NULL)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "Panel expected, got INVALID_PANEL");
  return d;
}


LUALIB_API VPANEL luaL_checkvpanel (lua_State *L, int narg) {
  lua_Panel *d = lua_topanel(L, narg);
  if (d == NULL)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "Panel expected, got INVALID_PANEL");
  PHandle hPanel;
  hPanel.Set(d);
  return ivgui()->HandleToPanel(hPanel.m_iPanelID);
}


LUALIB_API lua_Panel *luaL_optpanel (lua_State *L, int narg,
                                                   Panel *def) {
  return luaL_opt(L, luaL_checkpanel, narg, def);
}


static int Panel_AddKeyBinding (lua_State *L) {
  luaL_checkpanel(L, 1)->AddKeyBinding(luaL_checkstring(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int Panel_AddActionSignalTarget (lua_State *L) {
  luaL_checkpanel(L, 1)->AddActionSignalTarget(luaL_checkpanel(L, 2));
  return 0;
}

static int Panel_CanStartDragging (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->CanStartDragging(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5)));
  return 1;
}

static int Panel_ChainToAnimationMap (lua_State *L) {
  luaL_checkpanel(L, 1)->ChainToAnimationMap();
  return 0;
}

static int Panel_ChainToMap (lua_State *L) {
  luaL_checkpanel(L, 1)->ChainToMap();
  return 0;
}

static int Panel_DeletePanel (lua_State *L) {
  luaL_checkpanel(L, 1)->DeletePanel();
  return 0;
}

static int Panel_DisableMouseInputForThisPanel (lua_State *L) {
  luaL_checkpanel(L, 1)->DisableMouseInputForThisPanel(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_DrawBox (lua_State *L) {
  luaL_checkpanel(L, 1)->DrawBox(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), luaL_checkcolor(L, 6), luaL_checknumber(L, 7), luaL_optboolean(L, 8, 0));
  return 0;
}

static int Panel_DrawBoxFade (lua_State *L) {
  luaL_checkpanel(L, 1)->DrawBoxFade(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), luaL_checkcolor(L, 6), luaL_checknumber(L, 7), luaL_checkint(L, 8), luaL_checkint(L, 9), luaL_checkboolean(L, 10), luaL_optboolean(L, 11, 0));
  return 0;
}

static int Panel_DrawHollowBox (lua_State *L) {
  luaL_checkpanel(L, 1)->DrawHollowBox(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), luaL_checkcolor(L, 6), luaL_checknumber(L, 7));
  return 0;
}

static int Panel_DrawTexturedBox (lua_State *L) {
  luaL_checkpanel(L, 1)->DrawTexturedBox(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), luaL_checkcolor(L, 6), luaL_checknumber(L, 7));
  return 0;
}

static int Panel_EditKeyBindings (lua_State *L) {
  luaL_checkpanel(L, 1)->EditKeyBindings();
  return 0;
}

static int Panel_FillRectSkippingPanel (lua_State *L) {
  luaL_checkpanel(L, 1)->FillRectSkippingPanel(luaL_checkcolor(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), luaL_checkint(L, 6), luaL_checkpanel(L, 7));
  return 0;
}

static int Panel_FindChildByName (lua_State *L) {
  lua_pushpanel(L, luaL_checkpanel(L, 1)->FindChildByName(luaL_checkstring(L, 2), luaL_optboolean(L, 3, 0)));
  return 1;
}

static int Panel_FindChildIndexByName (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->FindChildIndexByName(luaL_checkstring(L, 2)));
  return 1;
}

static int Panel_FindSiblingByName (lua_State *L) {
  lua_pushpanel(L, luaL_checkpanel(L, 1)->FindSiblingByName(luaL_checkstring(L, 2)));
  return 1;
}

static int Panel_GetAlpha (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetAlpha());
  return 1;
}

static int Panel_GetBgColor (lua_State *L) {
  lua_pushcolor(L, luaL_checkpanel(L, 1)->GetBgColor());
  return 1;
}

static int Panel_GetBounds (lua_State *L) {
  int x, y, wide, tall;
  luaL_checkpanel(L, 1)->GetBounds(x, y, wide, tall);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 4;
}

static int Panel_GetChild (lua_State *L) {
  lua_pushpanel(L, luaL_checkpanel(L, 1)->GetChild(luaL_checkint(L, 2)));
  return 1;
}

static int Panel_GetChildCount (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetChildCount());
  return 1;
}

/*
** HL2SB: Panel:GetChildren() -- GMod's list-of-children accessor, which this
** fork never bound.  GMod's own lua/includes/extensions/client/panel.lua uses it
** in InvalidateChildren/Clear/MoveToAfter/MoveToBefore/GetClosestChild, and the
** ported Derma controls use it in their layout/animation code:
**
**   dcategorycollapse.lua:255  #self.Contents:GetChildren() > 0  (spawnmenu
**                              categories -- decides whether the open category
**                              sizes itself to its contents)
**   dcategorycollapse.lua:291  sums child heights while sliding open
**   diconlayout.lua / dtilelayout.lua / dmenu.lua / dtree_node.lua / ...
**
** Same push as Panel_GetChild/Panel_GetParent (a fresh wrapper per call; the
** engine has no per-panel userdata cache), which is enough for the way GMod uses
** the result: iterate, read sizes, SetZPos, Remove, InvalidateChildren.
*/
static int Panel_GetChildren (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  int nChildren = pPanel->GetChildCount();

  lua_createtable(L, nChildren, 0);
  for (int i = 0; i < nChildren; ++i) {
    Panel *pChild = pPanel->GetChild(i);
    if (pChild == NULL)
      continue;
    lua_pushpanel(L, pChild);
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

static int Panel_GetClassName (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->GetClassName());
  return 1;
}

static int Panel_GetClipRect (lua_State *L) {
  int x0, y0, x1, y1;
  luaL_checkpanel(L, 1)->GetClipRect(x0, y0, x1, y1);
  lua_pushinteger(L, x0);
  lua_pushinteger(L, y0);
  lua_pushinteger(L, x1);
  lua_pushinteger(L, y1);
  return 4;
}

static int Panel_GetCornerTextureSize (lua_State *L) {
  int w, h;
  luaL_checkpanel(L, 1)->GetCornerTextureSize(w, h);
  lua_pushinteger(L, w);
  lua_pushinteger(L, h);
  return 2;
}

static int Panel_GetDescription (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->GetDescription());
  return 1;
}

static int Panel_GetDragFrameColor (lua_State *L) {
  lua_pushcolor(L, luaL_checkpanel(L, 1)->GetDragFrameColor());
  return 1;
}

static int Panel_GetDragPanel (lua_State *L) {
  lua_pushpanel(L, luaL_checkpanel(L, 1)->GetDragPanel());
  return 1;
}

static int Panel_GetDragStartTolerance (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetDragStartTolerance());
  return 1;
}

static int Panel_GetDropFrameColor (lua_State *L) {
  lua_pushcolor(L, luaL_checkpanel(L, 1)->GetDropFrameColor());
  return 1;
}

static int Panel_GetFgColor (lua_State *L) {
  lua_pushcolor(L, luaL_checkpanel(L, 1)->GetFgColor());
  return 1;
}

static int Panel_GetInset (lua_State *L) {
  int left, top, right, bottom;
  luaL_checkpanel(L, 1)->GetInset(left, top, right, bottom);
  lua_pushinteger(L, left);
  lua_pushinteger(L, top);
  lua_pushinteger(L, right);
  lua_pushinteger(L, bottom);
  return 4;
}

static int Panel_GetKeyBindingsFile (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->GetKeyBindingsFile());
  return 1;
}

static int Panel_GetKeyBindingsFilePathID (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->GetKeyBindingsFilePathID());
  return 1;
}

static int Panel_GetKeyMappingCount (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetKeyMappingCount());
  return 1;
}

static int Panel_GetMinimumSize (lua_State *L) {
  int wide, tall;
  luaL_checkpanel(L, 1)->GetMinimumSize(wide, tall);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

static int Panel_GetModuleName (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->GetModuleName());
  return 1;
}

static int Panel_GetName (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->GetName());
  return 1;
}

static int Panel_GetPaintBackgroundType (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetPaintBackgroundType());
  return 1;
}

static int Panel_GetPaintSize (lua_State *L) {
  int wide, tall;
  luaL_checkpanel(L, 1)->GetPaintSize(wide, tall);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

static int Panel_GetPanelBaseClassName (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->GetPanelBaseClassName());
  return 1;
}

static int Panel_GetPanelClassName (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->GetPanelClassName());
  return 1;
}

static int Panel_GetParent (lua_State *L) {
  lua_pushpanel(L, luaL_checkpanel(L, 1)->GetParent());
  return 1;
}

static int Panel_GetPinCorner (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetPinCorner());
  return 1;
}

static int Panel_GetPinOffset (lua_State *L) {
  int dx, dy;
  luaL_checkpanel(L, 1)->GetPinOffset(dx, dy);
  lua_pushinteger(L, dx);
  lua_pushinteger(L, dy);
  return 2;
}

static int Panel_GetPos (lua_State *L) {
  int x, y;
  luaL_checkpanel(L, 1)->GetPos(x, y);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}


/*
** HL2SB: a reference table for panels that are not LPanel.
**
** Only LPanel carries m_nTableReference, so Panel_GetRefTable returned nil for every
** other control and Panel___index/__newindex had nothing to consult.  That is fatal
** for the ported LLabel and LTextEntry, which derive from vgui::Label /
** vgui::TextEntry rather than LPanel: vgui.Create does
**
**     local refTable = panel:GetRefTable()
**     if refTable then table.merge( refTable, control ) end
**
** so their control table was silently never merged -- no Init, no OnMousePressed, no
** ApplySchemeSettings.  A Derma DTextEntry therefore never called RequestFocus and
** could not be typed into.
**
** For those the table lives in the Lua registry, keyed by the panel pointer.  The
** entry is dropped when the panel is deleted by luaDroppedPanelRefTable below, which
** lPanel.cpp's own __gc path calls.
*/
static void luaPushPanelRefTable ( lua_State *L, Panel *pPanel, bool bCreate )
{
  lua_pushlightuserdata( L, pPanel );
  lua_rawget( L, LUA_REGISTRYINDEX );

  if ( lua_isnil( L, -1 ) && bCreate && pPanel != NULL )
  {
    lua_pop( L, 1 );

    lua_newtable( L );                       /* [tbl] */
    lua_pushlightuserdata( L, pPanel );
    lua_pushvalue( L, -2 );
    lua_rawset( L, LUA_REGISTRYINDEX );      /* registry[pPanel] = tbl ; [tbl] */
  }
}

static void luaDropPanelRefTable ( lua_State *L, Panel *pPanel )
{
  if ( pPanel == NULL )
    return;

  lua_pushlightuserdata( L, pPanel );
  lua_pushnil( L );
  lua_rawset( L, LUA_REGISTRYINDEX );
}

static int Panel_GetRefTable (lua_State *L) {
  LPanel *plPanel = dynamic_cast<LPanel *>(luaL_checkpanel(L, 1));
  if (plPanel) {
    if (plPanel->m_nTableReference == LUA_NOREF) {
      lua_newtable(L);
      plPanel->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_getref(L, plPanel->m_nTableReference);
  }
  else
  {
    /* HL2SB: not an LPanel -- see luaPushPanelRefTable. */
    luaPushPanelRefTable( L, luaL_checkpanel( L, 1 ), true );
  }
  return 1;
}

static int Panel_GetResizeOffset (lua_State *L) {
  int dx, dy;
  luaL_checkpanel(L, 1)->GetResizeOffset(dx, dy);
  lua_pushinteger(L, dx);
  lua_pushinteger(L, dy);
  return 2;
}

static int Panel_GetSize (lua_State *L) {
  int wide, tall;
  luaL_checkpanel(L, 1)->GetSize(wide, tall);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

static int Panel_GetTabPosition (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetTabPosition());
  return 1;
}

static int Panel_GetTall (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetTall());
  return 1;
}

static int Panel_GetVPanel (lua_State *L) {
  lua_pushpanel(L, luaL_checkpanel(L, 1)->GetVPanel());
  return 1;
}

static int Panel_GetVParent (lua_State *L) {
  lua_pushpanel(L, luaL_checkpanel(L, 1)->GetVParent());
  return 1;
}

static int Panel_GetWide (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetWide());
  return 1;
}

static int Panel_GetZPos (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetZPos());
  return 1;
}

static int Panel_HasFocus (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->HasFocus());
  return 1;
}

static int Panel_HasUserConfigSettings (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->HasUserConfigSettings());
  return 1;
}

static int Panel_InitPropertyConverters (lua_State *L) {
  luaL_checkpanel(L, 1)->InitPropertyConverters();
  return 0;
}

static int Panel_InvalidateLayout (lua_State *L) {
  luaL_checkpanel(L, 1)->InvalidateLayout(luaL_optboolean(L, 2, 0), luaL_optboolean(L, 3, 0));
  return 0;
}

static int Panel_IsAutoDeleteSet (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsAutoDeleteSet());
  return 1;
}

static int Panel_IsBeingDragged (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsBeingDragged());
  return 1;
}

static int Panel_IsBlockingDragChaining (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsBlockingDragChaining());
  return 1;
}

static int Panel_IsBottomAligned (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsBottomAligned());
  return 1;
}

static int Panel_IsBuildGroupEnabled (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsBuildGroupEnabled());
  return 1;
}

static int Panel_IsBuildModeActive (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsBuildModeActive());
  return 1;
}

static int Panel_IsBuildModeDeletable (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsBuildModeDeletable());
  return 1;
}

static int Panel_IsBuildModeEditable (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsBuildModeEditable());
  return 1;
}

static int Panel_IsChildOfModalSubTree (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsChildOfModalSubTree());
  return 1;
}

static int Panel_IsChildOfSurfaceModalPanel (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsChildOfSurfaceModalPanel());
  return 1;
}

static int Panel_IsCursorNone (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsCursorNone());
  return 1;
}

static int Panel_IsCursorOver (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsCursorOver());
  return 1;
}

static int Panel_IsDragEnabled (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsDragEnabled());
  return 1;
}

static int Panel_IsDropEnabled (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsDropEnabled());
  return 1;
}

static int Panel_IsEnabled (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsEnabled());
  return 1;
}

static int Panel_IsKeyBindingChainToParentAllowed (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsKeyBindingChainToParentAllowed());
  return 1;
}

static int Panel_IsKeyBoardInputEnabled (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsKeyBoardInputEnabled());
  return 1;
}

static int Panel_IsKeyOverridden (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsKeyOverridden((KeyCode)luaL_checkint(L, 2), luaL_checkint(L, 3)));
  return 1;
}

static int Panel_IsKeyRebound (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsKeyRebound((KeyCode)luaL_checkint(L, 2), luaL_checkint(L, 3)));
  return 1;
}

static int Panel_IsLayoutInvalid (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsLayoutInvalid());
  return 1;
}

static int Panel_IsMouseInputDisabledForThisPanel (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsMouseInputDisabledForThisPanel());
  return 1;
}

static int Panel_IsMouseInputEnabled (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsMouseInputEnabled());
  return 1;
}

static int Panel_IsOpaque (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsOpaque());
  return 1;
}

static int Panel_IsPopup (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsPopup());
  return 1;
}

static int Panel_IsProportional (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsProportional());
  return 1;
}

static int Panel_IsRightAligned (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsRightAligned());
  return 1;
}

static int Panel_IsStartDragWhenMouseExitsPanel (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsStartDragWhenMouseExitsPanel());
  return 1;
}

static int Panel_IsTriplePressAllowed (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsTriplePressAllowed());
  return 1;
}

static int Panel_IsValidKeyBindingsContext (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsValidKeyBindingsContext());
  return 1;
}

static int Panel_IsVisible (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsVisible());
  return 1;
}

static int Panel_IsWithin (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsWithin(luaL_checkint(L, 2), luaL_checkint(L, 3)));
  return 1;
}

static int Panel_IsWithinTraverse (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsWithinTraverse(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkboolean(L, 4)));
  return 1;
}

static int Panel_KB_AddBoundKey (lua_State *L) {
  luaL_checkpanel(L, 1)->KB_AddBoundKey(luaL_checkstring(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int Panel_KB_ChainToMap (lua_State *L) {
  luaL_checkpanel(L, 1)->KB_ChainToMap();
  return 0;
}

static int Panel_KeyCodeToString (lua_State *L) {
  lua_pushstring(L, luaL_checkpanel(L, 1)->KeyCodeToString((KeyCode)luaL_checkint(L, 2)));
  return 1;
}

static int Panel_LocalToScreen (lua_State *L) {
  // HL2SB: x/y were declared uninitialised and passed to the in/out method, so
  // the function always returned stack junk.  Take the input coordinates (they
  // default to 0, which yields the panel's own position).
  int x = luaL_optint(L, 2, 0);
  int y = luaL_optint(L, 3, 0);
  luaL_checkpanel(L, 1)->LocalToScreen(x, y);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}

static int Panel_MakePopup (lua_State *L) {
  luaL_checkpanel(L, 1)->MakePopup(luaL_optboolean(L, 2, 1), luaL_optboolean(L, 3, 0));
  return 0;
}

static int Panel_MakeReadyForUse (lua_State *L) {
  luaL_checkpanel(L, 1)->MakeReadyForUse();
  return 0;
}

static int Panel_MarkForDeletion (lua_State *L) {
  luaL_checkpanel(L, 1)->MarkForDeletion();
  return 0;
}

static int Panel_MoveToFront (lua_State *L) {
  luaL_checkpanel(L, 1)->MoveToFront();
  return 0;
}

static int Panel_OnCommand (lua_State *L) {
  luaL_checkpanel(L, 1)->OnCommand(luaL_checkstring(L, 2));
  return 0;
}

static int Panel_OnCursorEntered (lua_State *L) {
  luaL_checkpanel(L, 1)->OnCursorEntered();
  return 0;
}

static int Panel_OnCursorExited (lua_State *L) {
  luaL_checkpanel(L, 1)->OnCursorExited();
  return 0;
}

static int Panel_OnCursorMoved (lua_State *L) {
  luaL_checkpanel(L, 1)->OnCursorMoved(luaL_checkint(L, 2), luaL_checkint(L, 3));
  return 0;
}

static int Panel_OnDelete (lua_State *L) {
  luaL_checkpanel(L, 1)->OnDelete();
  return 0;
}

static int Panel_OnDraggablePanelPaint (lua_State *L) {
  luaL_checkpanel(L, 1)->OnDraggablePanelPaint();
  return 0;
}

static int Panel_OnKeyCodePressed (lua_State *L) {
  luaL_checkpanel(L, 1)->OnKeyCodePressed((KeyCode)luaL_checkint(L, 2));
  return 0;
}

static int Panel_OnKeyCodeTyped (lua_State *L) {
  luaL_checkpanel(L, 1)->OnKeyCodeTyped((KeyCode)luaL_checkint(L, 2));
  return 0;
}

static int Panel_OnKeyFocusTicked (lua_State *L) {
  luaL_checkpanel(L, 1)->OnKeyFocusTicked();
  return 0;
}

static int Panel_OnKillFocus (lua_State *L) {
  luaL_checkpanel(L, 1)->OnKillFocus();
  return 0;
}

static int Panel_OnMouseCaptureLost (lua_State *L) {
  luaL_checkpanel(L, 1)->OnMouseCaptureLost();
  return 0;
}

static int Panel_OnMouseDoublePressed (lua_State *L) {
  luaL_checkpanel(L, 1)->OnMouseDoublePressed((MouseCode)luaL_checkint(L, 2));
  return 0;
}

static int Panel_OnMouseFocusTicked (lua_State *L) {
  luaL_checkpanel(L, 1)->OnMouseFocusTicked();
  return 0;
}

static int Panel_OnMousePressed (lua_State *L) {
  luaL_checkpanel(L, 1)->OnMousePressed((MouseCode)luaL_checkint(L, 2));
  return 0;
}

static int Panel_OnMouseReleased (lua_State *L) {
  luaL_checkpanel(L, 1)->OnMouseReleased((MouseCode)luaL_checkint(L, 2));
  return 0;
}

static int Panel_OnMouseTriplePressed (lua_State *L) {
  luaL_checkpanel(L, 1)->OnMouseTriplePressed((MouseCode)luaL_checkint(L, 2));
  return 0;
}

static int Panel_OnMouseWheeled (lua_State *L) {
  luaL_checkpanel(L, 1)->OnMouseWheeled(luaL_checkint(L, 2));
  return 0;
}

static int Panel_OnMove (lua_State *L) {
  luaL_checkpanel(L, 1)->OnMove();
  return 0;
}

static int Panel_OnSetFocus (lua_State *L) {
  luaL_checkpanel(L, 1)->OnSetFocus();
  return 0;
}

static int Panel_OnSizeChanged (lua_State *L) {
  luaL_checkpanel(L, 1)->OnSizeChanged(luaL_checkint(L, 2), luaL_checkint(L, 3));
  return 0;
}

static int Panel_OnThink (lua_State *L) {
  luaL_checkpanel(L, 1)->OnThink();
  return 0;
}

static int Panel_OnTick (lua_State *L) {
  luaL_checkpanel(L, 1)->OnTick();
  return 0;
}

static int Panel_Paint (lua_State *L) {
  luaL_checkpanel(L, 1)->Paint();
  return 0;
}

static int Panel_PaintBackground (lua_State *L) {
  luaL_checkpanel(L, 1)->PaintBackground();
  return 0;
}

static int Panel_PaintBorder (lua_State *L) {
  luaL_checkpanel(L, 1)->PaintBorder();
  return 0;
}

static int Panel_PaintBuildOverlay (lua_State *L) {
  luaL_checkpanel(L, 1)->PaintBuildOverlay();
  return 0;
}

static int Panel_ParentLocalToScreen (lua_State *L) {
  // HL2SB: x/y were declared uninitialised and passed to the in/out method, so
  // the function always returned stack junk.  Take the input coordinates (they
  // default to 0, which yields the panel's own position).
  int x = luaL_optint(L, 2, 0);
  int y = luaL_optint(L, 3, 0);
  luaL_checkpanel(L, 1)->ParentLocalToScreen(x, y);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}

static int Panel_PerformLayout (lua_State *L) {
  luaL_checkpanel(L, 1)->PerformLayout();
  return 0;
}

static int Panel_PostChildPaint (lua_State *L) {
  luaL_checkpanel(L, 1)->PostChildPaint();
  return 0;
}

static int Panel_ReloadKeyBindings (lua_State *L) {
  luaL_checkpanel(L, 1)->ReloadKeyBindings();
  return 0;
}

static int Panel_RemoveActionSignalTarget (lua_State *L) {
  luaL_checkpanel(L, 1)->RemoveActionSignalTarget(luaL_checkpanel(L, 2));
  return 0;
}

static int Panel_RemoveAllKeyBindings (lua_State *L) {
  luaL_checkpanel(L, 1)->RemoveAllKeyBindings();
  return 0;
}

static int Panel_Repaint (lua_State *L) {
  luaL_checkpanel(L, 1)->Repaint();
  return 0;
}

static int Panel_RequestFocus (lua_State *L) {
  luaL_checkpanel(L, 1)->RequestFocus(luaL_optint(L, 2, 0));
  return 0;
}

static int Panel_RevertKeyBindingsToDefault (lua_State *L) {
  luaL_checkpanel(L, 1)->RevertKeyBindingsToDefault();
  return 0;
}

static int Panel_ScreenToLocal (lua_State *L) {
  // HL2SB: x/y were declared uninitialised and passed to the in/out method, so
  // the function always returned stack junk.  Take the input coordinates (they
  // default to 0, which yields the panel's own position).
  int x = luaL_optint(L, 2, 0);
  int y = luaL_optint(L, 3, 0);
  luaL_checkpanel(L, 1)->ScreenToLocal(x, y);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}

static int Panel_SetAllowKeyBindingChainToParent (lua_State *L) {
  luaL_checkpanel(L, 1)->SetAllowKeyBindingChainToParent(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetAlpha (lua_State *L) {
  luaL_checkpanel(L, 1)->SetAlpha(luaL_checkint(L, 2));
  return 0;
}

static int Panel_SetAutoDelete (lua_State *L) {
  luaL_checkpanel(L, 1)->SetAutoDelete(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetAutoResize (lua_State *L) {
  luaL_checkpanel(L, 1)->SetAutoResize((Panel::PinCorner_e)luaL_checkint(L, 2), (Panel::AutoResize_e)luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), luaL_checkint(L, 6), luaL_checkint(L, 7));
  return 0;
}

static int Panel_SetBgColor (lua_State *L) {
  luaL_checkpanel(L, 1)->SetBgColor(luaL_checkcolor(L, 2));
  return 0;
}

static int Panel_SetBlockDragChaining (lua_State *L) {
  luaL_checkpanel(L, 1)->SetBlockDragChaining(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetBounds (lua_State *L) {
  luaL_checkpanel(L, 1)->SetBounds(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5));
  return 0;
}

static int Panel_SetBuildModeDeletable (lua_State *L) {
  luaL_checkpanel(L, 1)->SetBuildModeDeletable(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetBuildModeEditable (lua_State *L) {
  luaL_checkpanel(L, 1)->SetBuildModeEditable(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetDragEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDragEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetDragSTartTolerance (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDragSTartTolerance(luaL_checkint(L, 2));
  return 0;
}

static int Panel_SetDropEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDropEnabled(luaL_checkboolean(L, 2), luaL_optnumber(L, 3, 0.0f));
  return 0;
}

static int Panel_SetEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetFgColor (lua_State *L) {
  luaL_checkpanel(L, 1)->SetFgColor(luaL_checkcolor(L, 2));
  return 0;
}

static int Panel_SetKeyBoardInputEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetKeyBoardInputEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetMinimumSize (lua_State *L) {
  luaL_checkpanel(L, 1)->SetMinimumSize(luaL_checkint(L, 2), luaL_checkint(L, 3));
  return 0;
}

static int Panel_SetMouseInputEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetMouseInputEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetName (lua_State *L) {
  luaL_checkpanel(L, 1)->SetName(luaL_checkstring(L, 2));
  return 0;
}

static int Panel_SetPaintBackgroundEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetPaintBackgroundEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetPaintBackgroundType (lua_State *L) {
  luaL_checkpanel(L, 1)->SetPaintBackgroundType(luaL_checkint(L, 2));
  return 0;
}

static int Panel_SetPaintBorderEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetPaintBorderEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetPaintEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetPaintEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetParent (lua_State *L) {
  luaL_checkpanel(L, 1)->SetParent(luaL_checkpanel(L, 2));
  return 0;
}

static int Panel_SetPinCorner (lua_State *L) {
  luaL_checkpanel(L, 1)->SetPinCorner((Panel::PinCorner_e)luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

/*
** HL2SB: GMod panels read self.x / self.y / self.w / self.h straight off their
** Lua table -- lua/vgui/DFrame.lua:191-193 ("if ( self.y < 0 )"), :219 (starts a
** drag with "gui.MouseX() - self.x") and :150-151 (screen-lock clamping) all do
** -- because GMod's engine refreshes those fields whenever the panel moves.
**
** Without them a click on a frame threw
**
**   lua/vgui/DFrame.lua:219: attempt to perform arithmetic on a nil value (field 'x')
**
** and dragging never started.  Kept in sync from the geometry bindings; engine
** side moves (docking) are not covered, which only affects dragging a docked
** panel -- precisely the case where it does not matter.
*/
static void Panel_SyncLuaGeometry (lua_State *L, Panel *pPanel) {
  if (pPanel == NULL)
    return;

  int x = 0, y = 0;
  pPanel->GetPos(x, y);

  int w = 0, h = 0;
  pPanel->GetSize(w, h);

  luaPushPanelRefTable(L, pPanel, true);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  lua_pushinteger(L, x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, y);
  lua_setfield(L, -2, "y");
  lua_pushinteger(L, w);
  lua_setfield(L, -2, "w");
  lua_pushinteger(L, h);
  lua_setfield(L, -2, "h");

  lua_pop(L, 1);
}

static int Panel_SetPos (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);

  pPanel->SetPos(luaL_checkint(L, 2), luaL_checkint(L, 3));
  Panel_SyncLuaGeometry(L, pPanel);
  return 0;
}

static int Panel_SetPostChildPaintEnabled (lua_State *L) {
  luaL_checkpanel(L, 1)->SetPostChildPaintEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetProportional (lua_State *L) {
  luaL_checkpanel(L, 1)->SetProportional(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetScheme (lua_State *L) {
  luaL_checkpanel(L, 1)->SetScheme(luaL_checkstring(L, 2));
  return 0;
}

static int Panel_SetSilentMode (lua_State *L) {
  luaL_checkpanel(L, 1)->SetSilentMode(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetSize (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);

  pPanel->SetSize(luaL_checkint(L, 2), luaL_checkint(L, 3));
  Panel_SyncLuaGeometry(L, pPanel);
  return 0;
}

static int Panel_SetSkipChildDuringPainting (lua_State *L) {
  luaL_checkpanel(L, 1)->SetSkipChildDuringPainting(luaL_checkpanel(L, 2));
  return 0;
}

static int Panel_SetStartDragWhenMouseExitsPanel (lua_State *L) {
  luaL_checkpanel(L, 1)->SetStartDragWhenMouseExitsPanel(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetTabPosition (lua_State *L) {
  luaL_checkpanel(L, 1)->SetTabPosition(luaL_checkint(L, 2));
  return 0;
}

static int Panel_SetTall (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  pPanel->SetTall(luaL_checkint(L, 2));
  // HL2SB: GMod's Lua reads the plain x/y/w/h fields for cheap geometry checks
  // (DFrame's drag code, DListLayout ...), so a SetTall/SetWide that skips this
  // leaves them stale.  Same sync SetPos/SetSize already do.
  Panel_SyncLuaGeometry(L, pPanel);
  return 0;
}

static int Panel_SetTriplePressAllowed (lua_State *L) {
  luaL_checkpanel(L, 1)->SetTriplePressAllowed(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetVisible (lua_State *L) {
  luaL_checkpanel(L, 1)->SetVisible(luaL_checkboolean(L, 2));
  return 0;
}

static int Panel_SetWide (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  pPanel->SetWide(luaL_checkint(L, 2));
  // HL2SB: keep x/y/w/h in step, see Panel_SetTall.
  Panel_SyncLuaGeometry(L, pPanel);
  return 0;
}

static int Panel_SetZPos (lua_State *L) {
  luaL_checkpanel(L, 1)->SetZPos(luaL_checkint(L, 2));
  return 0;
}

/*
** ===========================================================================
** HL2SB: the Panel methods GMod's own Derma controls call.
**
** lua/vgui/DFrame.lua (Garry's Mod's window class, shipped here byte for byte)
** failed on the very first line of its Init:
**
**     ConCommand 'hl2sb_playermodel_gmod' Failed:
**       lua/vgui/DFrame.lua:18: attempt to call a nil value (method 'SetFocusTopLevel')
**     lua/vgui/DFrame.lua:246: attempt to index a nil value (field 'btnClose')
**
** The second error is the cascade: Init threw before btnClose was created, so
** PerformLayout had nothing to position.  The rest of this block is the other
** half of what DFrame (and every Derma panel that has a title bar) touches.
** ===========================================================================
*/

// GMod's name; the engine call behind it is ISurface::SetTopLevelFocus( VPANEL ).
static int Panel_SetFocusTopLevel (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  bool bTopLevel = luaL_optboolean(L, 2, 1);

  if ( vgui::surface() ) {
    vgui::surface()->SetTopLevelFocus(bTopLevel ? pPanel->GetVPanel() : (VPANEL)0);
  }

  return 0;
}

// GMod's cursors are named ("arrow", "sizeall", "sizenwse", ...); the engine
// wants an HCursor.  Unknown names fall back to the normal arrow.
static HCursor Panel_CursorFromName (const char *pName) {
  if ( !pName || !pName[0] || !V_stricmp(pName, "arrow") ) return vgui::dc_arrow;
  if ( !V_stricmp(pName, "hand") )      return vgui::dc_hand;
  if ( !V_stricmp(pName, "sizeall") )   return vgui::dc_sizeall;
  if ( !V_stricmp(pName, "sizenwse") )  return vgui::dc_sizenwse;
  if ( !V_stricmp(pName, "sizenesw") )  return vgui::dc_sizenesw;
  if ( !V_stricmp(pName, "sizewe") )    return vgui::dc_sizewe;
  if ( !V_stricmp(pName, "sizens") )    return vgui::dc_sizens;
  if ( !V_stricmp(pName, "text") )      return vgui::dc_ibeam;
  if ( !V_stricmp(pName, "ibeam") )     return vgui::dc_ibeam;
  if ( !V_stricmp(pName, "hourglass") ) return vgui::dc_hourglass;
  if ( !V_stricmp(pName, "wait") )      return vgui::dc_hourglass;
  if ( !V_stricmp(pName, "crosshair") ) return vgui::dc_crosshair;
  if ( !V_stricmp(pName, "no") )        return vgui::dc_no;
  return vgui::dc_arrow;
}

static int Panel_SetCursor (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);

  if ( lua_type(L, 2) == LUA_TNUMBER ) {
    pPanel->SetCursor((HCursor)lua_tointeger(L, 2));
  } else {
    pPanel->SetCursor(Panel_CursorFromName(luaL_checkstring(L, 2)));
  }

  return 0;
}

// Panel:MouseCapture( b ) -- what DFrame uses while dragging/resizing.
static int Panel_MouseCapture (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  bool bCapture = luaL_optboolean(L, 2, 1);

  if ( vgui::input() ) {
    vgui::input()->SetMouseCapture(bCapture ? pPanel->GetVPanel() : (VPANEL)0);
  }

  return 0;
}

// GMod's Panel:Remove().  Panel::DeletePanel() is an immediate "delete this",
// which is not safe to run from a Lua callback, so defer it like the engine's
// own panels do.
static int Panel_Remove (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);

  if ( pPanel && vgui::ivgui() ) {
    vgui::ivgui()->MarkPanelForDeletion(pPanel->GetVPanel());
  }

  return 0;
}

// GMod's global IsValid( object ) reads object.IsValid, so every metatable GMod
// code passes to it needs the method (see lua/includes/extensions/gmod_isvalid.lua
// for entities and players).  Reaching this method at all means the userdata
// resolved to a live panel.
static int Panel_IsValid (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  lua_pushboolean(L, pPanel != NULL && pPanel->GetVPanel() != 0);
  return 1;
}

static void Panel_CenterParentSize (Panel *pPanel, int &nWide, int &nTall) {
  nWide = nTall = 0;

  VPANEL hParent = pPanel ? pPanel->GetVParent() : 0;
  if ( hParent && vgui::ipanel() ) {
    vgui::ipanel()->GetSize(hParent, nWide, nTall);
  }

  // Top level panels have the popup (the whole screen) as their parent, so this
  // also covers "center on screen".
  if ( (nWide <= 0 || nTall <= 0) && vgui::surface() ) {
    vgui::surface()->GetScreenSize(nWide, nTall);
  }
}

static int Panel_CenterVertical (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  float flFraction = (float)luaL_optnumber(L, 2, 0.5);
  int nOffset = luaL_optint(L, 3, 0);

  int nWide = 0, nTall = 0;
  Panel_CenterParentSize(pPanel, nWide, nTall);

  int x = 0, y = 0;
  pPanel->GetPos(x, y);
  pPanel->SetPos(x, (int)(nTall * flFraction - pPanel->GetTall() * flFraction) + nOffset);
  Panel_SyncLuaGeometry(L, pPanel);
  return 0;
}

static int Panel_CenterHorizontal (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  float flFraction = (float)luaL_optnumber(L, 2, 0.5);
  int nOffset = luaL_optint(L, 3, 0);

  int nWide = 0, nTall = 0;
  Panel_CenterParentSize(pPanel, nWide, nTall);

  int x = 0, y = 0;
  pPanel->GetPos(x, y);
  pPanel->SetPos((int)(nWide * flFraction - pPanel->GetWide() * flFraction) + nOffset, y);
  Panel_SyncLuaGeometry(L, pPanel);
  return 0;
}

static int Panel_ShouldHandleInputMessage (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->ShouldHandleInputMessage());
  return 1;
}

static int Panel_StringToKeyCode (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->StringToKeyCode(luaL_checkstring(L, 2)));
  return 1;
}

static int Panel___index (lua_State *L) {
  Panel *pPanel = lua_topanel(L, 1);
  if (pPanel == NULL) {  /* avoid extra test when d is not 0 */
    lua_Debug ar1;
    lua_getstack(L, 1, &ar1);
    lua_getinfo(L, "fl", &ar1);
    lua_Debug ar2;
    lua_getinfo(L, ">S", &ar2);
	/* HL2SB: indexing a deleted panel yields nil, the way GMod's engine behaves.
      GMod's own IsValid() (lua/includes/util.lua:314-322) is
      `local isvalid = object.IsValid` -- and a panel that has been marked for
      deletion reaches exactly that read.  Raising here turned every such check
      into an error (7221 lines in one run) and blanked the Derma UI. */
      lua_pushnil(L);
      return 1;
  }
  LPanel *plPanel = dynamic_cast<LPanel *>(pPanel);
  if (plPanel && plPanel->m_nTableReference != LUA_NOREF) {
    lua_getref(L, plPanel->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      lua_getmetatable(L, 1);
      lua_pushvalue(L, 2);
      lua_gettable(L, -2);
    }
  } else {
    /* HL2SB: non-LPanel controls keep their table in the registry. */
    luaPushPanelRefTable( L, pPanel, false );
    if ( !lua_isnil( L, -1 ) )
    {
      lua_pushvalue( L, 2 );
      lua_gettable( L, -2 );
      if ( !lua_isnil( L, -1 ) )
        return 1;
      lua_pop( L, 2 );
    }
    else
    {
      lua_pop( L, 1 );
    }

    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);

    // HL2SB: fall back to the Panel metatable.
    //
    // The branch above only runs for an LPanel.  LLabel and LTextEntry derive from
    // vgui::Label / vgui::TextEntry instead -- that is what Experiment does, and the
    // ported bindings follow -- so they always land here, where the lookup only ever
    // sees their own metatable.  Every Panel method was therefore unreachable on
    // them: GetRefTable came back nil, which broke vgui.register's
    // `table.merge( panel:GetRefTable(), helper )` and, with it, every GMod
    // vgui.Create.  SetPos, SetSize, SetVisible and MakePopup were nil for the same
    // reason -- which is why a console vgui.Label(...) looked fine (GetText is a
    // Label method) while anything touching the panel API did not.
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      luaL_getmetatable(L, "Panel");
      lua_pushvalue(L, 2);
      lua_gettable(L, -2);
    }
  }
  return 1;
}

static int Panel___newindex (lua_State *L) {
  Panel *pPanel = lua_topanel(L, 1);
  if (pPanel == NULL) {  /* avoid extra test when d is not 0 */
    lua_Debug ar1;
    lua_getstack(L, 1, &ar1);
    lua_getinfo(L, "fl", &ar1);
    lua_Debug ar2;
    lua_getinfo(L, ">S", &ar2);
    /* HL2SB: indexing a deleted panel yields nil, the way GMod's engine behaves.
      GMod's own IsValid() (lua/includes/util.lua:314-322) is
      `local isvalid = object.IsValid` -- and a panel that has been marked for
      deletion reaches exactly that read.  Raising here turned every such check
      into an error (7221 lines in one run) and blanked the Derma UI. */
      lua_pushnil(L);
      return 1;
  }
  LPanel *plPanel = dynamic_cast<LPanel *>(pPanel);
  if (plPanel) {
    if (plPanel->m_nTableReference == LUA_NOREF) {
      lua_newtable(L);
      plPanel->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_getref(L, plPanel->m_nTableReference);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, luaL_checkstring(L, 2));
	lua_pop(L, 1);
    return 0;
  } else {
    /*
    ** HL2SB: non-LPanel controls -- LLabel and LTextEntry -- used to raise
    ** "attempt to index a non-scripted panel" here, so nothing could be assigned to
    ** them at all.  Garry's Mod code assigns fields to controls constantly
    ** (label.UpdateColours = function ... , entry.OnTextChanged = ...), so they get the
    ** registry-backed table luaPushPanelRefTable hands out instead.
    */
    luaPushPanelRefTable( L, pPanel, true );
    lua_pushvalue( L, 3 );
    lua_setfield( L, -2, luaL_checkstring( L, 2 ) );
    lua_pop( L, 1 );
    return 0;
  }
}

static int Panel___gc (lua_State *L) {
  LPanel *plPanel = dynamic_cast<LPanel *>(lua_topanel(L, 1));
  if (plPanel) {
    --plPanel->m_nRefCount;
	if (plPanel->m_nRefCount <= 0) {
      delete plPanel;
    }
  }
  return 0;
}

static int Panel___eq (lua_State *L) {
  lua_pushboolean(L, lua_topanel(L, 1) == lua_topanel(L, 2));
  return 1;
}

static int Panel___tostring (lua_State *L) {
  Panel *pPanel = lua_topanel(L, 1);
  if (pPanel == NULL)
    lua_pushstring(L, "INVALID_PANEL");
  else {
    const char *pName = pPanel->GetName();
    if (Q_strcmp(pName, "") == 0)
      pName = "(no name)";
    lua_pushfstring(L, "Panel: \"%s\"", pName);
  }
  return 1;
}


//=============================================================================
// HL2SB: GMod docking.
//
// GMod's Panel:Dock / DockPadding / DockMargin over the vgui2 implementation
// added in vgui2/vgui_controls/Panel.cpp.  The DOCK enum values are GMod's and
// are exposed WITHOUT the DOCK_ prefix (see the Lua side), so these just pass
// the number straight through.
//=============================================================================
static int Panel_Dock (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDock(luaL_checkint(L, 2));
  return 0;
}

static int Panel_GetDock (lua_State *L) {
  lua_pushinteger(L, luaL_checkpanel(L, 1)->GetDock());
  return 1;
}

static int Panel_DockPadding (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDockPadding(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5));
  return 0;
}

static int Panel_GetDockPadding (lua_State *L) {
  int l = 0, t = 0, r = 0, b = 0;
  luaL_checkpanel(L, 1)->GetDockPadding(l, t, r, b);
  lua_pushinteger(L, l); lua_pushinteger(L, t); lua_pushinteger(L, r); lua_pushinteger(L, b);
  return 4;
}

static int Panel_DockMargin (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDockMargin(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5));
  return 0;
}

static int Panel_GetDockMargin (lua_State *L) {
  int l = 0, t = 0, r = 0, b = 0;
  luaL_checkpanel(L, 1)->GetDockMargin(l, t, r, b);
  lua_pushinteger(L, l); lua_pushinteger(L, t); lua_pushinteger(L, r); lua_pushinteger(L, b);
  return 4;
}

//=============================================================================
// HL2SB: the layout half of GMod's Panel API, which GMod's own lua/vgui/*.lua
// controls call on every layout pass and which this fork never bound:
//
//   Panel:SizeToChildren( bWidth, bHeight )   dlistlayout.lua:24,
//       dsizetocontents.lua:17, dproperties.lua:166/167/217,
//       dcategorycollapse.lua:188/255/256, dscrollpanel.lua:71,
//       diconlayout.lua:122, DPanPanel.lua:141, propselect.lua:165
//   Panel:ChildrenSize() / Panel:GetChildrenSize()   dtilelayout.lua:169,
//       gmod_compatibility/sh_init.lua
//   Panel:InvalidateParentLayout()
//
// Without these the Derma control throws on the first line of its
// PerformLayout() and keeps whatever height it started with -- which is exactly
// the "Dock(TOP) is not sized to the correct height" report.
//
// SetDock / SetDockMargin / SetDockPadding are registered as well: they are the
// engine names GMod's lua/includes/modules/gmod_compatibility/sh_init.lua
// aliases to Dock / DockMargin / DockPadding, and that file is inert here today
// (GMOD_COMPATIBILITY = false) but would overwrite the working aliases with nil
// the day it gets switched on.
//=============================================================================
static int Panel_SizeToChildren (lua_State *L) {
  luaL_checkpanel(L, 1)->SizeToChildren(lua_toboolean(L, 2) != 0, lua_toboolean(L, 3) != 0);
  return 0;
}

static int Panel_GetChildrenSize (lua_State *L) {
  int w = 0, h = 0;
  luaL_checkpanel(L, 1)->GetChildrenSize(w, h);
  lua_pushinteger(L, w); lua_pushinteger(L, h);
  return 2;
}

static int Panel_InvalidateParentLayout (lua_State *L) {
  luaL_checkpanel(L, 1)->InvalidateParentLayout(luaL_optboolean(L, 2, 0), luaL_optboolean(L, 3, 0));
  return 0;
}

static int Panel_SetDock (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDock(luaL_checkint(L, 2));
  return 0;
}

static int Panel_SetDockMargin (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDockMargin(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5));
  return 0;
}

static int Panel_SetDockPadding (lua_State *L) {
  luaL_checkpanel(L, 1)->SetDockPadding(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5));
  return 0;
}


static const luaL_Reg Panelmeta[] = {
  {"Dock", Panel_Dock},
  {"SetDock", Panel_SetDock},
  {"GetDock", Panel_GetDock},
  {"DockPadding", Panel_DockPadding},
  {"SetDockPadding", Panel_SetDockPadding},
  {"GetDockPadding", Panel_GetDockPadding},
  {"DockMargin", Panel_DockMargin},
  {"SetDockMargin", Panel_SetDockMargin},
  {"GetDockMargin", Panel_GetDockMargin},
  {"SizeToChildren", Panel_SizeToChildren},
  {"GetChildrenSize", Panel_GetChildrenSize},
  {"ChildrenSize", Panel_GetChildrenSize},
  {"InvalidateParentLayout", Panel_InvalidateParentLayout},
  {"AddKeyBinding", Panel_AddKeyBinding},
  {"AddActionSignalTarget", Panel_AddActionSignalTarget},
  {"CanStartDragging", Panel_CanStartDragging},
  {"ChainToAnimationMap", Panel_ChainToAnimationMap},
  {"ChainToMap", Panel_ChainToMap},
  {"DeletePanel", Panel_DeletePanel},
  {"DisableMouseInputForThisPanel", Panel_DisableMouseInputForThisPanel},
  {"DrawBox", Panel_DrawBox},
  {"DrawBoxFade", Panel_DrawBoxFade},
  {"DrawHollowBox", Panel_DrawHollowBox},
  {"DrawTexturedBox", Panel_DrawTexturedBox},
  {"EditKeyBindings", Panel_EditKeyBindings},
  {"FillRectSkippingPanel", Panel_FillRectSkippingPanel},
  {"FindChildByName", Panel_FindChildByName},
  {"FindChildIndexByName", Panel_FindChildIndexByName},
  {"FindSiblingByName", Panel_FindSiblingByName},
  {"GetAlpha", Panel_GetAlpha},
  {"GetBgColor", Panel_GetBgColor},
  {"GetBounds", Panel_GetBounds},
  {"GetChild", Panel_GetChild},
  {"GetChildCount", Panel_GetChildCount},
  {"GetChildren", Panel_GetChildren},
  {"GetClassName", Panel_GetClassName},
  {"GetClipRect", Panel_GetClipRect},
  {"GetCornerTextureSize", Panel_GetCornerTextureSize},
  {"GetDescription", Panel_GetDescription},
  {"GetDragFrameColor", Panel_GetDragFrameColor},
  {"GetDragPanel", Panel_GetDragPanel},
  {"GetDragStartTolerance", Panel_GetDragStartTolerance},
  {"GetDropFrameColor", Panel_GetDropFrameColor},
  {"GetFgColor", Panel_GetFgColor},
  {"GetInset", Panel_GetInset},
  {"GetKeyBindingsFile", Panel_GetKeyBindingsFile},
  {"GetKeyBindingsFilePathID", Panel_GetKeyBindingsFilePathID},
  {"GetKeyMappingCount", Panel_GetKeyMappingCount},
  {"GetMinimumSize", Panel_GetMinimumSize},
  {"GetModuleName", Panel_GetModuleName},
  {"GetName", Panel_GetName},
  {"GetPaintBackgroundType", Panel_GetPaintBackgroundType},
  {"GetPaintSize", Panel_GetPaintSize},
  {"GetPanelBaseClassName", Panel_GetPanelBaseClassName},
  {"GetPanelClassName", Panel_GetPanelClassName},
  {"GetParent", Panel_GetParent},
  {"GetPinCorner", Panel_GetPinCorner},
  {"GetPinOffset", Panel_GetPinOffset},
  {"GetPos", Panel_GetPos},
  {"GetRefTable", Panel_GetRefTable},
  /* HL2SB: GMod spells this GetTable().  lua/includes/extensions/client/panel/
  ** scriptedpanels.lua:35 does
  **
  **     table.Merge( panel:GetTable(), metatable )
  **
  ** and every GMod panel file (dpanel.lua, dlabel.lua, ...) calls panel:GetTable()
  ** too, so with only the Experiment name registered the call resolved to nil:
  **
  **     Hook 'hl2sb_notification' (OnUndo) Failed:
  **       scriptedpanels.lua:35: attempt to call a nil value (method 'GetTable')
  **
  ** which kept the GMod undo notification from ever being built even after the
  ** undo itself worked.  Same function, both names. */
  {"GetTable", Panel_GetRefTable},
  {"GetResizeOffset", Panel_GetResizeOffset},
  {"GetSize", Panel_GetSize},
  {"GetTabPosition", Panel_GetTabPosition},
  {"GetTall", Panel_GetTall},
  {"GetVPanel", Panel_GetVPanel},
  {"GetVParent", Panel_GetVParent},
  {"GetWide", Panel_GetWide},
  {"GetZPos", Panel_GetZPos},
  {"HasFocus", Panel_HasFocus},
  {"HasUserConfigSettings", Panel_HasUserConfigSettings},
  {"InitPropertyConverters", Panel_InitPropertyConverters},
  {"InvalidateLayout", Panel_InvalidateLayout},
  {"IsAutoDeleteSet", Panel_IsAutoDeleteSet},
  {"IsBeingDragged", Panel_IsBeingDragged},
  {"IsBlockingDragChaining", Panel_IsBlockingDragChaining},
  {"IsBottomAligned", Panel_IsBottomAligned},
  {"IsBuildGroupEnabled", Panel_IsBuildGroupEnabled},
  {"IsBuildModeActive", Panel_IsBuildModeActive},
  {"IsBuildModeDeletable", Panel_IsBuildModeDeletable},
  {"IsBuildModeEditable", Panel_IsBuildModeEditable},
  {"IsChildOfModalSubTree", Panel_IsChildOfModalSubTree},
  {"IsChildOfSurfaceModalPanel", Panel_IsChildOfSurfaceModalPanel},
  {"IsCursorNone", Panel_IsCursorNone},
  {"IsCursorOver", Panel_IsCursorOver},
  {"IsDragEnabled", Panel_IsDragEnabled},
  {"IsDropEnabled", Panel_IsDropEnabled},
  {"IsEnabled", Panel_IsEnabled},
  {"IsKeyBindingChainToParentAllowed", Panel_IsKeyBindingChainToParentAllowed},
  {"IsKeyBoardInputEnabled", Panel_IsKeyBoardInputEnabled},
  {"IsKeyOverridden", Panel_IsKeyOverridden},
  {"IsKeyRebound", Panel_IsKeyRebound},
  {"IsLayoutInvalid", Panel_IsLayoutInvalid},
  {"IsMouseInputDisabledForThisPanel", Panel_IsMouseInputDisabledForThisPanel},
  {"IsMouseInputEnabled", Panel_IsMouseInputEnabled},
  {"IsOpaque", Panel_IsOpaque},
  {"IsPopup", Panel_IsPopup},
  {"IsProportional", Panel_IsProportional},
  {"IsRightAligned", Panel_IsRightAligned},
  {"IsStartDragWhenMouseExitsPanel", Panel_IsStartDragWhenMouseExitsPanel},
  {"IsTriplePressAllowed", Panel_IsTriplePressAllowed},
  {"IsValidKeyBindingsContext", Panel_IsValidKeyBindingsContext},
  {"IsVisible", Panel_IsVisible},
  {"IsWithin", Panel_IsWithin},
  {"IsWithinTraverse", Panel_IsWithinTraverse},
  {"KB_AddBoundKey", Panel_KB_AddBoundKey},
  {"KB_ChainToMap", Panel_KB_ChainToMap},
  {"KeyCodeToString", Panel_KeyCodeToString},
  {"LocalToScreen", Panel_LocalToScreen},
  {"MakePopup", Panel_MakePopup},
  {"MakeReadyForUse", Panel_MakeReadyForUse},
  {"MarkForDeletion", Panel_MarkForDeletion},
  {"MoveToFront", Panel_MoveToFront},
  {"OnCommand", Panel_OnCommand},
  {"OnCursorEntered", Panel_OnCursorEntered},
  {"OnCursorExited", Panel_OnCursorExited},
  {"OnCursorMoved", Panel_OnCursorMoved},
  {"OnDelete", Panel_OnDelete},
  {"OnDraggablePanelPaint", Panel_OnDraggablePanelPaint},
  {"OnKeyCodePressed", Panel_OnKeyCodePressed},
  {"OnKeyCodeTyped", Panel_OnKeyCodeTyped},
  {"OnKeyFocusTicked", Panel_OnKeyFocusTicked},
  {"OnKillFocus", Panel_OnKillFocus},
  {"OnMouseCaptureLost", Panel_OnMouseCaptureLost},
  {"OnMouseDoublePressed", Panel_OnMouseDoublePressed},
  {"OnMouseFocusTicked", Panel_OnMouseFocusTicked},
  {"OnMousePressed", Panel_OnMousePressed},
  {"OnMouseReleased", Panel_OnMouseReleased},
  {"OnMouseTriplePressed", Panel_OnMouseTriplePressed},
  {"OnMouseWheeled", Panel_OnMouseWheeled},
  {"OnMove", Panel_OnMove},
  {"OnSetFocus", Panel_OnSetFocus},
  {"OnSizeChanged", Panel_OnSizeChanged},
  {"OnThink", Panel_OnThink},
  {"OnTick", Panel_OnTick},
  {"Paint", Panel_Paint},
  {"PaintBackground", Panel_PaintBackground},
  {"PaintBorder", Panel_PaintBorder},
  {"PaintBuildOverlay", Panel_PaintBuildOverlay},
  {"ParentLocalToScreen", Panel_ParentLocalToScreen},
  {"PerformLayout", Panel_PerformLayout},
  {"PostChildPaint", Panel_PostChildPaint},
  {"ReloadKeyBindings", Panel_ReloadKeyBindings},
  {"RemoveActionSignalTarget", Panel_RemoveActionSignalTarget},
  {"RemoveAllKeyBindings", Panel_RemoveAllKeyBindings},
  {"Repaint", Panel_Repaint},
  {"RequestFocus", Panel_RequestFocus},
  {"RevertKeyBindingsToDefault", Panel_RevertKeyBindingsToDefault},
  {"ScreenToLocal", Panel_ScreenToLocal},
  {"SetAllowKeyBindingChainToParent", Panel_SetAllowKeyBindingChainToParent},
  {"SetAlpha", Panel_SetAlpha},
  {"SetAutoDelete", Panel_SetAutoDelete},
  {"SetAutoResize", Panel_SetAutoResize},
  {"SetBgColor", Panel_SetBgColor},
  {"SetBlockDragChaining", Panel_SetBlockDragChaining},
  {"SetBounds", Panel_SetBounds},
  {"SetBuildModeDeletable", Panel_SetBuildModeDeletable},
  {"SetBuildModeEditable", Panel_SetBuildModeEditable},
  {"SetDragEnabled", Panel_SetDragEnabled},
  {"SetDragSTartTolerance", Panel_SetDragSTartTolerance},
  {"SetDropEnabled", Panel_SetDropEnabled},
  {"SetEnabled", Panel_SetEnabled},
  {"SetFgColor", Panel_SetFgColor},
  {"SetKeyBoardInputEnabled", Panel_SetKeyBoardInputEnabled},
  {"SetMinimumSize", Panel_SetMinimumSize},
  {"SetMouseInputEnabled", Panel_SetMouseInputEnabled},
  {"SetName", Panel_SetName},
  {"SetPaintBackgroundEnabled", Panel_SetPaintBackgroundEnabled},
  {"SetPaintBackgroundType", Panel_SetPaintBackgroundType},
  {"SetPaintBorderEnabled", Panel_SetPaintBorderEnabled},
  {"SetPaintEnabled", Panel_SetPaintEnabled},
  {"SetParent", Panel_SetParent},
  {"SetPinCorner", Panel_SetPinCorner},
  {"SetPos", Panel_SetPos},
  {"SetPostChildPaintEnabled", Panel_SetPostChildPaintEnabled},
  {"SetProportional", Panel_SetProportional},
  {"SetScheme", Panel_SetScheme},
  {"SetSilentMode", Panel_SetSilentMode},
  {"SetSize", Panel_SetSize},
  {"SetSkipChildDuringPainting", Panel_SetSkipChildDuringPainting},
  {"SetStartDragWhenMouseExitsPanel", Panel_SetStartDragWhenMouseExitsPanel},
  {"SetTabPosition", Panel_SetTabPosition},
  {"SetTall", Panel_SetTall},
  {"SetTriplePressAllowed", Panel_SetTriplePressAllowed},
  {"SetVisible", Panel_SetVisible},
  {"SetWide", Panel_SetWide},
  {"SetZPos", Panel_SetZPos},
  // HL2SB: GMod's names for the rest of what Derma controls call.
  {"SetFocusTopLevel", Panel_SetFocusTopLevel},
  {"SetCursor", Panel_SetCursor},
  {"MouseCapture", Panel_MouseCapture},
  {"Remove", Panel_Remove},
  {"IsValid", Panel_IsValid},
  {"CenterVertical", Panel_CenterVertical},
  {"CenterHorizontal", Panel_CenterHorizontal},
  {"ShouldHandleInputMessage", Panel_ShouldHandleInputMessage},
  {"StringToKeyCode", Panel_StringToKeyCode},
  {"__index", Panel___index},
  {"__newindex", Panel___newindex},
  {"__gc", Panel___gc},
  {"__eq", Panel___eq},
  {"__tostring", Panel___tostring},
  {NULL, NULL}
};


static int luasrc_VGui_GetGameUIPanel (lua_State *L) {
  lua_pushpanel(L, VGui_GetGameUIPanel());
  return 1;
}

static int luasrc_VGui_GetClientLuaRootPanel (lua_State *L) {
  lua_pushpanel(L, VGui_GetClientLuaRootPanel());
  return 1;
}


static const luaL_Reg Panel_funcs[] = {
  {"VGui_GetGameUIPanel", luasrc_VGui_GetGameUIPanel},
  {"VGui_GetClientLuaRootPanel", luasrc_VGui_GetClientLuaRootPanel},
  {NULL, NULL}
};


/*
** Open Panel object
*/
LUALIB_API int luaopen_Panel (lua_State *L) {
  luaL_newmetatable(L, "Panel");
  luaL_register(L, NULL, Panelmeta);
  lua_pushstring(L, "panel");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "panel" */
  luaL_register(L, "_G", Panel_funcs);
  lua_pop(L, 1);
  // Andrew; Don't be mislead, INVALID_PANEL is not NULL internally, but we
  // need a name other than NULL, because NULL has already been assigned as an
  // entity.
  lua_pushpanel(L, (Panel *)0);
  lua_setglobal(L, "INVALID_PANEL");  /* set global INVALID_PANEL */
  return 1;
}

