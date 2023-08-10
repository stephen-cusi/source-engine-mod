//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Glock - hand gun
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "weapon_hl2mpbase.h"
//#include "basecombatcharacter.h"
//#include "AI_BaseNPC.h"
#ifdef CLIENT_DLL
#include "c_baseplayer.h"
#else
#include "player.h"
#endif
#include "gamerules.h"
#include "in_buttons.h"
#ifdef CLIENT_DLL
#else
#include "soundent.h"
#include "game.h"
#endif
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"

#ifdef CLIENT_DLL
#define CHL1MPWeaponGlock C_HL1MPWeaponGlock
#endif

class CHL1MPWeaponGlock : public CWeaponHL2MPBase
{
	DECLARE_CLASS( CHL1MPWeaponGlock, CWeaponHL2MPBase);

	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();

public:

	CHL1MPWeaponGlock(void);

	void	Precache( void );
	void	PrimaryAttack( void );
	void	SecondaryAttack( void );
	bool	Reload( void );
	void	WeaponIdle( void );
	void	DryFire( void );

private:
	void	GlockFire( float flSpread , float flCycleTime, bool fUseAutoAim );
};

IMPLEMENT_NETWORKCLASS_ALIASED( HL1MPWeaponGlock, DT_HL1MPWeaponGlock );

BEGIN_NETWORK_TABLE( CHL1MPWeaponGlock, DT_HL1MPWeaponGlock )
END_NETWORK_TABLE()

LINK_ENTITY_TO_CLASS( weapon_hl1mp_glock, CHL1MPWeaponGlock );

BEGIN_PREDICTION_DATA( CHL1MPWeaponGlock )
END_PREDICTION_DATA()

PRECACHE_WEAPON_REGISTER( weapon_hl1mp_glock );


CHL1MPWeaponGlock::CHL1MPWeaponGlock( void )
{
	m_bReloadsSingly	= false;
	m_bFiresUnderwater	= true;
}


void CHL1MPWeaponGlock::Precache( void )
{

	BaseClass::Precache();
}


void CHL1MPWeaponGlock::DryFire( void )
{
	WeaponSound( EMPTY );
	SendWeaponAnim( ACT_VM_DRYFIRE );
		
	m_flNextPrimaryAttack = gpGlobals->curtime + 0.2;
}


void CHL1MPWeaponGlock::PrimaryAttack( void )
{
	GlockFire( 0.01, 0.3, TRUE );
}


void CHL1MPWeaponGlock::SecondaryAttack( void )
{
	GlockFire( 0.1, 0.2, FALSE );
}


void CHL1MPWeaponGlock::GlockFire( float flSpread , float flCycleTime, bool fUseAutoAim )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( !pPlayer )
	{
		return;
	}

	if ( m_iClip1 <= 0 )
	{
		if ( !m_bFireOnEmpty )
		{
			Reload();
		}
		else
		{
			DryFire();
		}

		return;
	}

	WeaponSound( SINGLE );

	pPlayer->DoMuzzleFlash();

	m_iClip1--;

	if ( m_iClip1 == 0 )
		SendWeaponAnim( ACT_GLOCK_SHOOTEMPTY );
	else
		SendWeaponAnim( ACT_VM_PRIMARYATTACK );

	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	m_flNextPrimaryAttack	= gpGlobals->curtime + flCycleTime;
	m_flNextSecondaryAttack	= gpGlobals->curtime + flCycleTime;

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAiming;
	
	if ( fUseAutoAim )
	{
		vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_10DEGREES );	
	}
	else
	{
		vecAiming = pPlayer->GetAutoaimVector( 0 );	
	}

//	pPlayer->FireBullets( 1, vecSrc, vecAiming, Vector( flSpread, flSpread, flSpread ), MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0 );
	FireBulletsInfo_t info( 1, vecSrc, vecAiming, Vector( flSpread, flSpread, flSpread ), MAX_TRACE_LENGTH, m_iPrimaryAmmoType );
	info.m_pAttacker = pPlayer;
	pPlayer->FireBullets( info );

	EjectShell( pPlayer, 0 );

	pPlayer->ViewPunch( QAngle( -2, 0, 0 ) );
#if !defined(CLIENT_DLL)
	pPlayer->SetMuzzleFlashTime( gpGlobals->curtime + 0.5 );

	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), 400, 0.2 );
#endif

	if ( !m_iClip1 && pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		// HEV suit - indicate out of ammo condition
		pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
	}

	SetWeaponIdleTime( gpGlobals->curtime + random->RandomFloat( 10, 15 ) );
}


bool CHL1MPWeaponGlock::Reload( void )
{
	bool iResult;

	if ( m_iClip1 == 0 )
		iResult = DefaultReload( GetMaxClip1(), GetMaxClip2(), ACT_GLOCK_SHOOT_RELOAD );
	else
		iResult = DefaultReload( GetMaxClip1(), GetMaxClip2(), ACT_VM_RELOAD );

	return iResult;
}



void CHL1MPWeaponGlock::WeaponIdle( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( pPlayer )
	{
		pPlayer->GetAutoaimVector( AUTOAIM_10DEGREES );
	}

	// only idle if the slid isn't back
	if ( m_iClip1 != 0 )
	{
		BaseClass::WeaponIdle();
	}
}
