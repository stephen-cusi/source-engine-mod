//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#ifndef WEAPON_NH_HATCHET_H
#define WEAPON_NH_HATCHET_H

#include "basebludgeonweapon.h"

#if defined( _WIN32 )
#pragma once
#endif

#define	HATCHET_RANGE	85.0f
#define	HATCHET_REFIRE	0.55f

//-----------------------------------------------------------------------------
// CWeaponNHHatchet
//-----------------------------------------------------------------------------

class CWeaponNHHatchet : public CBaseHLBludgeonWeapon
{
public:
	DECLARE_CLASS( CWeaponNHHatchet, CBaseHLBludgeonWeapon );

	DECLARE_SERVERCLASS();
	DECLARE_ACTTABLE();

	CWeaponNHHatchet();

	float		GetRange( void )		{	return	HATCHET_RANGE;	}
	float		GetFireRate( void )		{	return	HATCHET_REFIRE;	}

	void		AddViewKick( void );
	float		GetDamageForActivity( Activity hitActivity );

	bool CanHolster() { return !m_bIsSwinging; }
	void PrimaryAttack();
	void SecondaryAttack();
	void ItemPostFrame();

	virtual void EmitImpactSound();

private:
	bool m_bIsSwinging; //are we in a secondary attack swing?
	float m_flSwingTime; //when to swing
	int m_iSwingType; //0,1 = pri, 2 = sec
};

#endif // WEAPON_NH_HATCHET_H
