
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "in_buttons.h"

#ifdef CLIENT_DLL
	#include "c_hl2mp_player.h"
#else
	#include "hl2mp_player.h"
#endif

#include "weapon_hl2mpbasehlmpcombatweapon.h"

#ifdef CLIENT_DLL
#define CWeapon357 C_Weapon357
#endif

//-----------------------------------------------------------------------------
// CWeapon357
//-----------------------------------------------------------------------------

class CWeapon357 : public CBaseHL2MPCombatWeapon
{
	DECLARE_CLASS( CWeapon357, CBaseHL2MPCombatWeapon );
public:

	CWeapon357( void );

	void	PrimaryAttack( void );
	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();

#ifndef CLIENT_DLL
	DECLARE_ACTTABLE();
#endif

private:
	
	CWeapon357( const CWeapon357 & );
};

IMPLEMENT_NETWORKCLASS_ALIASED( Weapon357, DT_Weapon357 )

BEGIN_NETWORK_TABLE( CWeapon357, DT_Weapon357 )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CWeapon357 )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_357, CWeapon357 );
PRECACHE_WEAPON_REGISTER( weapon_357 );


#ifndef CLIENT_DLL
acttable_t CWeapon357::m_acttable[] = 
{
	// ------------------------------------------------------------------
	// GMod player layer (ACT_MP_*). GMod's hold type ("aim layer") for the
	// .357 is "revolver" (two handed pistol), not "pistol" - the string is
	// right next to weapon_357's netvars in GMod's own client/server DLLs.
	// So the GMod layer uses REVOLVER while the HL2MP layer below keeps
	// PISTOL: HL2MP's male_anims/female_anims.mdl only has the ten HL2MP
	// hold types, while models/m_anm.mdl has both.
	// ------------------------------------------------------------------
	{ ACT_MP_STAND_IDLE, ACT_HL2MP_IDLE_REVOLVER, false },
	{ ACT_MP_CROUCH_IDLE, ACT_HL2MP_IDLE_CROUCH_REVOLVER, false },
	{ ACT_MP_CROUCHWALK, ACT_HL2MP_WALK_CROUCH_REVOLVER, false },
	{ ACT_MP_WALK, ACT_HL2MP_WALK_REVOLVER, false },
	{ ACT_MP_RUN, ACT_HL2MP_RUN_REVOLVER, false },
	{ ACT_MP_SPRINT, ACT_HL2MP_RUN_FAST, false },
	{ ACT_MP_SWIM, ACT_HL2MP_SWIM_REVOLVER, false },
	{ ACT_MP_JUMP, ACT_HL2MP_JUMP_REVOLVER, false },
	{ ACT_MP_JUMP_START, ACT_HL2MP_JUMP_REVOLVER, false },
	{ ACT_MP_JUMP_LAND, ACT_HL2MP_JUMP_REVOLVER, false },
	{ ACT_MP_JUMP_FLOAT, ACT_HL2MP_JUMP_REVOLVER, false },
	{ ACT_MP_DOUBLEJUMP, ACT_HL2MP_JUMP_REVOLVER, false },
	{ ACT_MP_ATTACK_STAND_PRIMARYFIRE, ACT_HL2MP_GESTURE_RANGE_ATTACK_REVOLVER, false },
	{ ACT_MP_ATTACK_CROUCH_PRIMARYFIRE, ACT_HL2MP_GESTURE_RANGE_ATTACK_REVOLVER, false },
	{ ACT_MP_ATTACK_SWIM_PRIMARYFIRE, ACT_HL2MP_GESTURE_RANGE_ATTACK_REVOLVER, false },
	{ ACT_MP_RELOAD_STAND, ACT_HL2MP_GESTURE_RELOAD_REVOLVER, false },
	{ ACT_MP_RELOAD_CROUCH, ACT_HL2MP_GESTURE_RELOAD_REVOLVER, false },
	{ ACT_MP_RELOAD_SWIM, ACT_HL2MP_GESTURE_RELOAD_REVOLVER, false },

	// ------------------------------------------------------------------
	// GMod base states this table never had, kept on the HL2MP PISTOL
	// hold type so the weapon still animates on HL2MP animation models.
	// ------------------------------------------------------------------
	{ ACT_HL2MP_WALK, ACT_HL2MP_WALK_PISTOL, false },
	{ ACT_HL2MP_SWIM, ACT_HL2MP_SWIM_PISTOL, false },
	{ ACT_HL2MP_SWIM_IDLE, ACT_HL2MP_SWIM_IDLE_PISTOL, false },
	{ ACT_HL2MP_SIT, ACT_HL2MP_SIT_PISTOL, false },
	{ ACT_HL2MP_RUN_FAST, ACT_HL2MP_RUN_PISTOL, false },
	{ ACT_HL2MP_RUN_CHARGING, ACT_HL2MP_RUN_PISTOL, false },
	{ ACT_HL2MP_RUN_PANICKED, ACT_HL2MP_RUN_PISTOL, false },
	{ ACT_HL2MP_RUN_PROTECTED, ACT_HL2MP_RUN_PISTOL, false },

	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_PISTOL,					false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_PISTOL,					false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_PISTOL,			false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_PISTOL,			false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PISTOL,	false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_PISTOL,		false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_PISTOL,					false },
	{ ACT_RANGE_ATTACK1,				ACT_RANGE_ATTACK_PISTOL,				false },
};



IMPLEMENT_ACTTABLE( CWeapon357 );

#endif

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CWeapon357::CWeapon357( void )
{
	m_bReloadsSingly	= false;
	m_bFiresUnderwater	= false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeapon357::PrimaryAttack( void )
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
			WeaponSound( EMPTY );
			m_flNextPrimaryAttack = 0.15;
		}

		return;
	}

	WeaponSound( SINGLE );
	pPlayer->DoMuzzleFlash();

	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	m_flNextPrimaryAttack = gpGlobals->curtime + 0.75;
	m_flNextSecondaryAttack = gpGlobals->curtime + 0.75;

	m_iClip1--;

	Vector vecSrc		= pPlayer->Weapon_ShootPosition();
	Vector vecAiming	= pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );	

	FireBulletsInfo_t info( 1, vecSrc, vecAiming, vec3_origin, MAX_TRACE_LENGTH, m_iPrimaryAmmoType );
	info.m_pAttacker = pPlayer;

	// Fire the bullets, and force the first shot to be perfectly accuracy
	pPlayer->FireBullets( info );

	//Disorient the player
	QAngle angles = pPlayer->GetLocalAngles();

	angles.x += random->RandomInt( -1, 1 );
	angles.y += random->RandomInt( -1, 1 );
	angles.z = 0;

#ifndef CLIENT_DLL
	pPlayer->SnapEyeAngles( angles );
#endif

	pPlayer->ViewPunch( QAngle( -8, random->RandomFloat( -2, 2 ), 0 ) );

	if ( !m_iClip1 && pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		// HEV suit - indicate out of ammo condition
		pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 ); 
	}
}
