//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "c_basetempentity.h"
#include <cliententitylist.h>
#include "ammodef.h"
#include "c_te_effect_dispatch.h"
#include "shot_manipulator.h"
// HL2SB: g_StringTableEffectDispatch, to resolve the tracer effect name the
// server shipped with this TE (see TE_HL2MPFireBullets).
#include "networkstringtable_clientdll.h"

// HL2SB: game/shared/lua/luamanager.h is not on this file's include path
// (client_hl2mp.vpc vs client_lua.vpc -- same reason c_te_effect_dispatch.cpp
// declares HL2SB_CreateLuaEffect by hand), so the bounded one-shot warning
// helper is declared here.
void HL2SB_WarnOnce( const char *pszKey, const char *pszFormat, ... );

class C_TEHL2MPFireBullets : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TEHL2MPFireBullets, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();

	// HL2SB: the network layer fills every prop on creation, but keep the
	// "no name" value explicit for the paths that read it first.
	C_TEHL2MPFireBullets() : m_iTracerName( 0 ) {}

	virtual void	PostDataUpdate( DataUpdateType_t updateType );

	void CreateEffects( void );

public:
	int		m_iPlayer;
	Vector	m_vecOrigin;
	Vector  m_vecDir;
	int		m_iAmmoID;
	int		m_iWeaponIndex;
	int		m_iSeed;
	float	m_flSpread;
	int		m_iShots;
	bool	m_bDoImpacts;
	bool	m_bDoTracers;
	// HL2SB: index into the "EffectDispatch" string table holding the tracer
	// effect name the shooter's weapon asked for (0 = the table's empty string,
	// i.e. the server did not send one).
	int		m_iTracerName;
};

class CTraceFilterSkipPlayerAndViewModelOnly : public CTraceFilter
{
public:
	virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
	{
		C_BaseEntity *pEntity = EntityFromEntityHandle( pServerEntity );
		if( pEntity &&
			( ( dynamic_cast<C_BaseViewModel *>( pEntity ) != NULL ) ||
			( dynamic_cast<C_BasePlayer *>( pEntity ) != NULL ) ) )
		{
			return false;
		}
		else
		{
			return true;
		}
	}
};

