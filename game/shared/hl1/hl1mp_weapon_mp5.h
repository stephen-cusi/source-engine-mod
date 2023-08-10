//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Projectile shot from the MP5 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#ifndef	WEAPONMP5_H
#define	WEAPONMP5_H

#ifdef CLIENT_DLL
#else
#include "hl1_basegrenade.h"
#endif
#include "weapon_hl2mpbase.h"

#ifdef CLIENT_DLL
class CGrenadeMP5;
#else
#endif

#ifdef CLIENT_DLL
#define CHL1MPWeaponMP5 C_HL1MPWeaponMP5
#endif

class CHL1MPWeaponMP5 : public CWeaponHL2MPBase
{
	DECLARE_CLASS( CHL1MPWeaponMP5, CWeaponHL2MPBase );
public:

	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();

	CHL1MPWeaponMP5();

	void	Precache( void );
	void	PrimaryAttack( void );
	void	SecondaryAttack( void );
	void	DryFire( void );
	void	WeaponIdle( void );

//	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
};


#endif	//WEAPONMP5_H
