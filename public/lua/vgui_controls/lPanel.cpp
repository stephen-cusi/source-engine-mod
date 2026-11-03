//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#define lpanel_cpp

#include "cbase.h"
#include "icvar.h"
#include <vgui_controls/Panel.h>
#include "c_vguiscreen.h"
#include "iclientmode.h"
#include "ienginevgui.h"
#include <vgui/IVGui.h>
#include <vgui/IInput.h>
#include <vgui/ISurface.h>
#include <vgui/IPanel.h>
#include <Color.h>
#include <vgui/Cursor.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/TextImage.h>
#include <vgui_controls/TextEntry.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/FocusNavGroup.h>
#include "panelmetaclassmgr.h"
#include <vgui_controls/PHandle.h>
#include "luamanager.h"
#include "vgui_int.h"
#include "lPanel.h"
#include <scripted_controls/lPanel.h>
#include <scripted_controls/lLabel.h>
#include <scripted_controls/lTextEntry.h>
#include <scripted_controls/lButton.h>
#include <scripted_controls/lCheckButton.h>
#include <scripted_controls/lFrame.h>
#include <scripted_controls/lModelPanel.h>
#include <scripted_controls/lPropertyPage.h>
#include <scripted_controls/lPropertyDialog.h>
#include "lColor.h"
#include "vgui/LVGUI.h"
#include "vgui_controls/HTML.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "vgui/ILocalize.h"
#include "tier3/tier3.h"
#include "filesystem.h"
#include "iviewrender.h"
#include "view_shared.h"
#include <string>
#include <vector>

extern ConVar hl2sb_dev_msgs;

using namespace vgui;

extern FocusNavGroup *HL2SB_LuaPanelFocusGroup(Panel *panel);
extern void HL2SB_SetLuaLabelShadow( Label *label, int offset, Color color );

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
  if (LCheckButton *p = dynamic_cast<LCheckButton *>(pPanel)) { lua_pushcheckbutton(L, p); return; }
  if (LButton *p = dynamic_cast<LButton *>(pPanel)) { lua_pushbutton(L, p); return; }
  if (LLabel *p = dynamic_cast<LLabel *>(pPanel)) { lua_pushlabel(L, p); return; }
  if (LTextEntry *p = dynamic_cast<LTextEntry *>(pPanel)) { lua_pushtextentry(L, p); return; }
  if (LFrame *p = dynamic_cast<LFrame *>(pPanel)) { lua_pushframe(L, p); return; }
  if (LModelPanel *p = dynamic_cast<LModelPanel *>(pPanel)) { lua_pushmodelpanel(L, p); return; }
  if (LPropertyPage *p = dynamic_cast<LPropertyPage *>(pPanel)) { lua_pushpropertypage(L, p); return; }
  if (LPropertyDialog *p = dynamic_cast<LPropertyDialog *>(pPanel)) { lua_pushpropertydialog(L, p); return; }
  LPanel *plPanel = dynamic_cast<LPanel *>(pPanel);
  if (plPanel)
    ++plPanel->m_nRefCount;
  PHandle *phPanel = (PHandle *)lua_newuserdata(L, sizeof(PHandle));
  phPanel->Set(pPanel);
  luaL_getmetatable(L, "Panel");
  lua_setmetatable(L, -2);
}


