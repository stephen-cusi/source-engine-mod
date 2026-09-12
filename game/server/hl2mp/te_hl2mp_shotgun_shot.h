//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef TE_HL2MP_SHOTGUN_SHOT_H
#define TE_HL2MP_SHOTGUN_SHOT_H
#ifdef _WIN32
#pragma once
#endif


// HL2SB: pszTracerName is the tracer effect name the shooter's weapon asked for
// (GMod's bullet.TracerName).  It rides along with this TE because the client
// that receives it cannot always work the name out for itself -- see the
// comment in te_hl2mp_shotgun_shot.cpp.
void TE_HL2MPFireBullets( 
	int	iPlayerIndex,
	const Vector &vOrigin,
	const Vector &vDir,
	int	iAmmoID,
	int iSeed,
	int iShots,
	float flSpread, 
	bool bDoTracers,
	bool bDoImpacts,
	const char *pszTracerName = NULL );


#endif // TE_HL2MP_SHOTGUN_SHOT_H
