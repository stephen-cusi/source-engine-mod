//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "basetempentity.h"
// HL2SB: g_pStringTableEffectDispatch + MAX_EFFECT_DISPATCH_STRING_BITS, to ship
// the tracer effect name as an index into the engine's own effect-name table.
#include "networkstringtable_gamedll.h"
#include "effect_dispatch_data.h"


#define NUM_BULLET_SEED_BITS 8


//-----------------------------------------------------------------------------
// Purpose: Display's a blood sprite
//-----------------------------------------------------------------------------
class CTEHL2MPFireBullets : public CBaseTempEntity
{
public:
	DECLARE_CLASS( CTEHL2MPFireBullets, CBaseTempEntity );
	DECLARE_SERVERCLASS();

					CTEHL2MPFireBullets( const char *name );
	virtual			~CTEHL2MPFireBullets( void );

public:
	CNetworkVar( int, m_iPlayer );
	// HL2SB: the firing weapon's entindex.  The client's recv table has always
	// had this prop and the server's send table never did, and the engine
	// matches recv props to send props BY INDEX (engine/dt_common_eng.cpp:108),
	// so every field after m_iPlayer was shifted by one on the wire:
	//   m_flSpread    <- m_bDoImpacts   (0 or 1, so the client re-traced every
	//                                    shot with a garbage spread)
	//   m_bDoImpacts  <- m_bDoTracers   (right by luck)
	//   m_bDoTracers  <- whatever came next
	//   m_iTracerName <- nothing at all
	// Sending it here is what makes the rest of the table line up.
	CNetworkVar( int, m_iWeaponIndex );
	CNetworkVector( m_vecOrigin );
	CNetworkVector( m_vecDir );
	CNetworkVar( int, m_iAmmoID );
	CNetworkVar( int, m_iSeed );
	CNetworkVar( int, m_iShots );
	CNetworkVar( float, m_flSpread );
	CNetworkVar( bool, m_bDoImpacts );
	CNetworkVar( bool, m_bDoTracers );
	// HL2SB: index into the "EffectDispatch" network string table holding the
	// tracer effect name the shooter wanted ("rb655_nyan_tracer"), 0 for none.
	CNetworkVar( int, m_iTracerName );
};

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *name - 
//-----------------------------------------------------------------------------
CTEHL2MPFireBullets::CTEHL2MPFireBullets( const char *name ) :
	CBaseTempEntity( name )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CTEHL2MPFireBullets::~CTEHL2MPFireBullets( void )
{
}

IMPLEMENT_SERVERCLASS_ST_NOBASE(CTEHL2MPFireBullets, DT_TEHL2MPFireBullets)
	SendPropVector( SENDINFO(m_vecOrigin), -1, SPROP_COORD ),
	SendPropVector( SENDINFO(m_vecDir), -1 ),
	SendPropInt( SENDINFO( m_iAmmoID ), 5, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iSeed ), NUM_BULLET_SEED_BITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iShots ), 5, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iPlayer ), 6, SPROP_UNSIGNED ), 	// max 64 players, see MAX_PLAYERS
	SendPropInt( SENDINFO( m_iWeaponIndex ), 11, SPROP_UNSIGNED ),	// HL2SB: see the class comment -- the client already had this prop
	SendPropFloat( SENDINFO( m_flSpread ), 10, 0, 0, 1 ),	
	SendPropBool( SENDINFO( m_bDoImpacts ) ),
	SendPropBool( SENDINFO( m_bDoTracers ) ),
	SendPropInt( SENDINFO( m_iTracerName ), MAX_EFFECT_DISPATCH_STRING_BITS, SPROP_UNSIGNED ),
END_SEND_TABLE()


// Singleton
static CTEHL2MPFireBullets g_TEHL2MPFireBullets( "Shotgun Shot" );


//-----------------------------------------------------------------------------
// HL2SB: WHY THIS TE CARRIES THE TRACER NAME
//
// GMod's bullet tables name their tracer (weapon_nyangun: bullet.TracerName =
// "rb655_nyan_tracer"), and a SWEP publishes it into its own Lua table when the
// Lua FireBullets() binding runs (lbaseentity_shared.cpp) -- CHL2MPScriptedWeapon
// ::GetTracerType() then hands it to the engine.
//
// The client that receives this TE (C_TEHL2MPFireBullets::CreateEffects) is the
// one that actually draws the tracers, and it used to ask ITS OWN weapon for
// GetTracerType().  That only works if the client happened to run the Lua
// FireBullets() itself, i.e. only if the shot was predicted on that realm -- and
// GMod SWEPs gate that on IsFirstTimePredicted() (weapon_nyangun.lua:95), which
// the client answers from the live prediction state.  When it does not run, the
// name is unknown on the client and the tracer silently falls back to the stock
// "Tracer" effect: the rainbow came out missing for the shooter while every
// other part of the shot (impacts, sounds, damage) worked.
//
// So the server -- the authority, which always knows the name -- ships it with
// the shot.  The name travels as an index into the engine's "EffectDispatch"
// network string table, exactly the way DispatchEffect() itself sends one, which
// needs no new table and no string property.
//-----------------------------------------------------------------------------
void TE_HL2MPFireBullets( 
	int	iPlayerIndex,
	const Vector &vOrigin,
	const Vector &vDir,
	int	iAmmoID,
	int iSeed,
	int iShots,
	float flSpread,
	bool bDoTracers,
	bool bDoImpacts,
	const char *pszTracerName,
	int iWeaponIndex )
{
	CPASFilter filter( vOrigin );
	filter.UsePredictionRules();

	g_TEHL2MPFireBullets.m_iPlayer = iPlayerIndex;
	g_TEHL2MPFireBullets.m_iWeaponIndex = iWeaponIndex;
	g_TEHL2MPFireBullets.m_vecOrigin = vOrigin;
	g_TEHL2MPFireBullets.m_vecDir = vDir;
	g_TEHL2MPFireBullets.m_iSeed = iSeed;
	g_TEHL2MPFireBullets.m_iShots = iShots;
	g_TEHL2MPFireBullets.m_flSpread = flSpread;
	g_TEHL2MPFireBullets.m_iAmmoID = iAmmoID;
	g_TEHL2MPFireBullets.m_bDoTracers = bDoTracers;
	g_TEHL2MPFireBullets.m_bDoImpacts = bDoImpacts;

	// Index 0 is the string table's empty string, i.e. "no name here".
	g_TEHL2MPFireBullets.m_iTracerName = 0;
	if ( pszTracerName != NULL && pszTracerName[0] != '\0' && g_pStringTableEffectDispatch != NULL )
	{
		int iIndex = g_pStringTableEffectDispatch->AddString( CBaseEntity::IsServer(), pszTracerName );
		if ( iIndex > 0 )
		{
			g_TEHL2MPFireBullets.m_iTracerName = iIndex;
		}
	}
	
	Assert( iSeed < (1 << NUM_BULLET_SEED_BITS) );
	
	g_TEHL2MPFireBullets.Create( filter, 0 );
}
