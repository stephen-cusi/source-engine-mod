//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include <cbase.h>

#include <vgui_int.h>
#include <luamanager.h>
#include <vgui_controls/lPanel.h>
#include <lColor.h>

#include <scripted_controls/lCheckButton.h>

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
LCheckButton::LCheckButton(Panel *parent, const char *panelName, const char *text, lua_State *L) : CheckButton(parent, panelName, text)
{
#if defined( LUA_SDK )
	m_lua_State = L;
	m_nTableReference = LUA_NOREF;
	m_nRefCount = 0;
#endif // LUA_SDK
}


//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
LCheckButton::~LCheckButton()
{
#if defined( LUA_SDK )
	lua_unref( m_lua_State, m_nTableReference );
#endif // LUA_SDK
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void LCheckButton::OnCheckButtonChecked()
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_CHECKBUTTON_METHOD( "OnCheckButtonChecked" );
	END_LUA_CALL_PANEL_METHOD( 0, 0 );
#endif
}

/*
** access functions (stack -> C)
*/


LUA_API lua_CheckButton *lua_tocheckbutton (lua_State *L, int idx) {
  PHandle *phPanel = dynamic_cast<PHandle *>((PHandle *)lua_touserdata(L, idx));
  if (phPanel == NULL)
    return NULL;
  return dynamic_cast<lua_CheckButton *>(phPanel->Get());
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushcheckbutton (lua_State *L, CheckButton *pCheckButton) {
  LCheckButton *plCheckButton = dynamic_cast<LCheckButton *>(pCheckButton);
  if (plCheckButton)
    ++plCheckButton->m_nRefCount;
  PHandle *phPanel = (PHandle *)lua_newuserdata(L, sizeof(PHandle));
  phPanel->Set(pCheckButton);
  luaL_getmetatable(L, "CheckButton");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_CheckButton *luaL_checkcheckbutton (lua_State *L, int narg) {
  lua_CheckButton *d = lua_tocheckbutton(L, narg);
  if (d == NULL)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "CheckButton expected, got INVALID_PANEL");
  return d;
}


static int CheckButton_ChainToAnimationMap (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->ChainToAnimationMap();
  return 0;
}

static int CheckButton_ChainToMap (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->ChainToMap();
  return 0;
}

static int CheckButton_GetDisabledBgColor (lua_State *L) {
  lua_pushcolor(L, luaL_checkcheckbutton(L, 1)->GetDisabledBgColor());
  return 1;
}

static int CheckButton_GetDisabledFgColor (lua_State *L) {
  lua_pushcolor(L, luaL_checkcheckbutton(L, 1)->GetDisabledFgColor());
  return 1;
}

static int CheckButton_GetPanelBaseClassName (lua_State *L) {
  lua_pushstring(L, luaL_checkcheckbutton(L, 1)->GetPanelBaseClassName());
  return 1;
}

static int CheckButton_GetPanelClassName (lua_State *L) {
  lua_pushstring(L, luaL_checkcheckbutton(L, 1)->GetPanelClassName());
  return 1;
}

static int CheckButton_GetRefTable (lua_State *L) {
  LCheckButton *plCheckButton = dynamic_cast<LCheckButton *>(luaL_checkcheckbutton(L, 1));
  if (plCheckButton) {
    if (plCheckButton->m_nTableReference == LUA_NOREF) {
      lua_newtable(L);
      plCheckButton->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_getref(L, plCheckButton->m_nTableReference);
  }
  else
    lua_pushnil(L);
  return 1;
}

static int CheckButton_KB_AddBoundKey (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->KB_AddBoundKey(luaL_checkstring(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int CheckButton_KB_ChainToMap (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->KB_ChainToMap();
  return 0;
}

static int CheckButton_SetCheckButtonCheckable (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->SetCheckButtonCheckable(luaL_checkboolean(L, 2));
  return 0;
}

/*
** HL2SB: GMod's DCheckBox spells the state pair SetChecked / GetChecked.
** Same storage as SetSelected / IsSelected (CheckButton wraps ToggleButton's
** selected flag), just GMod's names, so the custom panel layer does not need a
** Lua wrapper per call.
*/
static int CheckButton_SetChecked (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->SetSelected(luaL_checkboolean(L, 2));
  return 0;
}

static int CheckButton_GetChecked (lua_State *L) {
  lua_pushboolean(L, luaL_checkcheckbutton(L, 1)->IsSelected());
  return 1;
}

static int CheckButton_IsChecked (lua_State *L) {
  lua_pushboolean(L, luaL_checkcheckbutton(L, 1)->IsSelected());
  return 1;
}

/*
** HL2SB: the caption lives on vgui::Label and CheckButton derives from it
** (CheckButton : ToggleButton : Button : Label), but the Lua side CANNOT reach
** Label's bindings through a checkbox panel:
**
**   luaL_checklabel() validates the METATABLE NAME ("Label") via
**   luaL_checkudata before lua_tolabel()'s dynamic_cast ever runs, so calling
**   Label:SetText( checkboxPanel ) fails with
**
**       bad argument #1 to 'EngineSetText' (Label expected, got INVALID_PANEL)
**
** These four forwarders publish the same methods on the CheckButton metatable,
** which is exactly what a GMod script expects from a checkbox:
** panel:SetText / GetText / SizeToContents.  They are plain Label calls -- no
** ABI change, no vgui2 rebuild.
*/
static int CheckButton_SetText (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->SetText(luaL_checkstring(L, 2));
  return 0;
}

static int CheckButton_GetText (lua_State *L) {
  char buffer[1024];
  luaL_checkcheckbutton(L, 1)->GetText(buffer, sizeof(buffer));
  lua_pushstring(L, buffer);
  return 1;
}

static int CheckButton_SizeToContents (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->SizeToContents();
  return 0;
}

static int CheckButton_GetContentSize (lua_State *L) {
  int wide = 0, tall = 0;
  luaL_checkcheckbutton(L, 1)->GetContentSize(wide, tall);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

/* The caption's text inset -- what Derma's SetIndent is built on. */
static int CheckButton_GetTextInset (lua_State *L) {
  int xInset = 0, yInset = 0;
  luaL_checkcheckbutton(L, 1)->GetTextInset(&xInset, &yInset);
  lua_pushinteger(L, xInset);
  lua_pushinteger(L, yInset);
  return 2;
}

static int CheckButton_SetTextInset (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->SetTextInset(luaL_checkint(L, 2), luaL_checkint(L, 3));
  return 0;
}

/*
** HL2SB: see the comment on the declaration in lCheckButton.h -- every check /
** uncheck funnels through SetSelected (the user click via ToggleButton::DoClick,
** scripts via SetChecked), so the Lua hook is dispatched here rather than
** waiting for a "CheckButtonChecked" action signal that is never posted to
** anything when no AddActionSignalTarget listener exists.
*/
void LCheckButton::SetSelected( bool state )
{
	BaseClass::SetSelected( state );

#ifdef LUA_SDK
	if ( m_nTableReference >= 0 )
	{
		lua_getref( m_lua_State, m_nTableReference );
		lua_getfield( m_lua_State, -1, "OnCheckButtonChecked" );
		lua_remove( m_lua_State, -2 );
		if ( lua_isfunction( m_lua_State, -1 ) )
		{
			lua_pushcheckbutton( m_lua_State, this );
			lua_pushboolean( m_lua_State, state );
			luasrc_pcall( m_lua_State, 2, 0, 0 );
		}
		else
		{
			lua_pop( m_lua_State, 1 );
		}
	}
#endif
}

static int CheckButton_SetSelected (lua_State *L) {
  luaL_checkcheckbutton(L, 1)->SetSelected(luaL_checkboolean(L, 2));
  return 0;
}

static int CheckButton___index (lua_State *L) {
  CheckButton *pCheckButton = lua_tocheckbutton(L, 1);
  if (pCheckButton == NULL) {  /* avoid extra test when d is not 0 */
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
  LCheckButton *plCheckButton = dynamic_cast<LCheckButton *>(pCheckButton);
  if (plCheckButton && plCheckButton->m_nTableReference != LUA_NOREF) {
    lua_getref(L, plCheckButton->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      lua_getmetatable(L, 1);
      lua_pushvalue(L, 2);
      lua_gettable(L, -2);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        luaL_getmetatable(L, "Button");
        lua_pushvalue(L, 2);
        lua_gettable(L, -2);
        if (lua_isnil(L, -1)) {
          lua_pop(L, 2);
          luaL_getmetatable(L, "Panel");
          lua_pushvalue(L, 2);
          lua_gettable(L, -2);
        }
      }
    }
  } else {
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      luaL_getmetatable(L, "Button");
      lua_pushvalue(L, 2);
      lua_gettable(L, -2);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        luaL_getmetatable(L, "Panel");
        lua_pushvalue(L, 2);
        lua_gettable(L, -2);
      }
    }
  }
  return 1;
}

static int CheckButton___newindex (lua_State *L) {
  CheckButton *pCheckButton = lua_tocheckbutton(L, 1);
  if (pCheckButton == NULL) {  /* avoid extra test when d is not 0 */
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
  LCheckButton *plCheckButton = dynamic_cast<LCheckButton *>(pCheckButton);
  if (plCheckButton) {
    if (plCheckButton->m_nTableReference == LUA_NOREF) {
      lua_newtable(L);
      plCheckButton->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_getref(L, plCheckButton->m_nTableReference);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, luaL_checkstring(L, 2));
	lua_pop(L, 1);
    return 0;
  } else {
    lua_Debug ar1;
    lua_getstack(L, 1, &ar1);
    lua_getinfo(L, "fl", &ar1);
    lua_Debug ar2;
    lua_getinfo(L, ">S", &ar2);
    lua_pushfstring(L, "%s:%d: attempt to index a non-scripted panel", ar2.short_src, ar1.currentline);
    return lua_error(L);
  }
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
static int CheckButton___gc (lua_State *L) {
  LCheckButton *plCheckButton = dynamic_cast<LCheckButton *>(lua_tocheckbutton(L, 1));
  if (plCheckButton) {
    --plCheckButton->m_nRefCount;

    if (plCheckButton->GetVParent() != 0) {
      static int s_nParentedGc = 0;
      if (s_nParentedGc < 12) {
        ++s_nParentedGc;
        Msg("[HL2SB] panel gc: '%s' (%p) was still parented (handles left=%d) - kept alive\n",
          plCheckButton->GetName(), (void *)plCheckButton, plCheckButton->m_nRefCount);
      }

      plCheckButton->SetAutoDelete(true);
    }
    else if (plCheckButton->m_nRefCount <= 0) {
      plCheckButton->MarkForDeletion();
    }
  }
  return 0;
}

static int CheckButton___eq (lua_State *L) {
  lua_pushboolean(L, lua_tocheckbutton(L, 1) == lua_tocheckbutton(L, 2));
  return 1;
}

static int CheckButton___tostring (lua_State *L) {
  CheckButton *pCheckButton = lua_tocheckbutton(L, 1);
  if (pCheckButton == NULL)
    lua_pushstring(L, "INVALID_PANEL");
  else {
    const char *pName = pCheckButton->GetName();
    if (Q_strcmp(pName, "") == 0)
      pName = "(no name)";
    lua_pushfstring(L, "CheckButton: \"%s\"", pName);
  }
  return 1;
}


static const luaL_Reg CheckButtonmeta[] = {
  {"ChainToAnimationMap", CheckButton_ChainToAnimationMap},
  {"ChainToMap", CheckButton_ChainToMap},
  {"GetDisabledBgColor", CheckButton_GetDisabledBgColor},
  {"GetDisabledFgColor", CheckButton_GetDisabledFgColor},
  {"GetPanelBaseClassName", CheckButton_GetPanelBaseClassName},
  {"GetPanelClassName", CheckButton_GetPanelClassName},
  {"GetRefTable", CheckButton_GetRefTable},
  {"KB_AddBoundKey", CheckButton_KB_AddBoundKey},
  {"KB_ChainToMap", CheckButton_KB_ChainToMap},
  {"SetCheckButtonCheckable", CheckButton_SetCheckButtonCheckable},
  {"SetSelected", CheckButton_SetSelected},
  {"SetChecked", CheckButton_SetChecked},
  {"GetChecked", CheckButton_GetChecked},
  {"IsChecked", CheckButton_IsChecked},
  {"SetText", CheckButton_SetText},
  {"GetText", CheckButton_GetText},
  {"SizeToContents", CheckButton_SizeToContents},
  {"GetContentSize", CheckButton_GetContentSize},
  {"GetTextInset", CheckButton_GetTextInset},
  {"SetTextInset", CheckButton_SetTextInset},
  {"__index", CheckButton___index},
  {"__newindex", CheckButton___newindex},
  {"__gc", CheckButton___gc},
  {"__eq", CheckButton___eq},
  {"__tostring", CheckButton___tostring},
  {NULL, NULL}
};


static int luasrc_CheckButton (lua_State *L) {
  CheckButton *pCheckButton = new LCheckButton(luaL_optpanel(L, 1, VGui_GetClientLuaRootPanel()), luaL_optstring(L, 2, NULL), luaL_optstring(L, 3, NULL), L);
  lua_pushcheckbutton(L, pCheckButton);
  return 1;
}


static const luaL_Reg CheckButton_funcs[] = {
  {"CheckButton", luasrc_CheckButton},
  {NULL, NULL}
};


/*
** Open CheckButton object
*/
LUALIB_API int luaopen_vgui_CheckButton(lua_State *L) {
  luaL_newmetatable(L, "CheckButton");
  luaL_register(L, NULL, CheckButtonmeta);
  lua_pushstring(L, "panel");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "panel" */
  luaL_register(L, "vgui", CheckButton_funcs);
  lua_pop(L, 2);
  return 1;
}
