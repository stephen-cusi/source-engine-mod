//=========== (C) Copyright 1999 Valve, L.L.C. All rights reserved. ===========
//
// Purpose:		Projectile shot from the AR2 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================

#ifndef	WEAPONOICW_H
#define	WEAPONOICW_H

#include "basegrenade_shared.h"
#include "basehlcombatweapon.h"

class CGrenadeAR2;

class CWeaponOICW : public CHLMachineGun
{
public:
	DECLARE_CLASS( CWeaponOICW, CHLMachineGun );

	CWeaponOICW();

	DECLARE_SERVERCLASS();

	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	bool	Reload( void );
	float	GetFireRate( void );
	void	ItemPostFrame( void );
	void	Precache( void );
	void	PrimaryAttack( void );
	void 	SecondaryAttack( void );
	void	AddViewKick( void );
	void	Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );

	int		GetMinBurst() { return 4; }
	int		GetMaxBurst() { return 7; }

	int		CapabilitiesGet( void ) { return bits_CAP_WEAPON_RANGE_ATTACK1; }

	virtual Activity GetPrimaryAttackActivity( void );

	virtual const Vector& GetBulletSpread( void )
	{
		static Vector cone;
		
		if( GetOwner() && GetOwner()->IsPlayer() )
		{
			cone = ( m_bZoomed ) ? VECTOR_CONE_1DEGREES : VECTOR_CONE_3DEGREES;
		}
		else
		{
			cone = VECTOR_CONE_8DEGREES;
		}
		
		return cone;
	}
	
	virtual bool	Deploy( void );
	virtual void	Drop( const Vector &velocity );

protected:

	void			Zoom( void );

	int				m_nShotsFired;
	bool			m_bZoomed;
	bool			m_bGrenade;

	static const char *pShootSounds[];

	DECLARE_ACTTABLE();
	DECLARE_DATADESC();
};


#endif	//WEAPONAR2_H