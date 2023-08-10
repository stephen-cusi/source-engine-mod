//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Satchel charge
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "weapon_hl2mpbase.h"
//#include "basecombatweapon.h"
//#include "basecombatcharacter.h"
#ifdef CLIENT_DLL
#include "c_baseplayer.h"
#else
#include "player.h"
#endif
//#include "AI_BaseNPC.h"
//#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#ifndef CLIENT_DLL
#include "soundent.h"
#include "game.h"
#endif
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "hl1mp_weapon_satchel.h"


//-----------------------------------------------------------------------------
// CHL1MPWeaponSatchel
//-----------------------------------------------------------------------------


#define SATCHEL_VIEW_MODEL			"models/v_satchel.mdl"
#define SATCHEL_WORLD_MODEL			"models/w_satchel.mdl"
#define SATCHELRADIO_VIEW_MODEL		"models/v_satchel_radio.mdl"
#define SATCHELRADIO_WORLD_MODEL	"models/w_satchel.mdl"	// this needs fixing if we do multiplayer

IMPLEMENT_NETWORKCLASS_ALIASED( HL1MPWeaponSatchel, DT_HL1MPWeaponSatchel );

BEGIN_NETWORK_TABLE( CHL1MPWeaponSatchel, DT_HL1MPWeaponSatchel )
#ifdef CLIENT_DLL
	RecvPropInt( RECVINFO( m_iRadioViewIndex ) ),
	RecvPropInt( RECVINFO( m_iRadioWorldIndex ) ),
	RecvPropInt( RECVINFO( m_iSatchelViewIndex ) ),
	RecvPropInt( RECVINFO( m_iSatchelWorldIndex ) ),
	RecvPropInt( RECVINFO( m_iChargeReady ) ),
#else
	SendPropInt( SENDINFO( m_iRadioViewIndex ) ),
	SendPropInt( SENDINFO( m_iRadioWorldIndex ) ),
	SendPropInt( SENDINFO( m_iSatchelViewIndex ) ),
	SendPropInt( SENDINFO( m_iSatchelWorldIndex ) ),
	SendPropInt( SENDINFO( m_iChargeReady ) ),
#endif
END_NETWORK_TABLE()

LINK_ENTITY_TO_CLASS( weapon_hl1mp_satchel, CHL1MPWeaponSatchel );

PRECACHE_WEAPON_REGISTER( weapon_hl1mp_satchel );

//IMPLEMENT_SERVERCLASS_ST( CHL1MPWeaponSatchel, DT_HL1MPWeaponSatchel )
//END_SEND_TABLE()


