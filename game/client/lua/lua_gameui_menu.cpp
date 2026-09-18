//=============================================================================//
//
// HL2SB: the main-menu (GameUI) Lua realm - a small library of its own.
//
// WHY THIS FILE EXISTS
//
//   The mod's main menu is a hand-opened lua_State (luamanager.cpp: luasrc_init_gameui,
//   "LGameUI").  Dialogs are built there out of the same scripted panel classes the game
//   realm uses (LPanel / LFrame / LLabel / LButton / LCheckButton / LTextEntry), but the
//   menu realm has no frame loop of its own that would run vgui's layout pass the way the
//   in-game one does:
//
//     [HL2SB] addonsdialog dbg:   1..14 Panel/Button/FrameSystemButton pos=(0,0) size=64x24
//     [HL2SB] addonsdialog dbg:   15 LLabel      pos=(0,0) size=442x36
//     [HL2SB] addonsdialog dbg:   19 LCheckButton pos=(0,0) size=442x26
//
//   SetSize() lands immediately, SetPos() does not - it is applied by Panel::PerformLayout,
//   which nothing calls in this realm, so every control (and even the frame's own title bar
//   and FrameSystemButtons) stays at (0,0) and the dialogs come up as an overlapping mess.
//
// WHAT GARRY'S MOD DOES
//
//   GMod's menu Lua is part of its own vgui library: the panel classes are vgui's, the
//   menu pulls the GMod Lua stack in through the regular loader, and the engine's paint
//   traversal (which calls PerformLayout when a panel's layout is invalid) covers it.  The
//   scripted panel classes of this fork *do* dispatch PerformLayout to Lua
//   (lPanel.cpp:397, lFrame.cpp:77, lLabel.h:116, lTextEntry.h:122, lModelPanel.cpp:331) -
//   the pass simply never runs here, which is the whole bug.  This file is that missing
//   pass, exposed to the menu realm in the same "one function per job, GMod naming"
//   style: the Lua side calls HL2SB_MenuLayout( panel ) after opening a dialog.
//
// CONTRACT
//
//   HL2SB_MenuLayout( panel )        - run the layout pass over that panel and its whole
//                                      subtree, immediately (recursively, depth first, so
//                                      children of a resized child are laid out too).
//                                      Returns the number of panels visited.
//   HL2SB_MenuDumpLayout( panel )    - log every panel in the subtree (class, pos, size,
//                                      visible, layout-invalid) - the diagnostic that pins
//                                      a layout problem without guessing.
//
//=============================================================================//

#include "cbase.h"
#ifdef LUA_SDK
#include "luamanager.h"
#include "luasrclib.h"
// luaL_checkpanel / lua_Panel
#include "vgui_controls/lPanel.h"
#include "vgui_controls/Panel.h"
#include "vgui/IInput.h"
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: depth-first walk of a panel tree.
//
//   For every panel it:
//     1. re-applies the geometry the panel's own Lua side recorded (x/y/w/h - the fields
//        lPanel.cpp keeps in sync when Lua calls SetPos/SetSize) through the NATIVE
//        setters, so a position that never reached the C++ panel is applied here;
//     2. runs the layout pass (InvalidateLayout( true ) + PerformLayout()).
//
//   Step 1 is what actually moves the menu controls: the dbg dump showed the Lua-side
//   SetSize landing but SetPos not (pos=(0,0) with the right sizes), i.e. the geometry Lua
//   handed the panels never made it into vgui in this realm.  Reading it back out of the
//   panel and setting it natively cannot be defeated by whatever the Lua path did.
//-----------------------------------------------------------------------------
static int HL2SB_LayoutSubtree( lua_State *L, vgui::Panel *pPanel, int nVisited, int nDepth )
{
	if ( pPanel == NULL || nDepth > 32 )
		return nVisited;

	++nVisited;

	// 1. native geometry from the panel's Lua handle.
	lua_pushpanel( L, pPanel );

	if ( lua_type( L, -1 ) == LUA_TUSERDATA || lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "x" ); int nX = (int)lua_tointeger( L, -1 ); lua_pop( L, 1 );
		lua_getfield( L, -1, "y" ); int nY = (int)lua_tointeger( L, -1 ); lua_pop( L, 1 );
		lua_getfield( L, -1, "w" ); int nW = (int)lua_tointeger( L, -1 ); lua_pop( L, 1 );
		lua_getfield( L, -1, "h" ); int nH = (int)lua_tointeger( L, -1 ); lua_pop( L, 1 );

		if ( nW > 0 && nH > 0 )
			pPanel->SetSize( nW, nH );

		pPanel->SetPos( nX, nY );
	}

	lua_pop( L, 1 );

	// 2. the layout pass (the scripted classes hand this to the Lua PerformLayout).
	pPanel->InvalidateLayout( true );
	pPanel->PerformLayout();

	int nChildren = pPanel->GetChildCount();

	for ( int i = 0; i < nChildren; ++i )
		nVisited = HL2SB_LayoutSubtree( L, pPanel->GetChild( i ), nVisited, nDepth + 1 );

	return nVisited;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_MenuLayout( panel ) -> number of panels laid out.
//-----------------------------------------------------------------------------
static int lua_HL2SB_MenuLayout( lua_State *L )
{
	if ( lua_isnoneornil( L, 1 ) )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}

	vgui::Panel *pPanel = luaL_checkpanel( L, 1 );

	if ( pPanel == NULL )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}

	lua_pushinteger( L, HL2SB_LayoutSubtree( L, pPanel, 0, 0 ) );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_MenuDumpLayout( panel ) - one line per panel in the subtree.
//-----------------------------------------------------------------------------
static int lua_HL2SB_MenuDumpLayout( lua_State *L, vgui::Panel *pPanel, int nDepth )
{
	if ( pPanel == NULL || nDepth > 32 )
		return 0;

	int x, y;
	pPanel->GetPos( x, y );

	Msg( "[HL2SB] menu layout: %*s%s '%s' pos=(%d,%d) size=%dx%d visible=%d layoutInvalid=%d\n",
		nDepth * 2, "",
		pPanel->GetClassName(),
		pPanel->GetName(),
		x, y, pPanel->GetWide(), pPanel->GetTall(),
		pPanel->IsVisible() ? 1 : 0,
		pPanel->IsLayoutInvalid() ? 1 : 0 );

	int nCount = 1;

	for ( int i = 0; i < pPanel->GetChildCount(); ++i )
		nCount += lua_HL2SB_MenuDumpLayout( L, pPanel->GetChild( i ), nDepth + 1 );

	return nCount;
}

static int lua_HL2SB_MenuDumpLayout_Entry( lua_State *L )
{
	vgui::Panel *pPanel = luaL_checkpanel( L, 1 );

	lua_pushinteger( L, lua_HL2SB_MenuDumpLayout( L, pPanel, 0 ) );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: the menu realm's own library table.
//-----------------------------------------------------------------------------
static const luaL_Reg gameui_menu_funcs[] =
{
	{ "HL2SB_MenuLayout",     lua_HL2SB_MenuLayout },
	{ "HL2SB_MenuDumpLayout", lua_HL2SB_MenuDumpLayout_Entry },
	{ NULL, NULL }
};

LUALIB_API int luaopen_gameui_menu (lua_State *L)
{
	// Publish as plain globals: the menu scripts are plain Lua files, not modules, and
	// GMod's own menu helpers are globals too.
	luaL_register( L, NULL, gameui_menu_funcs );
	return 1;
}

#endif // LUA_SDK
