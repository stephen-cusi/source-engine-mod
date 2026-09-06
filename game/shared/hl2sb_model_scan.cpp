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
//-----------------------------------------------------------------------------
bool HL2SB_IsValidPlayerModel( const char *pszModelPath )
{
	if ( !pszModelPath || !pszModelPath[0] )
		return false;

	// Check if model config exists for this path
	return ( HL2SB_FindModelConfigByPath( pszModelPath ) != NULL );
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

		// Check if config exists for this model
		if ( HL2SB_FindModelConfigByPath( szNormalized ) )
		{
			pszFinalModel = szNormalized;
		}
	}

	pBasePlayer->SetModel( pszFinalModel );
	return true;
}
