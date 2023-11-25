//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: NH Hatchet
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "player.h"
#include "gamerules.h"
#include "ammodef.h"
#include "mathlib/mathlib.h"
#include "in_buttons.h"
#include "soundent.h"
#include "basebludgeonweapon.h"
#include "vstdlib/random.h"
#include "npcevent.h"
#include "ai_basenpc.h"
#include "weapon_nh_hatchet.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar    sk_plr_dmg_hatchet		( "sk_plr_dmg_hatchet","0");
ConVar    sk_npc_dmg_hatchet		( "sk_npc_dmg_hatchet","0");
ConVar sk_weapon_hatchet_primary_delay("sk_weapon_hatchet_primary_delay","0.2");
ConVar sk_weapon_hatchet_secondary_delay("sk_weapon_hatchet_secondary_delay","0.95");
ConVar sk_weapon_hatchet_secondary_mult("sk_weapon_hatchet_secondary_mult", "3.0");

//-----------------------------------------------------------------------------
// CWeaponNHHatchet
//-----------------------------------------------------------------------------
IMPLEMENT_SERVERCLASS_ST(CWeaponNHHatchet, DT_WeaponNHHatchet)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_nh_hatchet, CWeaponNHHatchet );
PRECACHE_WEAPON_REGISTER( weapon_nh_hatchet );

acttable_t CWeaponNHHatchet::m_acttable[] = 
{
	{ ACT_MELEE_ATTACK1,	ACT_MELEE_ATTACK_SWING, true },
	{ ACT_IDLE,				ACT_IDLE_ANGRY_MELEE,	false },
	{ ACT_IDLE_ANGRY,		ACT_IDLE_ANGRY_MELEE,	false },
};

IMPLEMENT_ACTTABLE(CWeaponNHHatchet);

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CWeaponNHHatchet::CWeaponNHHatchet( void )
{
	m_bIsSwinging = false;
}

//-----------------------------------------------------------------------------
// Purpose: Get the damage amount for the animation we're doing
// Input  : hitActivity - currently played activity
// Output : Damage amount
//-----------------------------------------------------------------------------
float CWeaponNHHatchet::GetDamageForActivity( Activity hitActivity )
{
	float dmg;
	if ( ( GetOwner() != NULL ) && ( GetOwner()->IsPlayer() ) )
		dmg = sk_plr_dmg_hatchet.GetFloat();
	else
		dmg = sk_npc_dmg_hatchet.GetFloat();

	if(hitActivity == ACT_VM_HITCENTER2 || hitActivity == ACT_VM_MISSCENTER2)
		dmg *= sk_weapon_hatchet_secondary_mult.GetFloat();

	return dmg;
}

//-----------------------------------------------------------------------------
// Purpose: Add in a view kick for this weapon
//-----------------------------------------------------------------------------
void CWeaponNHHatchet::AddViewKick( void )
{
	CBasePlayer *pPlayer  = ToBasePlayer( GetOwner() );
	
	if ( pPlayer == NULL )
		return;

	QAngle punchAng;

	punchAng.x = random->RandomFloat( 1.0f, 2.0f );
	punchAng.y = random->RandomFloat( -2.0f, -1.0f );
	punchAng.z = 0.0f;
	
	pPlayer->ViewPunch( punchAng ); 
}

void CWeaponNHHatchet::PrimaryAttack()
{
	CBasePlayer *pPlayer  = ToBasePlayer( GetOwner() );
	if ( pPlayer == NULL )
		return;

	if(m_bIsSwinging)
		return;
	m_flSwingTime = gpGlobals->curtime + sk_weapon_hatchet_primary_delay.GetFloat();
	m_bIsSwinging = true;

	//Quickly check for something infront and use that to decide which swing type to use
	trace_t traceHit;
	Vector traceStart = pPlayer->Weapon_ShootPosition();
	Vector forward = pPlayer->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT, GetRange());
	Vector traceEnd = traceStart + forward * GetRange();
	UTIL_TraceLine(
			traceStart,
			traceEnd,
			MASK_SHOT|CONTENTS_GRATE,
			pPlayer,
			COLLISION_GROUP_NONE,
			&traceHit);

	//If we find something, random anim, otherwise miss anim
	if (traceHit.fraction < 1.0)
		m_iSwingType = random->RandomInt(0,1);
	else
		m_iSwingType = 1;

	if(m_iSwingType == 0)
		SendWeaponAnim(ACT_VM_HITCENTER);
	else
		SendWeaponAnim(ACT_VM_MISSCENTER);
	WeaponSound(SINGLE);
}

void CWeaponNHHatchet::SecondaryAttack()
{
	if(m_bIsSwinging)
		return;
	m_flSwingTime = gpGlobals->curtime + sk_weapon_hatchet_secondary_delay.GetFloat();
	m_bIsSwinging = true;
	m_iSwingType = 2;
	SendWeaponAnim(ACT_VM_MISSCENTER2);
	WeaponSound(WPN_DOUBLE);
}

void CWeaponNHHatchet::ItemPostFrame()
{
	if(m_bIsSwinging && gpGlobals->curtime > m_flSwingTime)
	{
		m_bIsSwinging = false;
		if (m_iSwingType == 2)
			BaseClass::SecondaryAttack();
		else
			BaseClass::PrimaryAttack();
	}

	BaseClass::ItemPostFrame();
}

void CWeaponNHHatchet::EmitImpactSound()
{
	if(m_iSwingType == 0)
		WeaponSound(SPECIAL1);
	else if(m_iSwingType == 1)
		WeaponSound(SPECIAL2);
	else if(m_iSwingType == 2)
		WeaponSound(SPECIAL3);
}
