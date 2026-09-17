// hl2sb_player_model_manager.cpp
// Server-side player model manager for HL2SB

#include "cbase.h"
#include "hl2sb_player_model_manager.h"
#include "hl2mp_player.h"
#include "hl2mp_gamerules.h"
#include "team.h"
// HL2SB: CBaseCombatWeapon, for the weapon color (cl_weaponcolor).
#include "basecombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// GMod-style model switching: when ON (default), a requested player model
// change only takes effect after the player dies/respawns. When OFF, it applies
// immediately (the old instant behavior).
ConVar hl2sb_model_respawn_only( "hl2sb_model_respawn_only", "1", FCVAR_NOTIFY,
	"When enabled, player model changes (hl2sb_setmodel / cl_playermodel) are applied only on respawn (GMod style); when disabled, they apply immediately" );

//-----------------------------------------------------------------------------
// Purpose: Initialize model manager
//-----------------------------------------------------------------------------
void HL2SB_ModelManager_Init( void )
{
	// No auto-scan - configs are loaded in gamerules Precache
}

//-----------------------------------------------------------------------------
// Purpose: Get default model for team
//-----------------------------------------------------------------------------
const char *HL2SB_GetDefaultModelForTeam( int iTeam )
{
	if ( iTeam == TEAM_COMBINE )
		return "models/player/combine_soldier.mdl";
	else if ( iTeam == TEAM_REBELS )
		return "models/player/group01/male_01.mdl";

	return "models/player/combine_soldier.mdl";
}

//-----------------------------------------------------------------------------
// Purpose: Read a "r g b" client convar (GMod writes vectors as strings, e.g.
//          RunConsoleCommand( "cl_playercolor", tostring( plycol:GetVector() ) )).
//          Anything unparsable stays white, which is "no tint".
//-----------------------------------------------------------------------------
static void HL2SB_ParseColor( const char *pszValue, float &r, float &g, float &b )
{
	r = g = b = 1.0f;

	if ( pszValue && pszValue[0] )
		sscanf( pszValue, "%f %f %f", &r, &g, &b );

	// A hand-edited convar must not push out-of-range values into m_clrRender.
	r = clamp( r, 0.0f, 1.0f );
	g = clamp( g, 0.0f, 1.0f );
	b = clamp( b, 0.0f, 1.0f );
}

//-----------------------------------------------------------------------------
// Purpose: Apply the bodygroups / skin / colors the client's player editor asked for.
//          GMod keeps them in cl_playerbodygroups / cl_playerskin / cl_playercolor /
//          cl_weaponcolor (garrysmod/gamemodes/sandbox/gamemode/editor_player.lua), and
//          they reach the server exactly like cl_playermodel does.
//-----------------------------------------------------------------------------
static void HL2SB_ApplyClientAppearance( CBasePlayer *pPlayer )
{
	if ( !pPlayer )
		return;

	const int iClient = pPlayer->entindex();

	// Bodygroups: one decimal value per bodygroup, space separated.  GMod's editor
	// writes a value for every group; an empty string means "the client never set
	// them", so the model's own defaults are left alone.
	const char *pszBodygroups = engine->GetClientConVarValue( iClient, "cl_playerbodygroups" );

	if ( pszBodygroups && pszBodygroups[0] )
	{
		const char *p = pszBodygroups;

		for ( int iGroup = 0; iGroup < 32 && *p; iGroup++ )
		{
			pPlayer->SetBodygroup( iGroup, atoi( p ) );

			while ( *p && *p != ' ' ) p++;		// the value
			while ( *p == ' ' ) p++;			// the separator
		}
	}

	const char *pszSkin = engine->GetClientConVarValue( iClient, "cl_playerskin" );

	if ( pszSkin && pszSkin[0] )
	{
		// m_nSkin is the networked skin (game/server/baseanimating.h:349).  There is no
		// SetSkin() accessor in this tree - the Lua binding writes the member too
		// (game/shared/lua/lbaseanimating_shared.cpp:220) - and assigning a CNetworkVar
		// is what marks it changed for networking.
		pPlayer->m_nSkin = atoi( pszSkin );
	}

	// Colors go through m_clrRender, which is networked: the renderer modulates the model
	// with it (CBaseEntity::GetColorModulation -> render.SetColorModulation), the same
	// channel GMod's cl_playercolor ends up in.  "1 1 1" leaves the model unchanged.
	float r, g, b;

	HL2SB_ParseColor( engine->GetClientConVarValue( iClient, "cl_playercolor" ), r, g, b );
	pPlayer->SetRenderColor( (byte)( r * 255.0f ), (byte)( g * 255.0f ), (byte)( b * 255.0f ) );

	// The weapon in the world takes the weapon color.  WARNING: GMod also tints the
	// first-person viewmodel; that is a client-side entity the server cannot reach, so
	// only the model other players see changes here (recorded in AGENTS.md).
	CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();

	if ( pWeapon )
	{
		HL2SB_ParseColor( engine->GetClientConVarValue( iClient, "cl_weaponcolor" ), r, g, b );
		pWeapon->SetRenderColor( (byte)( r * 255.0f ), (byte)( g * 255.0f ), (byte)( b * 255.0f ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Apply player model on spawn
//-----------------------------------------------------------------------------
void HL2SB_ModelManager_PlayerSpawn( CBasePlayer *pPlayer )
{
	if ( !pPlayer )
		return;

	CHL2MP_Player *pHL2Player = dynamic_cast<CHL2MP_Player *>(pPlayer);
	if ( !pHL2Player )
		return;

	// Get requested model from client
	const char *pszRequested = engine->GetClientConVarValue( 
		pPlayer->entindex(), "cl_playermodel" );

	const char *pszFallback = HL2SB_GetDefaultModelForTeam( pPlayer->GetTeamNumber() );

	HL2SB_ApplyPlayerModel( pPlayer, pszRequested, pszFallback );
	HL2SB_ApplyClientAppearance( pPlayer );
}

//-----------------------------------------------------------------------------
// Purpose: Handle client settings change
//-----------------------------------------------------------------------------
void HL2SB_ModelManager_ClientSettingsChanged( CBasePlayer *pPlayer )
{
	if ( !pPlayer )
		return;

	// Only apply if the player has a valid team
	if ( pPlayer->GetTeamNumber() <= 0 )
		return;

	const char *pszRequested = engine->GetClientConVarValue( 
		pPlayer->entindex(), "cl_playermodel" );

	const char *pszFallback = HL2SB_GetDefaultModelForTeam( pPlayer->GetTeamNumber() );

	HL2SB_ApplyPlayerModel( pPlayer, pszRequested, pszFallback );
	HL2SB_ApplyClientAppearance( pPlayer );
}