BEGIN_PREDICTION_DATA( CHL1MPWeaponSatchel )
#ifdef CLIENT_DLL
	DEFINE_PRED_FIELD( m_iRadioViewIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD( m_iRadioWorldIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD( m_iSatchelViewIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD( m_iSatchelWorldIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE | FTYPEDESC_MODELINDEX ),
	DEFINE_PRED_FIELD( m_iChargeReady, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
#endif
END_PREDICTION_DATA()


BEGIN_DATADESC( CHL1MPWeaponSatchel )
	DEFINE_FIELD( m_iChargeReady, FIELD_INTEGER ),

//	DEFINE_FIELD( m_iRadioViewIndex, FIELD_INTEGER ),
//	DEFINE_FIELD( m_iRadioWorldIndex, FIELD_INTEGER ),
//	DEFINE_FIELD( m_iSatchelViewIndex, FIELD_INTEGER ),
//	DEFINE_FIELD( m_iSatchelWorldIndex, FIELD_INTEGER ),
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHL1MPWeaponSatchel::CHL1MPWeaponSatchel( void )
{
	m_bReloadsSingly	= false;
	m_bFiresUnderwater	= true;
}

void CHL1MPWeaponSatchel::Equip( CBaseCombatCharacter *pOwner )
{
	BaseClass::Equip( pOwner );

	m_iChargeReady = 0;	// this satchel charge weapon now forgets that any satchels are deployed by it.
}

bool CHL1MPWeaponSatchel::HasAnyAmmo( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
	{
		return false;
	}

	if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) > 0 )
	{
		// player is carrying some satchels
		return true;
	}

	if ( HasChargeDeployed() )
	{
		// player isn't carrying any satchels, but has some out
		return true;
	}

	return BaseClass::HasAnyAmmo();
}

bool CHL1MPWeaponSatchel::CanDeploy( void )
{
	if ( HasAnyAmmo() )
	{
		return true;
	}
	else
	{
		return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1MPWeaponSatchel::Precache( void )
{
	m_iSatchelViewIndex		= PrecacheModel( SATCHEL_VIEW_MODEL );
	m_iSatchelWorldIndex	= PrecacheModel( SATCHEL_WORLD_MODEL );
	m_iRadioViewIndex		= PrecacheModel( SATCHELRADIO_VIEW_MODEL );
	m_iRadioWorldIndex		= PrecacheModel( SATCHELRADIO_WORLD_MODEL );

#ifndef CLIENT_DLL
	UTIL_PrecacheOther( "monster_satchel" );
#endif

	BaseClass::Precache();
}

void CHL1MPWeaponSatchel::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if (!pOwner)
	{
		return;
	}

	if ( (pOwner->m_nButtons & IN_ATTACK) && (m_flNextPrimaryAttack <= gpGlobals->curtime) )
	{
		// If the firing button was just pressed, reset the firing time
		if ( pOwner->m_afButtonPressed & IN_ATTACK )
		{
			 m_flNextPrimaryAttack = gpGlobals->curtime;
		}

		PrimaryAttack();
	}

	BaseClass::ItemPostFrame();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHL1MPWeaponSatchel::PrimaryAttack( void )
{
	switch ( m_iChargeReady )
	{
	case 0:
		{
			Throw();
		}
		break;
	case 1:
		{
			CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
			if ( !pPlayer )
			{
				return;
			}

			SendWeaponAnim( ACT_VM_PRIMARYATTACK );

#ifndef CLIENT_DLL
			CBaseEntity *pSatchel = NULL;

			while ( (pSatchel = gEntList.FindEntityByClassname( pSatchel, "monster_satchel" ) ) != NULL)
			{
				if ( pSatchel->GetOwnerEntity() == pPlayer )
				{
					pSatchel->Use( pPlayer, pPlayer, USE_ON, 0 );
				}
			}
#endif

			m_iChargeReady = 2;
			m_flNextPrimaryAttack = gpGlobals->curtime + 0.5;
			m_flNextSecondaryAttack = gpGlobals->curtime + 0.5;
			SetWeaponIdleTime( gpGlobals->curtime + 0.5 );
			break;
		}

	case 2:
		// we're reloading, don't allow fire
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHL1MPWeaponSatchel::SecondaryAttack( void )
{
	if ( m_iChargeReady != 2 )
	{
		Throw();
	}
}

void CHL1MPWeaponSatchel::Throw( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
	{
		return;
	}

	if ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) > 0 )
	{
		Vector vecForward;
		pPlayer->EyeVectors( &vecForward );

		Vector vecSrc	= pPlayer->WorldSpaceCenter();
		Vector vecThrow	= vecForward * 274 + pPlayer->GetAbsVelocity();

#ifndef CLIENT_DLL
		CBaseEntity *pSatchel = Create( "monster_satchel", vecSrc, QAngle( 0, 0, 90 ), pPlayer );
		if ( pSatchel )
		{
			pSatchel->SetAbsVelocity( vecThrow );
			QAngle angVel = pSatchel->GetLocalAngularVelocity();
			angVel.y = 400;
			pSatchel->SetLocalAngularVelocity( angVel );

			ActivateRadioModel();

			SendWeaponAnim( ACT_VM_DRAW );

			// player "shoot" animation
			pPlayer->SetAnimation( PLAYER_ATTACK1 );

			m_iChargeReady = 1;
			
			pPlayer->RemoveAmmo( 1, m_iPrimaryAmmoType );

		}
#endif

		m_flNextPrimaryAttack = gpGlobals->curtime + 1.0;
		m_flNextSecondaryAttack = gpGlobals->curtime + 0.5;
	}
}

void CHL1MPWeaponSatchel::WeaponIdle( void )
{
	if ( !HasWeaponIdleTimeElapsed() )
		return;

	switch( m_iChargeReady )
	{
		case 0:
		case 1:
			SendWeaponAnim( ACT_VM_FIDGET );
			break;
		case 2:
		{
			CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

			if ( pPlayer && (pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0) )
			{
				m_iChargeReady = 0;
				if ( !pPlayer->SwitchToNextBestWeapon( pPlayer->GetActiveWeapon() ) )
				{
					Holster();
				}

				return;
			}

			ActivateSatchelModel();

			SendWeaponAnim( ACT_VM_DRAW );

			m_flNextPrimaryAttack = gpGlobals->curtime + 0.5;
			m_flNextSecondaryAttack = gpGlobals->curtime + 0.5;
			m_iChargeReady = 0;
			break;
		}
	}

	SetWeaponIdleTime( gpGlobals->curtime + random->RandomFloat( 10, 15 ) );// how long till we do this again.
}

bool CHL1MPWeaponSatchel::Deploy( void )
{
	SetWeaponIdleTime( gpGlobals->curtime + random->RandomFloat( 10, 15 ) );

	if ( HasChargeDeployed() )
	{
		ActivateRadioModel();
	}
	else
	{
		ActivateSatchelModel();
	}
	
	bool bRet = BaseClass::Deploy();
	if ( bRet )
	{
		// 
		CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
		if ( pPlayer )
		{
			pPlayer->SetNextAttack( gpGlobals->curtime + 1.0 );
		}
	}

	return bRet;

}

bool CHL1MPWeaponSatchel::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	if ( !BaseClass::Holster( pSwitchingTo ) )
	{
		return false;
	}

	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( pPlayer )
	{
		pPlayer->SetNextAttack( gpGlobals->curtime + 0.5 );

		if ( (pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0) && !HasChargeDeployed() )
		{
#ifndef CLIENT_DLL
			SetThink( &CHL1MPWeaponSatchel::DestroyItem );
			SetNextThink( gpGlobals->curtime + 0.1 );
#endif
		}
	}

	return true;
}

void CHL1MPWeaponSatchel::ActivateSatchelModel( void )
{
	m_iViewModelIndex	= m_iSatchelViewIndex;
	m_iWorldModelIndex	= m_iSatchelWorldIndex;
	SetModel( GetViewModel() );
}

void CHL1MPWeaponSatchel::ActivateRadioModel( void )
{
	m_iViewModelIndex	= m_iRadioViewIndex;
	m_iWorldModelIndex	= m_iRadioWorldIndex;
	SetModel( GetViewModel() );
}

const char *CHL1MPWeaponSatchel::GetViewModel( int ) const
{
	if ( m_iViewModelIndex == m_iSatchelViewIndex )
	{
		return SATCHEL_VIEW_MODEL;
	}
	else
	{
		return SATCHELRADIO_VIEW_MODEL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CHL1MPWeaponSatchel::GetWorldModel( void ) const
{
	if ( m_iViewModelIndex == m_iSatchelViewIndex )
	{
		return SATCHEL_WORLD_MODEL;
	}
	else
	{
		return SATCHELRADIO_WORLD_MODEL;
	}
}

void CHL1MPWeaponSatchel::OnRestore( void )
{
	BaseClass::OnRestore();
	if ( HasChargeDeployed() )
	{
		ActivateRadioModel();
	}
	else
	{
		ActivateSatchelModel();
	}
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// CHL1MPSatchelCharge
//-----------------------------------------------------------------------------

#define SATCHEL_CHARGE_MODEL "models/w_satchel.mdl"


extern ConVar sk_plr_dmg_satchel;


BEGIN_DATADESC( CHL1MPSatchelCharge )
	DEFINE_FIELD( m_flNextBounceSoundTime, FIELD_TIME ),
	DEFINE_FIELD( m_bInAir, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vLastPosition, FIELD_POSITION_VECTOR ),

	// Function Pointers
	DEFINE_ENTITYFUNC( SatchelTouch ),
	DEFINE_THINKFUNC( SatchelThink ),
	DEFINE_USEFUNC( SatchelUse ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( monster_satchel, CHL1MPSatchelCharge );

//=========================================================
// Deactivate - do whatever it is we do to an orphaned 
// satchel when we don't want it in the world anymore.
//=========================================================
void CHL1MPSatchelCharge::Deactivate( void )
{
	AddSolidFlags( FSOLID_NOT_SOLID );
	UTIL_Remove( this );
}


void CHL1MPSatchelCharge::Spawn( void )
{
	Precache( );
	// motor
	SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_SLIDE );
	SetSolid( SOLID_BBOX ); 

	SetModel( SATCHEL_CHARGE_MODEL );

	UTIL_SetSize( this, Vector( -4, -4, 0), Vector(4, 4, 8) );

	SetTouch( &CHL1MPSatchelCharge::SatchelTouch );
	SetUse( &CHL1MPSatchelCharge::SatchelUse );
	SetThink( &CHL1MPSatchelCharge::SatchelThink );
	SetNextThink( gpGlobals->curtime + 0.1f );

	m_flDamage		= sk_plr_dmg_satchel.GetFloat();
	m_DmgRadius		= m_flDamage * 2.5;
	m_takedamage	= DAMAGE_NO;
	m_iHealth		= 1;

	SetGravity( UTIL_ScaleForGravity( 560 ) );	// slightly lower gravity
	SetFriction( 0.97 );	//used in SatchelTouch to slow us when sliding
	SetSequence( LookupSequence( "onback" ) );

	m_bInAir				= false;
	m_flNextBounceSoundTime	= 0;

	m_vLastPosition	= vec3_origin;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CHL1MPSatchelCharge::SatchelUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	SetThink( &CBaseGrenade::Detonate );
	SetNextThink( gpGlobals->curtime );
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CHL1MPSatchelCharge::SatchelTouch( CBaseEntity *pOther )
{
	Assert( pOther );
	if ( pOther->IsSolidFlagSet(FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS) || GetWaterLevel() > 0 )
		return;

	StudioFrameAdvance( );

	UpdateSlideSound();

	if ( m_bInAir )
	{
		BounceSound();
		m_bInAir = false;
	}

	// add a bit of static friction
	SetAbsVelocity( GetAbsVelocity() * GetFriction() );
	SetLocalAngularVelocity( GetLocalAngularVelocity() * GetFriction() );
}

void CHL1MPSatchelCharge::UpdateSlideSound( void )
{	
	// HACKHACK - On ground isn't always set, so look for ground underneath
	trace_t tr;
	UTIL_TraceLine( GetAbsOrigin(), GetAbsOrigin() - Vector(0,0,10), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

	if ( !(tr.fraction < 1.0) )
	{
		m_bInAir = true;
	}
}

void CHL1MPSatchelCharge::SatchelThink( void )
{
	UpdateSlideSound();

	StudioFrameAdvance( );
	SetNextThink( gpGlobals->curtime + 0.1f );

	if (!IsInWorld())
	{
		UTIL_Remove( this );
		return;
	}

	Vector vecNewVel = GetAbsVelocity();
	
	if ( GetWaterLevel() > 0 )
	{
		SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE );
		vecNewVel *= 0.8;
		SetLocalAngularVelocity( GetLocalAngularVelocity() * 0.9 );

		vecNewVel.z = 0;
		SetGravity( -0.2 );
	}
	else if ( GetWaterLevel() == 0 )
	{
		SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_SLIDE );

		SetGravity( 1.0 );
	}

	SetAbsVelocity( vecNewVel );	
}

void CHL1MPSatchelCharge::Precache( void )
{
	PrecacheModel( SATCHEL_CHARGE_MODEL );
	PrecacheScriptSound( "SatchelCharge.Bounce" );
}

void CHL1MPSatchelCharge::BounceSound( void )
{
	if ( gpGlobals->curtime > m_flNextBounceSoundTime )
	{
		EmitSound( "SatchelCharge.Bounce" );

		m_flNextBounceSoundTime = gpGlobals->curtime + 0.1;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
// Input  :
// Output :
//-----------------------------------------------------------------------------
CHL1MPSatchelCharge::CHL1MPSatchelCharge(void)
{
	m_vLastPosition.Init();
}

#endif
