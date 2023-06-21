//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "gamerules.h"
#include "player.h"
#include "items.h"
#include "engine/IEngineSound.h"
#include "hl1_items.h"
#include "in_buttons.h"


extern ConVar	sk_healthkit;
extern ConVar	sk_healthvial;
extern ConVar	sk_healthcharger;

//-----------------------------------------------------------------------------
// Small health kit. Heals the player when picked up.
//-----------------------------------------------------------------------------
class CHL1MPHealthKit : public CHL1Item
{
public:
	DECLARE_CLASS( CHL1MPHealthKit, CHL1Item );

	void Spawn( void );
	void Precache( void );
	bool MyTouch( CBasePlayer *pPlayer );
};

LINK_ENTITY_TO_CLASS( item_hl1mp_healthkit, CHL1MPHealthKit );
PRECACHE_REGISTER(item_hl1mp_healthkit);


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1MPHealthKit::Spawn( void )
{
	Precache();
	SetModel( "models/w_medkit.mdl" );

	BaseClass::Spawn();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1MPHealthKit::Precache( void )
{
	PrecacheModel("models/w_medkit.mdl");

	PrecacheScriptSound( "HealthKit.Touch" );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
// Output : 
//-----------------------------------------------------------------------------
bool CHL1MPHealthKit::MyTouch( CBasePlayer *pPlayer )
{
	if ( pPlayer->TakeHealth( sk_healthkit.GetFloat(), DMG_GENERIC ) )
	{
		CSingleUserRecipientFilter user( pPlayer );
		user.MakeReliable();

		UserMessageBegin( user, "ItemPickup" );
			WRITE_STRING( GetClassname() );
		MessageEnd();

		CPASAttenuationFilter filter( pPlayer, "HealthKit.Touch" );
		EmitSound( filter, pPlayer->entindex(), "HealthKit.Touch" );

		if ( g_pGameRules->ItemShouldRespawn( this ) )
		{
			Respawn();
		}
		else
		{
			UTIL_Remove(this);	
		}

		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Small dynamically dropped health kit
//-----------------------------------------------------------------------------

class CHL1MPHealthVial : public CHL1Item
{
public:
	DECLARE_CLASS( CHL1MPHealthVial, CHL1Item );

	void Spawn( void )
	{
		Precache();
		SetModel( "models/healthvial.mdl" );

		BaseClass::Spawn();
	}

	void Precache( void )
	{
		PrecacheModel("models/healthvial.mdl");

		PrecacheScriptSound( "HealthVial.Touch" );
	}

	bool MyTouch( CBasePlayer *pPlayer )
	{
		if ( pPlayer->TakeHealth( sk_healthvial.GetFloat(), DMG_GENERIC ) )
		{
			CSingleUserRecipientFilter user( pPlayer );
			user.MakeReliable();

			UserMessageBegin( user, "ItemPickup" );
				WRITE_STRING( GetClassname() );
			MessageEnd();

			CPASAttenuationFilter filter( pPlayer, "HealthVial.Touch" );
			EmitSound( filter, pPlayer->entindex(), "HealthVial.Touch" );

			if ( g_pGameRules->ItemShouldRespawn( this ) )
			{
				Respawn();
			}
			else
			{
				UTIL_Remove(this);	
			}

			return true;
		}

		return false;
	}
};

LINK_ENTITY_TO_CLASS( item_hl1mp_healthvial, CHL1MPHealthVial );
PRECACHE_REGISTER( item_hl1mp_healthvial );

//-----------------------------------------------------------------------------
// Wall mounted health kit. Heals the player when used.
//-----------------------------------------------------------------------------
class CHL1MPWallHealth : public CBaseToggle
{
public:
	DECLARE_CLASS( CHL1MPWallHealth, CBaseToggle );

	void Spawn( );
	void Precache( void );
	bool CreateVPhysics(void);
	void Off(void);
	void Recharge(void);
	bool KeyValue(  const char *szKeyName, const char *szValue );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	virtual int	ObjectCaps( void ) { return BaseClass::ObjectCaps() | m_iCaps; }

	float m_flNextCharge; 
	int		m_iReactivate ; // DeathMatch Delay until reactvated
	int		m_iJuice;
	int		m_iOn;			// 0 = off, 1 = startup, 2 = going
	float   m_flSoundTime;
	int		m_iCaps;

	DECLARE_DATADESC();
};

LINK_ENTITY_TO_CLASS(func_hl1mp_healthcharger, CHL1MPWallHealth);


BEGIN_DATADESC( CHL1MPWallHealth )

	DEFINE_FIELD( m_flNextCharge, FIELD_TIME),
	DEFINE_FIELD( m_iReactivate, FIELD_INTEGER),
	DEFINE_FIELD( m_iJuice, FIELD_INTEGER),
	DEFINE_FIELD( m_iOn, FIELD_INTEGER),
	DEFINE_FIELD( m_flSoundTime, FIELD_TIME),
	DEFINE_FIELD( m_iCaps, FIELD_INTEGER ),

	// Function Pointers
	DEFINE_FUNCTION( Off ),
	DEFINE_FUNCTION( Recharge ),

END_DATADESC()



//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pkvd - 
//-----------------------------------------------------------------------------
bool CHL1MPWallHealth::KeyValue(  const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "style") ||
		FStrEq(szKeyName, "height") ||
		FStrEq(szKeyName, "value1") ||
		FStrEq(szKeyName, "value2") ||
		FStrEq(szKeyName, "value3"))
	{
		return(true);
	}
	else if (FStrEq(szKeyName, "dmdelay"))
	{
		m_iReactivate = atoi(szValue);
		return(true);
	}

	return(BaseClass::KeyValue( szKeyName, szValue ));
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1MPWallHealth::Spawn(void)
{
	Precache( );

	SetSolid( SOLID_BSP );
	SetMoveType( MOVETYPE_PUSH );

	SetModel( STRING( GetModelName() ) );

	m_iJuice = sk_healthcharger.GetFloat();
	SetTextureFrameIndex( 0 );

	m_iCaps	= FCAP_CONTINUOUS_USE;

	CreateVPhysics();
}

//-----------------------------------------------------------------------------

bool CHL1MPWallHealth::CreateVPhysics(void)
{
	VPhysicsInitStatic();
	return true;

}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1MPWallHealth::Precache(void)
{
	PrecacheScriptSound( "WallHealth.Deny" );
	PrecacheScriptSound( "WallHealth.Start" );
	PrecacheScriptSound( "WallHealth.LoopingContinueCharge" );
	PrecacheScriptSound( "WallHealth.Recharge" );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pActivator - 
//			*pCaller - 
//			useType - 
//			value - 
//-----------------------------------------------------------------------------
void CHL1MPWallHealth::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{ 
	// Make sure that we have a caller
	if (!pActivator)
		return;
	// if it's not a player, ignore
	if ( !pActivator->IsPlayer() )
		return;

	// Reset to a state of continuous use.
	m_iCaps = FCAP_CONTINUOUS_USE;

	// if there is no juice left, turn it off
	if (m_iJuice <= 0)
	{
		Off();
		SetTextureFrameIndex( 1 );
	}

	CBasePlayer *pPlayer = ToBasePlayer( pActivator );
	
	// if the player doesn't have the suit, or there is no juice left, make the deny noise.
	if ((m_iJuice <= 0) || (!(pPlayer->m_Local.m_bWearingSuit)))
	{
		if (m_flSoundTime <= gpGlobals->curtime)
		{
			m_flSoundTime = gpGlobals->curtime + 0.62;
			EmitSound( "WallHealth.Deny" );
		}
		return;
	}

	if( pActivator->GetHealth() >= pActivator->GetMaxHealth() )
	{
		CBasePlayer *pPlayer = dynamic_cast<CBasePlayer *>(pActivator);

		if( pPlayer )
		{
			pPlayer->m_afButtonPressed &= ~IN_USE;
		}
		
		// Make the user re-use me to get started drawing health.
		m_iCaps = FCAP_IMPULSE_USE;

		EmitSound( "WallHealth.Deny" );
		return;
	}

	SetNextThink( gpGlobals->curtime + 0.25 );
	SetThink(&CHL1MPWallHealth::Off);

	// Time to recharge yet?

	if (m_flNextCharge >= gpGlobals->curtime)
		return;

	// Play the on sound or the looping charging sound
	if (!m_iOn)
	{
		m_iOn++;
		EmitSound( "WallHealth.Start" );
		m_flSoundTime = 0.56 + gpGlobals->curtime;
	}
	if ((m_iOn == 1) && (m_flSoundTime <= gpGlobals->curtime))
	{
		m_iOn++;
		CPASAttenuationFilter filter( this, "WallHealth.LoopingContinueCharge" );
		filter.MakeReliable();
		EmitSound( filter, entindex(), "WallHealth.LoopingContinueCharge" );
	}


	// charge the player
	if ( pActivator->TakeHealth( 1, DMG_GENERIC ) )
	{
		m_iJuice--;
	}

	// govern the rate of charge
	m_flNextCharge = gpGlobals->curtime + 0.1;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1MPWallHealth::Recharge(void)
{
	EmitSound( "WallHealth.Recharge" );
	m_iJuice = sk_healthcharger.GetFloat();
	SetTextureFrameIndex( 0 );
	SetThink( NULL );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL1MPWallHealth::Off(void)
{
	// Stop looping sound.
	if (m_iOn > 1)
		StopSound( "WallHealth.LoopingContinueCharge" );

	m_iOn = 0;

	if ((!m_iJuice) &&  ( ( m_iReactivate = g_pGameRules->FlHealthChargerRechargeTime() ) > 0) )
	{
		SetNextThink( gpGlobals->curtime + m_iReactivate );
		SetThink(&CHL1MPWallHealth::Recharge);
	}
	else
		SetThink( NULL );
}

