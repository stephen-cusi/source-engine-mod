//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "test_stressentities.h"
#include "vstdlib/random.h"
#include "world.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CStressEntityReg	*CStressEntityReg::s_pHead = NULL;


// CStressEntityReg::s_pHead in array form for convenient access.
CUtlVector<CStressEntityReg*> g_StressEntityRegs;

CUtlVector<EHANDLE> g_StressEntities;


CBaseEntity* MoveToRandomSpot( CBaseEntity *pEnt )
{
	if ( pEnt )
	{
		CBasePlayer *pLocalPlayer = UTIL_GetLocalPlayer();
		if ( pLocalPlayer )
		{			
			Vector vForward;
			pLocalPlayer->EyeVectors(&vForward );

			UTIL_SetOrigin( pEnt, GetRandomSpot() );
		}
	}

	return pEnt;
}


Vector GetRandomSpot()
{
	CWorld *pEnt = GetWorldEntity();
	if ( pEnt )
	{
		Vector vMin, vMax;
		pEnt->GetWorldBounds( vMin, vMax );
		return Vector(
			RandomFloat( vMin.x, vMax.x ),
			RandomFloat( vMin.y, vMax.y ),
			RandomFloat( vMin.z, vMax.z ) );
	}
	else
	{
		return Vector( 0, 0, 0 );
	}
}

