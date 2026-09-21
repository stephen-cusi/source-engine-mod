//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Client DLL VGUI2 Viewport
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

// our definition
#include "scriptedhudviewport.h"

// lua hooks
#ifdef LUA_SDK
#include "luamanager.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

//================================================================
CScriptedHudViewport::CScriptedHudViewport() : vgui::EditablePanel( NULL, "CScriptedHudViewport")
{
	SetKeyBoardInputEnabled( false );
	SetMouseInputEnabled( false );
	SetPaintBackgroundEnabled( false );

	SetProportional( true );
}

//-----------------------------------------------------------------------------
// Purpose: Sets the parent for each panel to use
//-----------------------------------------------------------------------------
void CScriptedHudViewport::SetParent(vgui::VPANEL parent)
{
	EditablePanel::SetParent( parent );
	// force ourselves to be proportional - when we set our parent above, if our new
	// parent happened to be non-proportional (such as the vgui root panel), we got
	// slammed to be nonproportional
	EditablePanel::SetProportional( true );
}

void CScriptedHudViewport::Paint()
{
	// HL2SB (2026-09-21): the engine timer library (game/shared/lua/ltimer.cpp)
	// pumps every frame off this paint -- the client realm's per-frame point.
	// Local prototype on purpose: a luasrclib.h touch would force a full-tree
	// rebuild (waf has no header dependency propagation).
	LUA_API void HL2SB_TimerTick( void );
	if ( L != NULL )
		HL2SB_TimerTick();

	// HL2SB (2026-09-21): drip-feed the rate-limited client->server net
	// transport (game/shared/lua/lnet.cpp) -- same per-frame point.
	LUA_API void HL2SB_NetCmdPump( void );
	HL2SB_NetCmdPump();

	BEGIN_LUA_CALL_HOOK( "HudViewportPaint" );
	END_LUA_CALL_HOOK( 0, 0 );

	// HL2SB: GMod's name for the same event.
	//
	// GMod addons write hook.Add( "HUDPaint", ... ) to draw a HUD (GMod passes no
	// arguments and ignores the return value), and nothing in this tree ever fired that
	// name: the only registration, lua/includes/extensions/client/player.lua:76, was dead.
	// Every ported GMod HUD was silently inert - cod_c4's C4 pick-up prompt among them
	// (addons/cod_c4/lua/entities/cod-c4/cl_init.lua:39), which is why the prompt never
	// appeared even once its input.LookupBinding call was fixed.
	BEGIN_LUA_CALL_HOOK( "HUDPaint" );
	END_LUA_CALL_HOOK( 0, 0 );
}
