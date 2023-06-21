//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Snark
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "weapon_hl2mpbase.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#include "soundent.h"
#include "game.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "hl1_npc_snark.h"
#include "beam_shared.h"


//-----------------------------------------------------------------------------
// CHL1MPWeaponSnark
//-----------------------------------------------------------------------------


#define SNARK_NEST_MODEL	"models/w_sqknest.mdl"


class CHL1MPWeaponSnark : public CWeaponHL2MPBase
{
	DECLARE_CLASS( CHL1MPWeaponSnark, CWeaponHL2MPBase );
public:

	CHL1MPWeaponSnark( void );

	void	Precache( void );
	void	PrimaryAttack( void );
	void	WeaponIdle( void );
	bool	Deploy( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

private:
	bool	m_bJustThrown;
};

LINK_ENTITY_TO_CLASS( weapon_hl1mp_snark, CHL1MPWeaponSnark );

PRECACHE_WEAPON_REGISTER( weapon_hl1mp_snark );

IMPLEMENT_SERVERCLASS_ST( CHL1MPWeaponSnark, DT_HL1MPWeaponSnark )
END_SEND_TABLE()

BEGIN_DATADESC( CHL1MPWeaponSnark )
	DEFINE_FIELD( m_bJustThrown, FIELD_BOOLEAN ),
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHL1MPWeaponSnark::CHL1MPWeaponSnark( void )
{
	m_bReloadsSingly	= false;
	m_bFiresUnderwater	= true;
	m_bJustThrown		= false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1MPWeaponSnark::Precache( void )
{
	BaseClass::Precache();

	PrecacheScriptSound( "WpnSnark.PrimaryAttack" );
	PrecacheScriptSound( "WpnSnark.Deploy" );

	UTIL_PrecacheOther("monster_snark");
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHL1MPWeaponSnark::PrimaryAttack( void )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( !pPlayer )
	{
		return;
	}

	if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
		return;

	Vector vecForward;
	pPlayer->EyeVectors( &vecForward );

	// find place to toss monster
	// Does this need to consider a crouched player?
	Vector vecStart	= pPlayer->WorldSpaceCenter() + (vecForward * 20);
	Vector vecEnd	= vecStart + (vecForward * 44);
	trace_t tr;
	UTIL_TraceLine( vecStart, vecEnd, MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );
	if ( tr.allsolid || tr.startsolid || tr.fraction <= 0.25 )
		return;

	// player "shoot" animation
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	CSnark *pSnark = (CSnark*)Create( "monster_snark", tr.endpos, pPlayer->EyeAngles(), GetOwner() );
	if ( pSnark )
	{
		pSnark->SetAbsVelocity( vecForward * 200 + pPlayer->GetAbsVelocity() );
	}

	// play hunt sound
	CPASAttenuationFilter filter( this );
	EmitSound( filter, entindex(), "WpnSnark.PrimaryAttack" );

	CSoundEnt::InsertSound( SOUND_DANGER, GetAbsOrigin(), 200, 0.2 );

	pPlayer->RemoveAmmo( 1, m_iPrimaryAmmoType );

	m_bJustThrown = true;

	m_flNextPrimaryAttack = gpGlobals->curtime + 0.3;
	SetWeaponIdleTime( gpGlobals->curtime + 1.0 );
}

void CHL1MPWeaponSnark::WeaponIdle( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( !pPlayer )
	{
		return;
	}

	if ( !HasWeaponIdleTimeElapsed() )
		return;

	if ( m_bJustThrown )
	{
		m_bJustThrown = false;

		if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
		{
			if ( !pPlayer->SwitchToNextBestWeapon( pPlayer->GetActiveWeapon() ) )
				Holster();
		}
		else
		{
			SendWeaponAnim( ACT_VM_DRAW );
			SetWeaponIdleTime( gpGlobals->curtime + random->RandomFloat( 10, 15 ) );
		}
	}
	else
	{
		if ( random->RandomFloat( 0, 1 ) <= 0.75 )
		{
			SendWeaponAnim( ACT_VM_IDLE );
		}
		else
		{
			SendWeaponAnim( ACT_VM_FIDGET );
		}
	}
}

bool CHL1MPWeaponSnark::Deploy( void )
{
	CPASAttenuationFilter filter( this );
	EmitSound( filter, entindex(), "WpnSnark.Deploy" );

	return BaseClass::Deploy();
}

bool CHL1MPWeaponSnark::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
	{
		return false;
	}

	if ( !BaseClass::Holster( pSwitchingTo ) )
	{
		return false;
	}

	if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		SetThink( &CHL1MPWeaponSnark::DestroyItem );
		SetNextThink( gpGlobals->curtime + 0.1 );
	}

	pPlayer->SetNextAttack( gpGlobals->curtime + 0.5 );

	return true;
}
