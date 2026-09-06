// hl2sb_player_model_manager.cpp
// Server-side player model manager for HL2SB

#include "cbase.h"
#include "hl2sb_player_model_manager.h"
#include "hl2mp_player.h"
#include "hl2mp_gamerules.h"
#include "team.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

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
}