LUA_API void lua_pushpanel (lua_State *L, VPANEL panel) {
  PHandle handle;
  handle.Set(ivgui()->PanelToHandle(panel));
  lua_pushpanel(L, handle.Get());
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
  Panel *panel = luaL_checkpanel(L, 1);
  lua_pushstring(L, dynamic_cast<EditablePanel *>(panel) ? "EditablePanel" : panel->GetClassName());
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
  Panel *parent = luaL_checkpanel(L, 1)->GetParent();
  if (!parent) return 0;
  lua_pushpanel(L, parent);
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

template<class T> static bool Panel_PushOwnedTable(lua_State *L, Panel *pPanel) {
  T *p = dynamic_cast<T *>(pPanel);
  if (!p) return false;
  if (p->m_lua_State != L) { lua_pushnil(L); return true; }
  if (!lua_isrefvalid(L, p->m_nTableReference)) {
    lua_newtable(L);
    p->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  lua_getref(L, p->m_nTableReference);
  return true;
}

static void Panel_PushTable(lua_State *L, Panel *p) {
  // GetTable, script lookup and native callbacks must share one instance table.
  if (!Panel_PushOwnedTable<LPanel>(L, p) &&
      !Panel_PushOwnedTable<LLabel>(L, p) &&
      !Panel_PushOwnedTable<LTextEntry>(L, p) &&
      !Panel_PushOwnedTable<LButton>(L, p) &&
      !Panel_PushOwnedTable<LCheckButton>(L, p) &&
      !Panel_PushOwnedTable<LFrame>(L, p) &&
      !Panel_PushOwnedTable<LModelPanel>(L, p) &&
      !Panel_PushOwnedTable<LPropertyPage>(L, p) &&
      !Panel_PushOwnedTable<LPropertyDialog>(L, p))
    luaPushPanelRefTable(L, p, true);
}

static int Panel_GetRefTable (lua_State *L) {
  Panel_PushTable(L, luaL_checkpanel(L, 1));
  return 1;
}

void HL2SB_LuaPanelSetHovered(lua_State *state, Panel *panel, bool hovered)
{
  if (!state || (state != ::L && state != LGameUI)) return;
  lua_checkstack(state, 12);
  Panel_PushTable(state, panel);
  bool changed = false;
  if (lua_istable(state, -1)) {
    lua_getfield(state, -1, "Hovered");
    changed = (lua_toboolean(state, -1) != 0) != hovered;
    lua_pop(state, 1);
    lua_pushboolean(state, hovered);
    lua_setfield(state, -2, "Hovered");
  }
  lua_pop(state, 1);
  if (changed) {
    // GMod raises this global independently of the control's cursor methods.
    lua_getglobal(state, hovered ? "ChangeTooltip" : "EndTooltip");
    if (lua_isfunction(state, -1)) {
      lua_pushpanel(state, panel);
      luasrc_pcall(state, 1, 0, 0);
    } else lua_pop(state, 1);
  }
}

bool HL2SB_LuaPanelBeginPaint(lua_State *state, Panel *panel)
{
  IMatSystemSurface *s = dynamic_cast<IMatSystemSurface *>(surface());
  bool disabled = true;
  if (!s) return disabled;
  int left, top, right, bottom;
  s->GetClippingRect(left, top, right, bottom, disabled);
  if (state && (state == ::L || state == LGameUI)) {
    lua_checkstack(state, 8);
    Panel_PushTable(state, panel);
    if (lua_istable(state, -1)) {
      lua_getfield(state, -1, "__hl2sb_NoClipping");
      if (lua_toboolean(state, -1)) s->DisableClipping(true);
      lua_pop(state, 1);
    }
    lua_pop(state, 1);
  }
  return disabled;
}

void HL2SB_LuaPanelEndPaint(bool previous)
{
  if (IMatSystemSurface *s = dynamic_cast<IMatSystemSurface *>(surface())) s->DisableClipping(previous);
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

extern "C" void VGUI_SetScreenshotPanelVisibleCallback( bool (*fn)(Panel *) );
static std::vector<PHandle> s_screenshotHiddenPanels;

static bool Panel_ShouldRenderInScreenshot( Panel *panel ) {
  if ( !engine->IsTakingScreenshot() ) return true;
  for ( size_t i = 0; i < s_screenshotHiddenPanels.size(); ++i )
    if ( s_screenshotHiddenPanels[i].Get() == panel ) return false;
  return true;
}

static int Panel_SetRenderInScreenshots( lua_State *L ) {
  Panel *panel = luaL_checkpanel( L, 1 );
  const bool render = lua_toboolean( L, 2 ) != 0;
  for ( size_t i = 0; i < s_screenshotHiddenPanels.size(); ) {
    Panel *stored = s_screenshotHiddenPanels[i].Get();
    if ( !stored || stored == panel ) s_screenshotHiddenPanels.erase( s_screenshotHiddenPanels.begin() + i );
    else ++i;
  }
  if ( !render ) {
    PHandle handle;
    handle.Set( panel );
    s_screenshotHiddenPanels.push_back( handle );
  }
  return 0;
}

static bool Panel_IsWorldClickerValue(lua_State *state, Panel *panel) {
  if (!state || !panel) return false;
  Panel_PushTable(state, panel);
  bool result = false;
  if (lua_istable(state, -1)) {
    lua_pushliteral(state, "m_bWorldClicker");
    lua_rawget(state, -2);
    result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
  return result;
}

bool HL2SB_IsScreenClickPanel(Panel *panel) {
  return panel && (panel == VGui_GetClientLuaRootPanel() ||
    panel->GetVPanel() == VGui_GetClientDLLRootPanel() ||
    Panel_IsWorldClickerValue(::L, panel));
}

static int Panel_IsWorldClicker (lua_State *L) {
  lua_pushboolean(L, Panel_IsWorldClickerValue(L, luaL_checkpanel(L, 1)));
  return 1;
}

static int Panel_SetWorldClicker (lua_State *L) {
  Panel_PushTable(L, luaL_checkpanel(L, 1));
  if (lua_istable(L, -1)) {
    lua_pushboolean(L, lua_toboolean(L, 2));
    lua_setfield(L, -2, "m_bWorldClicker");
  }
  lua_pop(L, 1);
  return 0;
}

// Use the player view for screen input, independently of offscreen rendering.
bool HL2SB_GetScreenClickDirection(Vector &direction, int x, int y, const CViewSetup *setup) {
  const CViewSetup *camera = setup ? setup : (view ? view->GetPlayerViewSetup() : NULL);
  if (!camera || camera->width <= 0 || camera->height <= 0 || camera->fov <= 0.0f) return false;
  Vector forward, right, up;
  AngleVectors(camera->angles, &forward, &right, &up);
  const float halfWidth = camera->width * 0.5f;
  const float halfHeight = camera->height * 0.5f;
  direction = forward * (halfWidth / tanf(DEG2RAD(camera->fov) * 0.5f)) +
    right * (x - camera->x - halfWidth) + up * (camera->y + halfHeight - y);
  return VectorNormalize(direction) > 0.0f;
}

// Use the actual input target; a button or captured control blocks world aiming.
bool HL2SB_GetWorldClickDirection(Vector &direction, const CViewSetup *setup) {
  if (!::L || !input() || !surface() || !ipanel() || !surface()->IsCursorVisible()) return false;
  if (enginevgui && enginevgui->IsGameUIVisible()) return false;
  VPANEL target = input()->GetMouseCapture();
  if (!target) target = input()->GetMouseOver();
  Panel *panel = target ? ipanel()->GetPanel(target, GetControlsModuleName()) : NULL;
  if (!panel || !panel->IsVisible() || !Panel_IsWorldClickerValue(::L, panel)) return false;
  int x, y;
  input()->GetCursorPos(x, y);
  return HL2SB_GetScreenClickDirection(direction, x, y, setup);
}

static int Panel_IsWithin (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsWithin(luaL_checkint(L, 2), luaL_checkint(L, 3)));
  return 1;
}

static int Panel_DrawFilledRect (lua_State *L) {
  Panel *panel = luaL_checkpanel(L, 1);
  surface()->DrawFilledRect(0, 0, panel->GetWide(), panel->GetTall());
  return 0;
}

static int Panel_DrawOutlinedRect (lua_State *L) {
  Panel *panel = luaL_checkpanel(L, 1);
  surface()->DrawOutlinedRect(0, 0, panel->GetWide(), panel->GetTall());
  return 0;
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

static int Panel_IsMarkedForDeletion (lua_State *L) {
  lua_pushboolean(L, luaL_checkpanel(L, 1)->IsMarkedForDeletion());
  return 1;
}

static int Panel_MarkForDeletion (lua_State *L) {
  luaL_checkpanel(L, 1)->MarkForDeletion();
  return 0;
}

static int Panel_MoveToFront (lua_State *L) {
  luaL_checkpanel(L, 1)->MoveToFront();
  return 0;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Panel:MoveToBack().
//
// GMod's Lua API has both MoveToFront and MoveToBack; this fork only ever bound
// MoveToFront (the entry just above).  vgui_controls::Panel has no MoveToBack()
// of its own -- but the interface underneath it does, and that is exactly what
// vgui2's own Frame.cpp:990 calls:
//
//     public/vgui/IPanel.h:65   virtual void MoveToBack(VPANEL vguiPanel) = 0;
//
// Needed by the GMod spawnmenu: gamemodes/sandbox/gamemode/spawnmenu/
// creationmenu/content/contenttypes/custom.lua:223 does `node:MoveToBack()` in
// the spawnlist right-click handler, and lua/includes/modules/menubar.lua:22
// does the same while re-parenting the menubar.
//-----------------------------------------------------------------------------
static int Panel_MoveToBack (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  if (pPanel != NULL)
    vgui::ipanel()->MoveToBack(pPanel->GetVPanel());
  return 0;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Panel:FocusNext().
//
// lua/vgui/dtextentry.lua:78 calls it when Enter is pressed -- the GMod text
// entry hands the keyboard on to the next control.  The vgui2 primitive is
// Panel::RequestFocusNext() (public/vgui_controls/Panel.h:322), which is what
// vgui2's own panels call to do the same thing.
//-----------------------------------------------------------------------------
static int Panel_FocusNext (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  if (pPanel != NULL)
    pPanel->RequestFocusNext();
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

/* HL2SB: defined further down, next to the SetPos/SetSize bindings. */
static void Panel_SyncLuaGeometry (lua_State *L, Panel *pPanel);

static int Panel_SetBounds (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  pPanel->SetBounds(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5));
  /* HL2SB: SetPos/SetSize feed the panel's Lua geometry fields (x/y/w/h)
     through Panel_SyncLuaGeometry; SetBounds is SetPos+SetSize in one call
     and must feed the same fields.  HL2SB_MenuLayout's walk reads those
     fields back and re-applies whatever it reads, so a panel positioned
     only through SetBounds answered (0,0,0,0) there and was slammed to the
     frame origin (the collapsed main-menu dialogs, 2026-10-07). */
  Panel_SyncLuaGeometry( L, pPanel );
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
  luaL_checkpanel(L, 1)->SetEnabled(lua_toboolean(L, 2) != 0);
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
  Panel *root = L == LGameUI ? VGui_GetGameUIPanel() : VGui_GetClientLuaRootPanel();
  luaL_checkpanel(L, 1)->SetParent(luaL_optpanel(L, 2, root));
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
  Panel_GetRefTable(L);
  if (lua_istable(L, -1)) {
    const char *fields[] = { "x", "y", "w", "h" };
    for (int i = 0; i < 4; ++i) { lua_pushnil(L); lua_setfield(L, -2, fields[i]); }
  }
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

  int wide = (int)lua_tonumber(L, 2);
  int tall = (int)lua_tonumber(L, 3);
  pPanel->SetSize(MIN(wide, 32767), MIN(tall, 32767));
  Panel_SyncLuaGeometry(L, pPanel);
  return 0;
}

// Manual painting keeps the normal traversal from drawing this subtree twice.
extern void HL2SB_PanelSetPaintedManually(Panel *panel, bool manual);
extern void HL2SB_PanelPaintManual(Panel *panel);

static int Panel_SetPaintedManually (lua_State *L) {
  HL2SB_PanelSetPaintedManually(luaL_checkpanel(L, 1), lua_toboolean(L, 2) != 0);
  return 0;
}

static int Panel_PaintManual (lua_State *L) {
  Panel *panel = luaL_checkpanel(L, 1);
  IMatSystemSurface *s = dynamic_cast<IMatSystemSurface *>(surface());
  bool disabled = true;
  if (s) {
    int left, top, right, bottom;
    s->GetClippingRect(left, top, right, bottom, disabled);
    if (lua_toboolean(L, 2)) s->DisableClipping(true);
  }
  HL2SB_PanelPaintManual(panel);
  if (s) s->DisableClipping(disabled);
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
  luaL_checkpanel(L, 1)->SetVisible(lua_toboolean(L, 2) != 0);
  return 0;
}

/*
** HL2SB: GMod's Panel metatable carries SetConVar/ConVarChanged natively, and
** menu dialogs reach them on panels that have no Lua-level override (the
** crosshair editor's DComboBox path: SetConVar on the combo, ConVarChanged
** from its OnSelect).  The name is stored on the panel's instance table under
** the same field GMod's own Lua reads back (DCheckBoxLabel checks
** pnl.Button.m_strConVar).  The new value is applied straight to the ConVar
** object -- deliberately NOT through RunConsoleCommand, whose Lua binding
** blocks exactly these names (GMod keeps them in its binding blocklist while
** the native path keeps working).
*/
static int Panel_SetConVar (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
  Panel_PushTable(L, pPanel);
  if (lua_istable(L, -1)) {
    lua_pushstring(L, pszName);
    lua_setfield(L, -2, "m_strConVar");
  }
  lua_pop(L, 1);
  return 0;
}

static int Panel_ConVarChanged (lua_State *L) {
  if (!lua_isnoneornil(L, 2)) {
    // Numbers convert in place; booleans become "1"/"0" like ConVar SetValue does.
    if (lua_isboolean(L, 2)) {
      lua_pushfstring(L, "%d", lua_toboolean(L, 2) != 0);
      lua_replace(L, 2);
    }
    const char *pszValue = lua_tostring(L, 2);
    if (pszValue != NULL && cvar != NULL) {
      Panel *pPanel = luaL_checkpanel(L, 1);
      Panel_PushTable(L, pPanel);
      if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "m_strConVar");
        const char *pszName = lua_tostring(L, -1);
        if (pszName != NULL && pszName[0] != '\0') {
          ConVar *pCVar = cvar->FindVar(pszName);
          if (pCVar != NULL)
            pCVar->SetValue(pszValue);
        }
      }
    }
  }
  lua_settop(L, 0);
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

// Keep tab navigation inside this editable window.
static int Panel_SetFocusTopLevel (lua_State *L) {
  Panel *pPanel = luaL_checkpanel(L, 1);
  bool bTopLevel = luaL_optboolean(L, 2, 1);

  if (FocusNavGroup *focus = HL2SB_LuaPanelFocusGroup(pPanel))
    focus->SetFocusTopLevel(bTopLevel);

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
  if ( !V_stricmp(pName, "beam") )      return vgui::dc_ibeam;
  if ( !V_stricmp(pName, "none") )      return vgui::dc_none;
  if ( !V_stricmp(pName, "blank") )     return vgui::dc_blank;
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
  Panel *pPanel = lua_topanel(L, 1);
  lua_pushboolean(L, pPanel != NULL && pPanel->GetVPanel() != 0 && !pPanel->IsMarkedForDeletion());
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

int HL2SB_LuaPanelGeometryIndex(lua_State *L) {
  Panel *p = lua_topanel(L, 1);
  const char *field = lua_tostring(L, 2);
  if (!p || !field || field[1]) return 0;
  int x, y; p->GetPos(x, y);
  switch (*field) {
    case 'x': lua_pushinteger(L, x); return 1;
    case 'y': lua_pushinteger(L, y); return 1;
    case 'w': lua_pushinteger(L, p->GetWide()); return 1;
    case 'h': lua_pushinteger(L, p->GetTall()); return 1;
  }
  return 0;
}

bool HL2SB_LuaPanelGeometryWrite(lua_State *L) {
  Panel *p = lua_topanel(L, 1);
  const char *field = lua_tostring(L, 2);
  if (!p || !field || !*field || field[1] || !strchr("xywh", *field)) return false;
  int value = luaL_checkint(L, 3), x, y; p->GetPos(x, y);
  if (*field == 'x') p->SetPos(value, y);
  if (*field == 'y') p->SetPos(x, value);
  if (*field == 'w') p->SetWide(value);
  if (*field == 'h') p->SetTall(value);
  return true;
}

static int Panel___index (lua_State *L) {
  Panel *p = lua_topanel(L, 1);
  if (!p) { lua_pushnil(L); return 1; }
  if (HL2SB_LuaPanelGeometryIndex(L)) return 1;
  Panel_GetRefTable(L);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2); lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  lua_getmetatable(L, 1);
  lua_pushvalue(L, 2); lua_rawget(L, -2);
  if (!lua_isnil(L, -1)) return 1;
  lua_pop(L, 2);
  luaL_getmetatable(L, "Panel");
  lua_pushvalue(L, 2); lua_rawget(L, -2);
  return 1;
}

static int Panel___newindex (lua_State *L) {
  Panel *p = lua_topanel(L, 1);
  if (!p) return 0;
  const char *field = luaL_checkstring(L, 2);
  if (HL2SB_LuaPanelGeometryWrite(L)) return 0;
  Panel_GetRefTable(L);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 3); lua_setfield(L, -2, field);
    if (!Q_strcmp(field, "OnChildAdded")) {
      lua_pushvalue(L, 3); lua_setfield(L, -2, "__hl2sb_PreparedOnChildAdded");
    }
  }
  lua_pop(L, 1);
  return 0;
}

// HL2SB: a dying Lua handle must NOT destroy the C++ panel.
//
// This used to `delete` the panel as soon as the last Lua handle was collected.  But
// a panel that lives in the vgui tree is OWNED BY ITS PARENT (Panel::~Panel deletes
// its autodelete children), and the Lua GC runs during allocation - i.e. it can run
// in the middle of Panel::PaintTraverse's children loop.  Deleting a live, parented
// panel there leaves the traversal reading freed entries, and the crash is a jump to
// NULL (or, when the memory was reused by a string, into that string) inside
// vgui::Panel::PaintTraverse -- exactly what every 2026-09-17 minidump shows.  It is
// also why the Lua spawnmenu kept finding "dead handles" in its panel pools.
//
// GMod never does this either: its Lua handle is a weak PHandle and vgui owns the
// lifetime.  So: a parented panel is handed to the parent (SetAutoDelete, same as
// GMod's vgui.Create) and kept alive; only a panel nothing owns any more - no parent
// and no other handle - is queued for deletion.
static int Panel___gc (lua_State *L) {
  LPanel *plPanel = dynamic_cast<LPanel *>(lua_topanel(L, 1));
  if (plPanel) {
    --plPanel->m_nRefCount;

    if (plPanel->GetVParent() != 0) {
      static int s_nParentedGc = 0;
      if (s_nParentedGc < 12) {
        ++s_nParentedGc;
        if ( hl2sb_dev_msgs.GetBool() )
        Msg("[HL2SB] panel gc: '%s' (%p) was still parented (handles left=%d) - kept alive\n",
          plPanel->GetName(), (void *)plPanel, plPanel->m_nRefCount);
      }

      plPanel->SetAutoDelete(true);
    }
    else if (plPanel->m_nRefCount <= 0) {
      plPanel->MarkForDeletion();
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
  Panel *panel = luaL_checkpanel(L, 1);
  int wide = 0, tall = 0;
  for (int i = 0; i < panel->GetChildCount(); ++i) {
    Panel *child = panel->GetChild(i);
    if (!child || !child->IsVisible() || child->IsMarkedForDeletion()) continue;
    int x, y, childWide, childTall;
    child->GetBounds(x, y, childWide, childTall);
    wide = MAX(wide, x + childWide);
    tall = MAX(tall, y + childTall);
  }
  int left, top, right, bottom;
  panel->GetDockPadding(left, top, right, bottom);
  if (lua_toboolean(L, 2)) panel->SetWide(wide + right);
  if (lua_toboolean(L, 3)) panel->SetTall(tall + bottom);
  return 0;
}

static int Panel_SizeToContents (lua_State *L) {
  Panel *panel = luaL_checkpanel(L, 1);
  if (Label *label = dynamic_cast<Label *>(panel))
    label->SizeToContents();
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

//-----------------------------------------------------------------------------
// HL2SB (2026-09-27): GMod's Panel meta is ONE shared table for every control
// (reference client.dll, 162 methods), so Panel-level names below
// are what GMod answers on ANY panel.  The ones this engine lacked and derma
// actually calls:
//-----------------------------------------------------------------------------

// GMod Panel:CursorPos() -> x, y -- cursor position in this panel's local space.
// (dslider/dscrollbar/dividers all use it; the fork's Lua had to fall back to
// ScreenToLocal per control.)
static int Panel_CursorPos (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  int x, y;
  vgui::input()->GetCursorPos( x, y );
  pPanel->ScreenToLocal( x, y );
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}

// GMod Panel:IsModal() -- MakePopup( true ) panels hold the app-modal surface.
static int Panel_IsModal (lua_State *L) {
  bool bModal = ( vgui::input()->GetAppModalSurface() == luaL_checkpanel(L, 1)->GetVPanel() );
  lua_pushboolean(L, bModal ? 1 : 0);
  return 1;
}

// Register non-popup controls in the surface's ordered paint list without
// changing keyboard or mouse input. A topmost flag alone does not paint them.
static int Panel_SetDrawOnTop (lua_State *L) {
  Panel *panel = luaL_checkpanel(L, 1);
  const bool onTop = luaL_checkboolean(L, 2) != 0;
  Panel_PushTable(L, panel);
  lua_pushliteral(L, "__hl2sb_DrawOnTopPopup");
  lua_rawget(L, -2);
  const bool promoted = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (onTop && !panel->IsPopup()) {
    vgui::surface()->CreatePopup(panel->GetVPanel(), false, false, false,
        panel->IsMouseInputEnabled(), panel->IsKeyBoardInputEnabled());
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "__hl2sb_DrawOnTopPopup");
  } else if (!onTop && promoted) {
    vgui::surface()->ReleasePanel(panel->GetVPanel());
    vgui::ipanel()->SetPopup(panel->GetVPanel(), false);
    lua_pushnil(L);
    lua_setfield(L, -2, "__hl2sb_DrawOnTopPopup");
  }
  lua_pop(L, 1);
  vgui::ipanel()->SetTopmostPopup(panel->GetVPanel(), onTop);
  if (onTop) vgui::surface()->MovePopupToFront(panel->GetVPanel());
  return 0;
}

// HL2SB (2026-10-13): GMod Panel:ParentToHUD() -- parents the panel to the
// game HUD viewport (reference behavior: the panel follows the HUD and is
// hidden on the escape menu).  g_pClientMode->GetViewport() is the same HUD
// container the engine HUD elements parent to.
static int Panel_ParentToHUD (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  if ( g_pClientMode == NULL )
    return 0;
  vgui::Panel *pViewport = g_pClientMode->GetViewport();
  if ( pViewport == NULL )
    return 0;
  pPanel->SetParent( pViewport );
  return 0;
}

// HL2SB (2026-10-13): GMod Panel:Add( class [, name ] ) -- creates a panel of
// the named class parented to this one and returns it (reference behavior:
// the vgui factory create with self as parent).  Routed through the Lua
// vgui.Create so this fork's scripted-class merge chain still runs; answers
// nil in states that have no vgui module.
static int Panel_Add (lua_State *L) {
  luaL_checkpanel(L, 1);
  lua_getglobal(L, "vgui");
  if ( !lua_istable(L, -1) ) { lua_pop(L, 1); lua_pushnil(L); return 1; }
  lua_getfield(L, -1, "Create");
  lua_remove(L, -2);
  if ( !lua_isfunction(L, -1) ) { lua_pop(L, 1); lua_pushnil(L); return 1; }
  lua_pushvalue(L, 2);                          // class name (or table)
  lua_pushvalue(L, 1);                          // self as the parent
  if ( lua_isnoneornil(L, 3) )
    lua_pushnil(L);
  else
    lua_pushvalue(L, 3);                        // optional panel name
  lua_call(L, 3, 1);
  return 1;
}

// HL2SB (2026-10-13): GMod scripts call SetPaintBackground; the C face under
// it is SetPaintBackgroundEnabled (GMod's binary registers that name too --
// its Lua-side alias is what panel code calls).  Same receiver, same shape.
static int Panel_SetPaintBackground (lua_State *L) {
  luaL_checkpanel(L, 1)->SetPaintBackgroundEnabled( luaL_checkboolean(L, 2) != 0 );
  return 0;
}

// GMod Panel:HasChildren()
static int Panel_HasChildren (lua_State *L) {
  bool bHas = luaL_checkpanel(L, 1)->GetChildCount() > 0;
  lua_pushboolean(L, bHas ? 1 : 0);
  return 1;
}

static int Panel_SetExpensiveShadow (lua_State *L) {
  Panel *panel = luaL_checkpanel(L, 1);
  Label *label = dynamic_cast<Label *>(panel);
  if (!label) return 0;
  int offset = (int)luaL_checknumber(L, 2);
  Color color(0, 0, 0, 180);
  if (lua_istable(L, 3)) {
    int values[] = {0, 0, 0, 255};
    const char *fields[] = {"r", "g", "b", "a"};
    for (int i = 0; i < 4; ++i) {
      lua_getfield(L, 3, fields[i]);
      if (lua_isnumber(L, -1)) values[i] = (int)lua_tonumber(L, -1);
      lua_pop(L, 1);
    }
    color = Color(values[0], values[1], values[2], values[3]);
  }
  HL2SB_SetLuaLabelShadow(label, offset, color);
  return 0;
}

static int Panel_SetFGColor (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  pPanel->SetFgColor( ::Color( (int)lua_tonumber(L, 2), (int)lua_tonumber(L, 3),
                              (int)lua_tonumber(L, 4), (int)lua_tonumber(L, 5) ) );
  return 0;
}

static int Panel_SetBGColor (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  pPanel->SetBgColor( ::Color( (int)lua_tonumber(L, 2), (int)lua_tonumber(L, 3),
                              (int)lua_tonumber(L, 4), (int)lua_tonumber(L, 5) ) );
  return 0;
}

// GMod's getters return a Color -- same answer as the engine spellings here.
static int Panel_GetFGColor (lua_State *L) {
  lua_pushcolor(L, luaL_checkpanel(L, 1)->GetFgColor());
  return 1;
}

static int Panel_GetBGColor (lua_State *L) {
  lua_pushcolor(L, luaL_checkpanel(L, 1)->GetBgColor());
  return 1;
}


/*
** ===========================================================================
** HL2SB: the text family on the shared Panel metatable.
**
** GMod keeps ONE method table for every panel object, built from every panel
** class's bindings (a plain Panel answers Panel:SetText, Panel:GetContentSize,
** ... exactly like a Label does).  This fork gives each class its own
** metatable, so a Derma control - whose engine base is "Panel" - could not
** reach anything the text controls bind.
**
** These methods therefore live on "Panel" and dispatch to the concrete vgui
** control, doing nothing when the panel has no text of that kind.  That is
** what a script observes on GMod: the call always succeeds, and only panels
** that actually own text show an effect.
** ===========================================================================
*/
static int Panel_DrawTextEntryText (lua_State *L) {
  if (LTextEntry *p = dynamic_cast<LTextEntry *>(luaL_checkpanel(L, 1)))
    p->DrawNativeText(luaL_checkcolor(L, 2), luaL_checkcolor(L, 3), luaL_checkcolor(L, 4));
  return 0;
}

static int Panel_KillFocus(lua_State *L) {
  Panel *p = luaL_checkpanel(L, 1);
  if (FocusNavGroup *focus = HL2SB_LuaPanelFocusGroup(p))
    focus->SetCurrentFocus(0, 0);
  else
    p->OnKillFocus();
  return 0;
}

static int Panel_NoClipping(lua_State *L) {
  Panel_GetRefTable(L);
  if (lua_istable(L, -1)) {
    lua_pushboolean(L, lua_toboolean(L, 2)); lua_setfield(L, -2, "__hl2sb_NoClipping");
  }
  lua_pop(L, 1);
  return 0;
}

static int Panel_SetTextSelectionColors(lua_State *L) {
  if (TextEntry *p = dynamic_cast<TextEntry *>(luaL_checkpanel(L, 1))) {
    p->SetSelectionTextColor(luaL_checkcolor(L, 2));
    p->SetSelectionBgColor(luaL_checkcolor(L, 3));
  }
  return 0;
}

static int Panel_Prepare (lua_State *L) {
  Panel_GetRefTable(L);
  if (lua_istable(L, -1)) {
    // Class tables are merged directly. Publish the child callback only after
    // Init, so a scroll panel can create its own canvas without reparenting it.
    lua_pushstring(L, "OnChildAdded");
    lua_rawget(L, -2);
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      lua_pushnil(L);
    }
    lua_setfield(L, -2, "__hl2sb_PreparedOnChildAdded");
  }
  lua_pop(L, 1);
  return 0;
}

static int Panel_SetFontInternal (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  HFont hFont = luaL_checkfont(L, 2);
  if (Label *pLabel = dynamic_cast<Label *>(pPanel))
    pLabel->SetFont(hFont);
  else if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    pEntry->SetFont(hFont);
  return 0;
}

static int Panel_SetText (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  const char *pszText = luaL_checkstring(L, 2);

  if (Label *pLabel = dynamic_cast<Label *>(pPanel)) {
    int top = lua_gettop(L);
    if (pszText[0] == '#') {
      lua_getglobal(L, "language");
      if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "GetPhrase");
        if (lua_isfunction(L, -1)) {
          lua_pushstring(L, pszText);
          if (lua_pcall(L, 1, 1, 0) == LUA_OK && lua_isstring(L, -1)) pszText = lua_tostring(L, -1);
        }
      }
    }
    // Unknown localization labels fall back to readable text without '#'.
    if (pszText[0] == '#' && (!g_pVGuiLocalize || !g_pVGuiLocalize->Find(pszText)))
      ++pszText;
    pLabel->SetText(pszText);
    lua_settop(L, top);
  }
  else if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    pEntry->SetText(pszText);
  return 0;
}

int HL2SB_LuaPanelSetText(lua_State *L) {
  return Panel_SetText(L);
}

static int Panel_GetText (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  char szText[1024];
  szText[0] = '\0';

  if (Label *pLabel = dynamic_cast<Label *>(pPanel))
    pLabel->GetText(szText, sizeof(szText));
  else if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    pEntry->GetText(szText, sizeof(szText));

  lua_pushstring(L, szText);
  return 1;
}

static int Panel_GetContentSize (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  int wide = 0, tall = 0;

  if (Label *pLabel = dynamic_cast<Label *>(pPanel))
    pLabel->GetContentSize(wide, tall);

  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

static int Panel_GetTextSize (lua_State *L) {
  Label *label = dynamic_cast<Label *>(luaL_checkpanel(L, 1));
  if (!label || !label->GetTextImage()) return 0;
  int wide = 0, tall = 0;
  label->GetTextImage()->GetContentSize(wide, tall);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

int HL2SB_LuaLabelAlignment(int alignment, bool toNative) {
  if (toNative) {
    if (alignment < 1 || alignment > 9) return -1;
    return ((9 - alignment) / 3) * 3 + (alignment - 1) % 3;
  }
  if (alignment < 0 || alignment > 8) return 4;
  return (2 - alignment / 3) * 3 + alignment % 3 + 1;
}

static int Panel_SetContentAlignment (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  int alignment = HL2SB_LuaLabelAlignment(luaL_checkint(L, 2), true);

  if (alignment >= 0)
    if (Label *pLabel = dynamic_cast<Label *>(pPanel))
      pLabel->SetContentAlignment((Label::Alignment)alignment);
  return 0;
}

static int Panel_SetTextInset (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  int xInset = luaL_checkint(L, 2);
  int yInset = luaL_checkint(L, 3);

  if (Label *pLabel = dynamic_cast<Label *>(pPanel))
    pLabel->SetTextInset(xInset, yInset);
  return 0;
}

static int Panel_GetTextInset (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  int xInset = 0, yInset = 0;

  if (Label *pLabel = dynamic_cast<Label *>(pPanel))
    pLabel->GetTextInset(&xInset, &yInset);

  lua_pushinteger(L, xInset);
  lua_pushinteger(L, yInset);
  return 2;
}


static int Panel_HasParent (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  // "self is a descendant of the argument" - the engine method of the same
  // name, and the direction GMod's own IsOurChild() relies on
  // (child:HasParent( self )).
  lua_pushboolean(L, pPanel->HasParent(luaL_checkvpanel(L, 2)));
  return 1;
}

static int Panel_IsOurChild(lua_State *L) {
  Panel *panel = luaL_checkpanel(L, 1);
  Panel *child = lua_topanel(L, 2);
  lua_pushboolean(L, child && child->HasParent(panel->GetVPanel()));
  return 1;
}

static int Panel_SelectAllText (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);

  if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    pEntry->SelectAllText(true);
  return 0;
}

static int Panel_SetAllowNonAsciiCharacters (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  bool bAllow = luaL_checkboolean(L, 2) != 0;

  if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    pEntry->SetAllowNonAsciiCharacters(bAllow);
  return 0;
}

static int Panel_SetMultiline (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  bool bMultiline = luaL_checkboolean(L, 2) != 0;

  if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    pEntry->SetMultiline(bMultiline);
  return 0;
}

static int Panel_IsMultiline (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  bool bMultiline = false;

  if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    bMultiline = pEntry->IsMultiline();

  lua_pushboolean(L, bMultiline);
  return 1;
}


static int Panel_SetCaretPos (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  int cursorPos = luaL_checkint(L, 2);

  if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    pEntry->SetCaretPos(cursorPos);
  return 0;
}

static int Panel_GetCaretPos (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  int cursorPos = 0;

  if (TextEntry *pEntry = dynamic_cast<TextEntry *>(pPanel))
    cursorPos = pEntry->GetCaretPos();

  lua_pushinteger(L, cursorPos);
  return 1;
}

static int Panel_DoModal (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);

  // Derma_Message / Derma_Query / Derma_StringRequest all end with
  // Window:DoModal(), and the window is a scripted Frame.
  if (Frame *pFrame = dynamic_cast<Frame *>(pPanel))
    pFrame->DoModal();
  return 0;
}


static int Panel_HasHierarchicalFocus (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  bool bFocused = false;

  if (vgui::input() && vgui::ipanel()) {
    VPANEL hFocused = vgui::input()->GetFocus();
    const VPANEL hSelf = pPanel->GetVPanel();

    while (hFocused != 0) {
      if (hFocused == hSelf) { bFocused = true; break; }
      hFocused = vgui::ipanel()->GetParent(hFocused);
    }
  }

  lua_pushboolean(L, bFocused);
  return 1;
}

static int Panel_PaintAt (lua_State *L) {
  vgui::Panel *pPanel = luaL_checkpanel(L, 1);
  int x = luaL_checkint(L, 2);
  int y = luaL_checkint(L, 3);

  // Paint the panel as if it sat at ( x, y ), then put it back.  GMod paints
  // panels off-position for the context menu's preview swatches.
  int oldX = 0, oldY = 0;
  pPanel->GetPos(oldX, oldY);
  pPanel->SetPos(x, y);
  if (vgui::ipanel())
    vgui::ipanel()->PaintTraverse(pPanel->GetVPanel(), false, true);
  pPanel->SetPos(oldX, oldY);
  return 0;
}

static int Panel_FocusPrevious (lua_State *L) {
  luaL_checkpanel(L, 1)->RequestFocusPrev();
  return 0;
}


extern int luasrc_UTIL_JSONToTable(lua_State *L);
extern int luasrc_UTIL_TableToJSON(lua_State *L);

static std::string HL2SB_HTMLQuote(const char *text)
{
  std::string result = "\"";
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    if (*p == '\\' || *p == '"') result += '\\';
    if (*p < 32 || *p == '<') {
      char escaped[8]; Q_snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
      result += escaped;
    } else result += (char)*p;
  }
  return result + "\"";
}

class CHL2SBLuaHTML : public vgui::HTML
{
  typedef vgui::HTML BaseClass;
public:
  lua_State *m_lua_State;
  int m_nTableReference;
  bool m_loading;
  std::vector<std::string> m_bridges;
  std::string m_tempFile;

  CHL2SBLuaHTML(Panel *parent, const char *name, lua_State *state)
    : HTML(parent, name, true), m_lua_State(state), m_loading(true)
  {
    luaPushPanelRefTable(state, this, true);
    m_nTableReference = luaL_ref(state, LUA_REGISTRYINDEX);
    HL2SB_LuaPanelTrack(this, state);
  }

  virtual ~CHL2SBLuaHTML()
  {
    HL2SB_LuaPanelUntrack(this);
    if (Live()) luaDropPanelRefTable(m_lua_State, this);
    HL2SB_LuaPanelUnref(m_lua_State, m_nTableReference);
    if (!m_tempFile.empty()) filesystem->RemoveFile(m_tempFile.c_str(), "DEFAULT_WRITE_PATH");
  }

  bool Live() const { return m_lua_State && (m_lua_State == ::L || m_lua_State == LGameUI); }

  std::string BridgeScript() const
  {
    std::string script =
      "window.IN_ENGINE=true;window.__hl2sbHTMLCallbacks=window.__hl2sbHTMLCallbacks||{};"
      "window.__hl2sbHTMLNext=window.__hl2sbHTMLNext||0;"
      "window.__hl2sbHTMLReply=function(id,args){var cb=window.__hl2sbHTMLCallbacks[id];"
      "delete window.__hl2sbHTMLCallbacks[id];if(cb)cb.apply(null,args||[]);};";
    for (size_t i = 0; i < m_bridges.size(); ++i) script += m_bridges[i];
    return script;
  }

  void AddObject(const char *name)
  {
    std::string key = HL2SB_HTMLQuote(name);
    std::string script = "window[" + key + "]=window[" + key + "]||{};";
    m_bridges.push_back(script);
    if (!m_loading) RunJavascript(script.c_str());
  }

  void AddCallback(const char *object, const char *function)
  {
    std::string script = "window[" + HL2SB_HTMLQuote(object) + "][" + HL2SB_HTMLQuote(function) + "]=function(){"
      "var args=Array.prototype.slice.call(arguments),cb=null;"
      "if(typeof args[args.length-1]==='function')cb=args.pop();"
      "var id=++window.__hl2sbHTMLNext;if(cb)window.__hl2sbHTMLCallbacks[id]=cb;"
      "var f=document.createElement('iframe');f.style.display='none';"
      "f.src='https://hl2sb.invalid/?'+encodeURIComponent(JSON.stringify([" +
      HL2SB_HTMLQuote(object) + "," + HL2SB_HTMLQuote(function) + ",id,args]));"
      "document.documentElement.appendChild(f);setTimeout(function(){f.remove();},1000);};";
    m_bridges.push_back(script);
    if (!m_loading) RunJavascript(script.c_str());
  }

  virtual bool OnStartRequest(const char *url, const char *target, const char *post, bool redirect)
  {
    const char *prefix = "https://hl2sb.invalid/?";
    if (!Q_strnicmp(url, prefix, Q_strlen(prefix))) {
      std::string data;
      for (const char *p = url + Q_strlen(prefix); *p; ++p) {
        if (*p == '%' && p[1] && p[2]) {
          char hex[3] = {p[1], p[2], 0};
          data += (char)strtol(hex, NULL, 16); p += 2;
        } else data += *p;
      }
      if (Live()) {
        lua_State *state = m_lua_State;
        int top = lua_gettop(state);
        lua_checkstack(state, 32);
        lua_pushcfunction(state, luasrc_UTIL_JSONToTable);
        lua_pushlstring(state, data.data(), data.size());
        if (lua_pcall(state, 1, 1, 0) == LUA_OK && lua_istable(state, -1)) {
          int message = lua_gettop(state);
          lua_rawgeti(state, message, 3); int id = lua_tointeger(state, -1); lua_pop(state, 1);
          lua_getref(state, m_nTableReference); lua_getfield(state, -1, "OnCallback"); lua_remove(state, -2);
          if (lua_isfunction(state, -1)) {
            lua_pushpanel(state, this);
            lua_rawgeti(state, message, 1); lua_rawgeti(state, message, 2); lua_rawgeti(state, message, 4);
            int first = lua_gettop(state) - 4;
            if (lua_pcall(state, 4, LUA_MULTRET, 0) == LUA_OK) {
              lua_checkstack(state, 16);
              int count = lua_gettop(state) - first + 1;
              lua_newtable(state);
              for (int i = 0; i < count; ++i) { lua_pushvalue(state, first + i); lua_rawseti(state, -2, i + 1); }
              lua_pushcfunction(state, luasrc_UTIL_TableToJSON); lua_insert(state, -2);
              if (lua_pcall(state, 1, 1, 0) == LUA_OK && lua_isstring(state, -1)) {
                char call[64]; Q_snprintf(call, sizeof(call), "window.__hl2sbHTMLReply(%d,", id);
                std::string reply = std::string(call) + lua_tostring(state, -1) + ");";
                RunJavascript(reply.c_str());
              }
            } else luasrc_LuaWarnMsgF("HTML callback: %s\n", lua_tostring(state, -1));
          }
        }
        lua_settop(state, top);
      }
      return false;
    }
    return BaseClass::OnStartRequest(url, target, post, redirect);
  }

  virtual void OnFinishRequest(const char *url, const char *title, const CUtlMap<CUtlString, CUtlString> &headers)
  {
    m_loading = false;
    RunJavascript(BridgeScript().c_str());
    BEGIN_LUA_CALL_PANEL_METHOD("OnFinishLoadingDocument");
      lua_pushstring(m_lua_State, url);
    END_LUA_CALL_PANEL_METHOD(1, 0);
    BEGIN_LUA_CALL_PANEL_METHOD("OnDocumentReady");
      lua_pushstring(m_lua_State, url);
    END_LUA_CALL_PANEL_METHOD(1, 0);
  }

  virtual void OnThink()
  {
    BaseClass::OnThink();
    BEGIN_LUA_CALL_PANEL_METHOD("Think");
    END_LUA_CALL_PANEL_METHOD(0, 0);
  }

  virtual void OnURLChanged(const char *url, const char *post, bool redirect)
  {
    BEGIN_LUA_CALL_PANEL_METHOD("OnChangeAddressBar");
      lua_pushstring(m_lua_State, url);
    END_LUA_CALL_PANEL_METHOD(1, 0);
  }

  virtual void OnSetHTMLTitle(const char *title)
  {
    BEGIN_LUA_CALL_PANEL_METHOD("OnChangeTitle");
      lua_pushstring(m_lua_State, title);
    END_LUA_CALL_PANEL_METHOD(1, 0);
  }

  void LoadMarkup(const char *markup)
  {
    m_loading = true;
    std::string document = "<script>" + BridgeScript() + "</script>" + markup;
    char temporary[MAX_PATH], full[MAX_PATH];
    Q_snprintf(temporary, sizeof(temporary), "data/html/hl2sb_%p.html", this);
    filesystem->CreateDirHierarchy("data/html", "DEFAULT_WRITE_PATH");
    CUtlBuffer data(document.data(), document.size(), CUtlBuffer::READ_ONLY);
    if (filesystem->WriteFile(temporary, "DEFAULT_WRITE_PATH", data) &&
        filesystem->RelativePathToFullPath(temporary, "DEFAULT_WRITE_PATH", full, sizeof(full))) {
      m_tempFile = temporary; V_FixSlashes(full, '/');
      BaseClass::OpenURL((std::string("file:///") + full).c_str(), NULL);
    }
  }

  void LoadURL(const char *url)
  {
    m_loading = true;
    BEGIN_LUA_CALL_PANEL_METHOD("OnBeginLoadingDocument");
      lua_pushstring(m_lua_State, url);
    END_LUA_CALL_PANEL_METHOD(1, 0);
    std::string target = url;
    const char *asset = "asset://garrysmod/";
    if (!Q_strnicmp(url, asset, Q_strlen(asset))) {
      const char *relative = url + Q_strlen(asset);
      char full[MAX_PATH];
      if (filesystem->RelativePathToFullPath(relative, "GAME", full, sizeof(full))) {
        CUtlBuffer data;
        if (filesystem->ReadFile(relative, "GAME", data)) {
          char directory[MAX_PATH]; Q_strncpy(directory, full, sizeof(directory));
          V_StripFilename(directory); V_FixSlashes(directory, '/');
          std::string document = "<base href=\"file:///" + std::string(directory) + "/\"><script>" + BridgeScript() + "</script>";
          document.append((const char *)data.Base(), data.TellPut());
          char temporary[MAX_PATH]; Q_snprintf(temporary, sizeof(temporary), "data/html/hl2sb_%p.html", this);
          filesystem->CreateDirHierarchy("data/html", "DEFAULT_WRITE_PATH");
          CUtlBuffer output(document.data(), document.size(), CUtlBuffer::READ_ONLY);
          if (filesystem->WriteFile(temporary, "DEFAULT_WRITE_PATH", output) &&
              filesystem->RelativePathToFullPath(temporary, "DEFAULT_WRITE_PATH", full, sizeof(full))) {
            m_tempFile = temporary; V_FixSlashes(full, '/');
            target = std::string("file:///") + full;
          }
        }
      }
    }
    BaseClass::OpenURL(target.c_str(), NULL);
  }
};

int HL2SB_LuaCreateHTML(lua_State *L)
{
  CHL2SBLuaHTML *panel = new CHL2SBLuaHTML(luaL_optpanel(L, 1, VGui_GetClientLuaRootPanel()), luaL_optstring(L, 2, "HTML"), L);
  lua_pushpanel(L, panel);
  return 1;
}

static int Panel_HTMLOpenURL(lua_State *L) {
  if (CHL2SBLuaHTML *p = dynamic_cast<CHL2SBLuaHTML *>(luaL_checkpanel(L, 1))) p->LoadURL(luaL_checkstring(L, 2));
  return 0;
}
static int Panel_HTMLJavascript(lua_State *L) {
  if (HTML *p = dynamic_cast<HTML *>(luaL_checkpanel(L, 1))) p->RunJavascript(luaL_checkstring(L, 2));
  return 0;
}
static int Panel_HTMLIsLoading(lua_State *L) {
  CHL2SBLuaHTML *p = dynamic_cast<CHL2SBLuaHTML *>(luaL_checkpanel(L, 1));
  lua_pushboolean(L, p && p->m_loading); return 1;
}
static int Panel_HTMLNewObject(lua_State *L) {
  if (CHL2SBLuaHTML *p = dynamic_cast<CHL2SBLuaHTML *>(luaL_checkpanel(L, 1))) p->AddObject(luaL_checkstring(L, 2));
  return 0;
}
static int Panel_HTMLNewCallback(lua_State *L) {
  if (CHL2SBLuaHTML *p = dynamic_cast<CHL2SBLuaHTML *>(luaL_checkpanel(L, 1))) p->AddCallback(luaL_checkstring(L, 2), luaL_checkstring(L, 3));
  return 0;
}
static int Panel_HTMLSetHTML(lua_State *L) {
  if (CHL2SBLuaHTML *p = dynamic_cast<CHL2SBLuaHTML *>(luaL_checkpanel(L, 1))) p->LoadMarkup(luaL_checkstring(L, 2));
  return 0;
}
static int Panel_HTMLScrollbars(lua_State *L) {
  if (HTML *p = dynamic_cast<HTML *>(luaL_checkpanel(L, 1))) p->SetScrollbarsEnabled(lua_toboolean(L, 2));
  return 0;
}
static int Panel_HTMLStop(lua_State *L) {
  if (CHL2SBLuaHTML *p = dynamic_cast<CHL2SBLuaHTML *>(luaL_checkpanel(L, 1))) { p->StopLoading(); p->m_loading = false; }
  return 0;
}
static int Panel_HTMLRefresh(lua_State *L) {
  if (HTML *p = dynamic_cast<HTML *>(luaL_checkpanel(L, 1))) p->Refresh();
  return 0;
}
static int Panel_HTMLBack(lua_State *L) {
  if (HTML *p = dynamic_cast<HTML *>(luaL_checkpanel(L, 1))) p->GoBack();
  return 0;
}
static int Panel_HTMLForward(lua_State *L) {
  if (HTML *p = dynamic_cast<HTML *>(luaL_checkpanel(L, 1))) p->GoForward();
  return 0;
}
static int Panel_HTMLUpdate(lua_State *L) {
  if (HTML *p = dynamic_cast<HTML *>(luaL_checkpanel(L, 1))) p->Repaint();
  return 0;
}

static const luaL_Reg Panelmeta[] = {
  {"OpenURL", Panel_HTMLOpenURL},
  {"RunJavascript", Panel_HTMLJavascript},
  {"IsLoading", Panel_HTMLIsLoading},
  {"NewObject", Panel_HTMLNewObject},
  {"NewObjectCallback", Panel_HTMLNewCallback},
  {"SetHTML", Panel_HTMLSetHTML},
  {"SetScrollbarsEnabled", Panel_HTMLScrollbars},
  {"StopLoading", Panel_HTMLStop},
  {"Refresh", Panel_HTMLRefresh},
  {"GoBack", Panel_HTMLBack},
  {"GoForward", Panel_HTMLForward},
  {"UpdateHTMLTexture", Panel_HTMLUpdate},
  // HL2SB: text family on the shared table (see the block above).
  {"GetContentSize", Panel_GetContentSize},
  {"GetTextSize", Panel_GetTextSize},
  {"DrawTextEntryText", Panel_DrawTextEntryText},
  {"KillFocus", Panel_KillFocus},
  {"NoClipping", Panel_NoClipping},
  {"SetTextSelectionColors", Panel_SetTextSelectionColors},
  {"GetText", Panel_GetText},
  {"GetTextInset", Panel_GetTextInset},
  {"GetValue", Panel_GetText},
  {"DoModal", Panel_DoModal},
  {"GetCaretPos", Panel_GetCaretPos},
  {"SetCaretPos", Panel_SetCaretPos},
  {"HasParent", Panel_HasParent},
  {"HasHierarchicalFocus", Panel_HasHierarchicalFocus},
  {"PaintAt", Panel_PaintAt},
  {"Prepare", Panel_Prepare},
  {"FocusPrevious", Panel_FocusPrevious},
  {"IsMultiline", Panel_IsMultiline},
  {"SelectAllText", Panel_SelectAllText},
  {"SetAllowNonAsciiCharacters", Panel_SetAllowNonAsciiCharacters},
  {"SetMultiline", Panel_SetMultiline},
  {"SetContentAlignment", Panel_SetContentAlignment},
  {"SetFontInternal", Panel_SetFontInternal},
  {"SetText", Panel_SetText},
  {"SetTextInset", Panel_SetTextInset},
  // HL2SB (2026-09-27): GMod's shared-panel-meta names this engine lacked --
  // aliases onto existing bindings plus the new implementations above.
  {"ChildCount", Panel_GetChildCount},
  {"CursorPos", Panel_CursorPos},
  {"Find", Panel_FindChildByName},
  {"GetBGColor", Panel_GetBGColor},
  {"GetFGColor", Panel_GetFGColor},
  {"HasChildren", Panel_HasChildren},
  {"IsKeyboardInputEnabled", Panel_IsKeyBoardInputEnabled},
  {"IsModal", Panel_IsModal},
  {"Add", Panel_Add},
  {"ParentToHUD", Panel_ParentToHUD},
  {"SetPaintBackground", Panel_SetPaintBackground},
  {"SetExpensiveShadow", Panel_SetExpensiveShadow},
  {"SetBGColor", Panel_SetBGColor},
  {"SetDrawOnTop", Panel_SetDrawOnTop},
  {"SetFGColor", Panel_SetFGColor},
  {"SetKeyboardInputEnabled", Panel_SetKeyBoardInputEnabled},
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
  {"SizeToContents", Panel_SizeToContents},
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
  {"SetRenderInScreenshots", Panel_SetRenderInScreenshots},
  {"IsWorldClicker", Panel_IsWorldClicker},
  {"SetWorldClicker", Panel_SetWorldClicker},
  {"DrawFilledRect", Panel_DrawFilledRect},
  {"DrawOutlinedRect", Panel_DrawOutlinedRect},
  // HL2SB: GMod spells this IsHovered - DButton and DPanel call self:IsHovered()
  // throughout derma - but this fork only ever bound IsCursorOver, so every such
  // call died on a nil method.  Same function, GMod's name.
  {"IsHovered", Panel_IsCursorOver},
  {"IsWithin", Panel_IsWithin},
  {"IsWithinTraverse", Panel_IsWithinTraverse},
  {"KB_AddBoundKey", Panel_KB_AddBoundKey},
  {"KB_ChainToMap", Panel_KB_ChainToMap},
  {"KeyCodeToString", Panel_KeyCodeToString},
  {"LocalToScreen", Panel_LocalToScreen},
  {"MakePopup", Panel_MakePopup},
  {"MakeReadyForUse", Panel_MakeReadyForUse},
  {"IsMarkedForDeletion", Panel_IsMarkedForDeletion},
  {"IsOurChild", Panel_IsOurChild},
  {"MarkForDeletion", Panel_MarkForDeletion},
  {"MoveToBack", Panel_MoveToBack},
  {"FocusNext", Panel_FocusNext},
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
  {"SetPaintedManually", Panel_SetPaintedManually},
  {"PaintManual", Panel_PaintManual},
  {"SetSkipChildDuringPainting", Panel_SetSkipChildDuringPainting},
  {"SetStartDragWhenMouseExitsPanel", Panel_SetStartDragWhenMouseExitsPanel},
  {"SetTabPosition", Panel_SetTabPosition},
  {"SetTall", Panel_SetTall},
  {"SetTriplePressAllowed", Panel_SetTriplePressAllowed},
  {"SetVisible", Panel_SetVisible},
  // HL2SB: GMod's Panel-level convar pair (see the comment at Panel_SetConVar).
  {"SetConVar", Panel_SetConVar},
  {"ConVarChanged", Panel_ConVarChanged},
  {"SetWide", Panel_SetWide},
  {"SetZPos", Panel_SetZPos},
  // HL2SB: GMod's names for the rest of what Derma controls call.
  {"SetFocusTopLevel", Panel_SetFocusTopLevel},
  {"SetCursor", Panel_SetCursor},
  {"MouseCapture", Panel_MouseCapture},
  {"Remove", Panel_Remove},
  {"IsValid", Panel_IsValid},
  // HL2SB: the same function under GMod's second spelling.
  {"Valid", Panel_IsValid},
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
  VGUI_SetScreenshotPanelVisibleCallback( Panel_ShouldRenderInScreenshot );
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

