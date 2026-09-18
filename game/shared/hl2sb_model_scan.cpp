// hl2sb_model_scan.cpp
// HL2SB Model validation

#include "cbase.h"
#include "hl2sb_model_scan.h"
#include "hl2sb_model_config.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Validate player model path
//
// HL2SB: the cfg/playermodel/<name>.cfg table is no longer the gate.  Player models
// come from the GMod side now - player_manager.AddValidModel / AddValidHands reach
// HL2SB_AddRuntimeModelConfig (see lua/autorun/client/hl2sb_playermodels.lua, which
// scans models/player/ and registers what it finds) - and an addon that ships only a
// .mdl must work without any cfg at all.
//
// Accepted:
//   * a registered entry (cfg or Lua) - keeps the old behaviour and the hands lookup
//   * anything under models/player/ - the custom models an addon drops in place,
//     which the server precaches on spawn (CHL2MP_Player::SetPlayerModel).
// Everything else (a prop, an NPC, "none", ...) is still refused, so cl_playermodel
// cannot turn a player into a crate.
//-----------------------------------------------------------------------------
bool HL2SB_IsValidPlayerModel( const char *pszModelPath )
{
	if ( !pszModelPath || !pszModelPath[0] )
		return false;

	if ( HL2SB_FindModelConfigByPath( pszModelPath ) != NULL )
		return true;

	if ( Q_stristr( pszModelPath, "models/player/" ) != NULL )
		return true;

	// A path that is already loaded (the stock HL2MP models are precached before the
	// player exists) is fair game too.
	return ( modelinfo != NULL && modelinfo->GetModelIndex( pszModelPath ) != -1 );
}

//-----------------------------------------------------------------------------
// Purpose: Apply player model with validation
//-----------------------------------------------------------------------------
bool HL2SB_ApplyPlayerModel( void *pPlayer, const char *pszRequestedModel, const char *pszFallbackModel )
{
	CBasePlayer *pBasePlayer = (CBasePlayer *)pPlayer;
	if ( !pBasePlayer )
		return false;

	const char *pszFinalModel = pszFallbackModel;

	if ( pszRequestedModel && pszRequestedModel[0] )
	{
		// Normalize path
		char szNormalized[128];
		Q_strncpy( szNormalized, pszRequestedModel, sizeof(szNormalized) );

		if ( Q_strnicmp( szNormalized, "models/", 7 ) != 0 )
		{
			char szTemp[128];
			Q_snprintf( szTemp, sizeof(szTemp), "models/player/%s", szNormalized );
			Q_strncpy( szNormalized, szTemp, sizeof(szNormalized) );
		}

		if ( HL2SB_IsValidPlayerModel( szNormalized ) )
		{
			pszFinalModel = szNormalized;
		}
	}

	pBasePlayer->SetModel( pszFinalModel );
	return true;
}
