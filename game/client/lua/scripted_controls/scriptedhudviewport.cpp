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
#include "lbaseentity_shared.h"	// HL2SB GMod compat: lua_pushplayer
#include "lbasecombatweapon_shared.h"	// HL2SB GMod compat: lua_pushweapon
#include "c_baseplayer.h"	// HL2SB GMod compat: SWEP:DrawHUD / SWEP:DrawHUDBackground
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
	// HL2SB (2026-09-25): the timer + client->server net pumps MOVED to
	// ClientModeShared::Update (clientmode_shared.cpp).  This Paint() only
	// runs when a scripted HUD viewport control exists, so timers and queued
	// net commands silently stalled on any client UI without one.

	BEGIN_LUA_CALL_HOOK( "HudViewportPaint" );
	END_LUA_CALL_HOOK( 0, 0 );

	// HL2SB GMod compat (2026-09-23): SWEP:DrawHUDBackground() runs in the
	// GM:HUDPaintBackground phase -- the wiki says it is called BEFORE
	// WEAPON:DrawHUD -- and SWEP:DrawHUD() runs in the GM:HUDPaint phase.
	// GMod order: HUDPaintBackground -> HUDPaint, so mirror that here.
#if defined( LUA_SDK )
	{
		C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();
		if ( pLocalPlayer != NULL && pLocalPlayer->IsAlive() )
		{
			CBaseCombatWeapon *pWep = pLocalPlayer->GetActiveWeapon();
			if ( pWep != NULL && pWep->IsScripted() )
			{
				BEGIN_LUA_CALL_WEAPON_HOOK( "DrawHUDBackground", pWep );
				END_LUA_CALL_WEAPON_HOOK( 0, 0 );
			}
		}
	}
#endif

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

#if defined( LUA_SDK )
	{
		C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();
		if ( pLocalPlayer != NULL && pLocalPlayer->IsAlive() )
		{
			CBaseCombatWeapon *pWep = pLocalPlayer->GetActiveWeapon();
			if ( pWep != NULL && pWep->IsScripted() )
			{
				BEGIN_LUA_CALL_WEAPON_HOOK( "DrawHUD", pWep );
				END_LUA_CALL_WEAPON_HOOK( 0, 0 );
			}
		}
	}
#endif
}
