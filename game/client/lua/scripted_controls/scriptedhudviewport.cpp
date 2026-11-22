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
#include "hl2sb_framestats.h"

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
	const bool bFrameStats = HL2SB_FrameStats_Enabled();
	double flHookStart = 0.0;
	if ( bFrameStats )
	{
		HL2SB_FrameStats_FramePulse();
		flHookStart = Plat_FloatTime();
	}

	BEGIN_LUA_CALL_HOOK( "HudViewportPaint" );
	END_LUA_CALL_HOOK( 0, 0 );
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	CBaseCombatWeapon *pWeapon = pPlayer && pPlayer->IsAlive() ? pPlayer->GetActiveWeapon() : NULL;
	if ( pWeapon && pWeapon->IsScripted() )
	{
		BEGIN_LUA_CALL_WEAPON_HOOK( "DrawHUDBackground", pWeapon );
		END_LUA_CALL_WEAPON_HOOK( 0, 0 );
	}

	// The gamemode and the active weapon each own a separate HUD callback.
	BEGIN_LUA_CALL_HOOK( "HUDPaint" );
	END_LUA_CALL_HOOK( 0, 0 );
	if ( bFrameStats )
		HL2SB_FrameStats_NoteLuaHook( "HUDPaint", Plat_FloatTime() - flHookStart );

	// HUDPaint can change the active weapon or remove its owner.
	pPlayer = C_BasePlayer::GetLocalPlayer();
	pWeapon = pPlayer && pPlayer->IsAlive() ? pPlayer->GetActiveWeapon() : NULL;
	if ( pWeapon && pWeapon->IsScripted() )
	{
		BEGIN_LUA_CALL_WEAPON_HOOK( "DrawHUD", pWeapon );
		END_LUA_CALL_WEAPON_HOOK( 0, 0 );
	}

	BEGIN_LUA_CALL_HOOK( "PostDrawHUD" );
	END_LUA_CALL_HOOK( 0, 0 );
}
