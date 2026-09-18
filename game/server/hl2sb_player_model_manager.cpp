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

	if ( pszSkin && pszSkin[0] )	{
		// m_nSkin is the networked skin (game/server/baseanimating.h:349).  There is no
		// SetSkin() accessor in this tree - the Lua binding writes the member too
		// (game/shared/lua/lbaseanimating_shared.cpp:220) - and assigning a CNetworkVar
		// is what marks it changed for networking.
		pPlayer->m_nSkin = atoi( pszSkin );
	}

	// Colors: NOT through m_clrRender any more (2026-09-17).
	//
	// m_clrRender reaches render->SetColorModulation(), and the studio renderer writes
	// that into $color2 for EVERY material of the model - i.e. the whole model tinted,
	// face and boots included, which is not what GMod does.
	//
	// GMod's player colour and weapon colour are applied by the VMTs' own proxies:
	//
	//     Proxies { PlayerColor       { resultVar $color2 ... } }
	//     Proxies { PlayerWeaponColor { resultVar $color2 ... } }
	//
	// (42 materials of the GMod playermodel pack declare PlayerColor), implemented in
	// game/client/c_viewmodel_attachment.cpp.  So the server leaves the render colour
	// alone - white means "no whole-model tint" - and only the bodygroups and the skin
	// are applied here.
	float r, g, b;

	(void)r; (void)g; (void)b;

	CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	(void)pWeapon;

	// HL2SB diagnostic (2026-09-17): "the clothing colour only ever applies once" needs
	// the raw userinfo values next to what was written.  One line per spawn/respawn.
	Msg( "[HL2SB] appearance: bodygroups='%s' skin='%s' color='%s' weapon='%s'\n",
		pszBodygroups ? pszBodygroups : "", pszSkin ? pszSkin : "",
		engine->GetClientConVarValue( iClient, "cl_playercolor" ),
		engine->GetClientConVarValue( iClient, "cl_weaponcolor" ) );
}

//-----------------------------------------------------------------------------
// Purpose: Apply the appearance (bodygroups / skin / colours) to a player.
//          Exported because CHL2MP_Player::SetPlayerModel() needs it: the two
//          manager entry points below are dead code (nothing in the tree calls
//          them), so before this the sliders of the player model selector only
//          ever changed the *preview* (2026-09-17).
//-----------------------------------------------------------------------------
void HL2SB_ModelManager_ApplyAppearance( CBasePlayer *pPlayer )
{
	HL2SB_ApplyClientAppearance( pPlayer );
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