void C_TEHL2MPFireBullets::CreateEffects( void )
{
	CAmmoDef*	pAmmoDef	= GetAmmoDef();

	if ( pAmmoDef == NULL )
		 return;

	C_BaseEntity *pEnt = ClientEntityList().GetEnt( m_iPlayer );

	if ( pEnt )
	{
		C_BasePlayer *pPlayer = dynamic_cast<C_BasePlayer *>(pEnt);

		if ( pPlayer && pPlayer->GetActiveWeapon() )
		{
			C_BaseCombatWeapon *pWpn = dynamic_cast<C_BaseCombatWeapon *>( pPlayer->GetActiveWeapon() );

			if ( pWpn )
			{
				int iSeed = m_iSeed;
					
				CShotManipulator Manipulator( m_vecDir );

				// HL2SB: everything below runs Lua -- DispatchEffect() lands in
				// EFFECT:Init and DoImpactEffect() is the SWEP's own Lua method --
				// and a script can destroy the shooter's weapon while it runs.  So
				// never hold a raw weapon pointer across it: keep the weapon by
				// handle and re-resolve it once per shot.
				CHandle<C_BaseCombatWeapon> hWeapon = pWpn;
				pWpn = NULL;

				// Network-supplied loop bound: the server sends 5 bits, so this
				// only matters for a desynced or hostile peer.
				const int nShots = Min( m_iShots, 64 );

				for (int iShot = 0; iShot < nShots; iShot++)
				{
					pWpn = hWeapon.Get();

					if ( pWpn == NULL )
						break;

					RandomSeed( iSeed );	// init random system with this seed

					// Don't run the biasing code for the player at the moment.
					Vector vecDir = Manipulator.ApplySpread( Vector( m_flSpread, m_flSpread, m_flSpread ) );
					Vector vecEnd = m_vecOrigin + vecDir * MAX_TRACE_LENGTH;
					trace_t tr;
					CTraceFilterSkipPlayerAndViewModelOnly traceFilter;

					if( m_iShots > 1 && iShot % 2 )
					{
						// Half of the shotgun pellets are hulls that make it easier to hit targets with the shotgun.
						UTIL_TraceHull( m_vecOrigin, vecEnd, Vector( -3, -3, -3 ), Vector( 3, 3, 3 ), MASK_SHOT, &traceFilter, &tr );
					}
					else
					{
						UTIL_TraceLine( m_vecOrigin, vecEnd, MASK_SHOT, &traceFilter, &tr);
					}

					// HL2SB: capture everything that comes from the weapon BEFORE
					// any Lua runs (see the handle comment above): the tracer's
					// entity handle, the class name for the diagnostic, and the
					// tracer effect name.
					const CBaseHandle hTracerEnt = pWpn->GetRefEHandle();

					char szWeapon[ 64 ];
					Q_strncpy( szWeapon, pWpn->GetClassname(), sizeof( szWeapon ) );

					char szTracerName[ 128 ] = { 0 };

					if ( m_bDoTracers )
					{
						// Prefer the name the server sent with the shot.
						//
						// Asking our own weapon (pWpn->GetTracerType()) only works
						// when THIS realm ran the Lua FireBullets() that published
						// bullet.TracerName -- i.e. only when the shot was predicted
						// here.  When it was not, the name was empty and the shot
						// silently drew the stock "Tracer": the Nyan Gun's rainbow
						// tracer was missing for the shooter while its impacts,
						// sounds and damage all worked.
						const char *pTracerName = NULL;

						// Bounds-checked: the index arrives over the network, and
						// the engine's GetString() asserts on an out-of-range one
						// (its own DT_TEEffectDispatch relies on the string table
						// update landing first; don't make this path depend on it).
						if ( m_iTracerName > 0 && m_iTracerName < MAX_EFFECT_DISPATCH_STRINGS &&
							 g_StringTableEffectDispatch != NULL &&
							 m_iTracerName < g_StringTableEffectDispatch->GetNumStrings() )
						{
							pTracerName = g_StringTableEffectDispatch->GetString( m_iTracerName );
						}

						if ( pTracerName == NULL || pTracerName[0] == '\0' )
						{
							pTracerName = pWpn->GetTracerType();
						}

						if ( pTracerName == NULL || pTracerName[0] == '\0' )
						{
							pTracerName = "Tracer";
						}

						// Copied: GetTracerType() hands back a static buffer, and
						// Lua runs before the name is used below.
						Q_strncpy( szTracerName, pTracerName, sizeof( szTracerName ) );

						// HL2SB diagnostic: names the source of the tracer name in
						// ds_debug.log, once per weapon per DLL load (Warning, not
						// DevMsg -- DevMsg needs developer 1 and is then missing
						// exactly when we need it).  Keyed by weapon class so a
						// shot with another weapon in hand cannot hide this one.
						char szKey[ 192 ];
						Q_snprintf( szKey, sizeof( szKey ), "te-firebullets-tracer:%s:%d", szWeapon, m_iTracerName );
						HL2SB_WarnOnce( szKey,
							"TE_HL2MPFireBullets: tracers=%d impacts=%d tracer='%s' (from %s, table index %d) weapon='%s' shooter=%d\n",
							m_bDoTracers ? 1 : 0,
							m_bDoImpacts ? 1 : 0,
							szTracerName,
							( m_iTracerName > 0 ) ? "server TE" : "weapon GetTracerType",
							m_iTracerName,
							szWeapon,
							m_iPlayer );
					}

					// Impacts first: DoImpactEffect() calls into Lua and must be
					// the last use of the raw weapon pointer in this iteration.
					if ( m_bDoImpacts )
					{
						pWpn->DoImpactEffect( tr, pAmmoDef->DamageType( m_iAmmoID ) );
					}

					if ( m_bDoTracers )
					{
						CEffectData data;
						data.m_vStart = tr.startpos;
						data.m_vOrigin = tr.endpos;
						data.m_hEntity = hTracerEnt;
						data.m_flScale = 0.0f;
						data.m_fFlags |= TRACER_FLAG_USEATTACHMENT;
						// Stomp the start, since it's not going to be used anyway
						data.m_nAttachmentIndex = 1;

						DispatchEffect( szTracerName, data );
					}

					iSeed++;
				}
			}
		}
	}

}

void C_TEHL2MPFireBullets::PostDataUpdate( DataUpdateType_t updateType )
{
	if ( m_bDoTracers || m_bDoImpacts )
	{
		CreateEffects();
	}
}


IMPLEMENT_CLIENTCLASS_EVENT( C_TEHL2MPFireBullets, DT_TEHL2MPFireBullets, CTEHL2MPFireBullets );


BEGIN_RECV_TABLE_NOBASE(C_TEHL2MPFireBullets, DT_TEHL2MPFireBullets )
	RecvPropVector( RECVINFO( m_vecOrigin ) ),
	RecvPropVector( RECVINFO( m_vecDir ) ),
	RecvPropInt( RECVINFO( m_iAmmoID ) ),
	RecvPropInt( RECVINFO( m_iSeed ) ),
	RecvPropInt( RECVINFO( m_iShots ) ),
	RecvPropInt( RECVINFO( m_iPlayer ) ),
	RecvPropInt( RECVINFO( m_iWeaponIndex ) ),
	RecvPropFloat( RECVINFO( m_flSpread ) ),
	RecvPropBool( RECVINFO( m_bDoImpacts ) ),
	RecvPropBool( RECVINFO( m_bDoTracers ) ),
	RecvPropInt( RECVINFO( m_iTracerName ) ),
END_RECV_TABLE()


