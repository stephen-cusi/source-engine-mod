//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Projectile shot by bullsquid 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#ifndef	HL1MPGrenadeSpit_H
#define	HL1MPGrenadeSpit_H

#include "hl1_basegrenade.h"

enum SpitSize_e
{
	SPIT_SMALL,
	SPIT_MEDIUM,
	SPIT_LARGE,
};

#define SPIT_GRAVITY 0.9

class CHL1MPGrenadeSpit : public CHL1BaseGrenade
{
public:
	DECLARE_CLASS( CHL1MPGrenadeSpit, CHL1BaseGrenade );

	void		Spawn( void );
	void		Precache( void );
	void		SpitThink( void );
	void 		GrenadeSpitTouch( CBaseEntity *pOther );
	void		Event_Killed( const CTakeDamageInfo &info );
	void		SetSpitSize(int nSize);

	int			m_nSquidSpitSprite;
	float		m_fSpitDeathTime;		// If non-zero won't detonate

	void EXPORT				Detonate(void);
	CHL1MPGrenadeSpit(void);

	DECLARE_DATADESC();
};

#endif	//HL1MPGrenadeSpit_H
