//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef BASESCRIPTED_H
#define BASESCRIPTED_H

#include "predictable_entity.h"
#include "baseentity_shared.h"

#if defined( CLIENT_DLL )
#define CBaseScripted C_BaseScripted

#endif 

#ifndef CLIENT_DLL
// HL2SB GMod compat: ENT:PhysicsCollide( data, physObj ).  Server only, because
// gamevcollisionevent_t and CBaseEntity::VPhysicsCollision exist only there.
struct gamevcollisionevent_t;
class  CDmgAccumulator;	// HL2SB 2026-09-29: TraceAttack override parameter type
#endif

class CBaseScripted : /* public CBaseEntity */ public CBaseAnimating
{
public:
	// DECLARE_CLASS( CBaseScripted, CBaseEntity );
	DECLARE_CLASS( CBaseScripted, CBaseAnimating );
	DECLARE_PREDICTABLE();
	DECLARE_NETWORKCLASS();

	CBaseScripted();
	~CBaseScripted();

	bool	IsScripted( void ) const { return true; }
	
	// CBaseEntity overrides.
public:
	void	Think();	

	void	Spawn( void );
	void	Precache( void );
	void	LoadScriptedEntity( void );
	// HL2SB: bCallInitialize=false binds the Lua class WITHOUT dispatching
	// ENT:Initialize -- that is the ents.Create path (GMod runs Initialize at
	// Spawn, after the script has set a model).  The Spawn path keeps the
	// default true.
	void	InitScriptedEntity( bool bCallInitialize = true );

	void	StartTouch( CBaseEntity *pOther );
	void	Touch( CBaseEntity *pOther ); 
	void	EndTouch( CBaseEntity *pOther );

	// GMod ENT contract: OnRemove is called before the entity is deleted.
	virtual void	UpdateOnRemove( void );

#ifdef CLIENT_DLL
	// model specific
	virtual int DrawModel( int flags );


	// HL2SB GMod compat: answers the script's ENT.RenderGroup, which is what picks
	// between ENTITY:Draw and ENTITY:DrawTranslucent (and sorts the entity
	// translucently).  A sprite entity that only defines DrawTranslucent -- the
	// npc_verity nextbot draws itself with render.DrawQuadEasy -- is invisible
	// without it: it fell through to the inherited ENT:Draw(), which asks for a
	// model it does not have.
	virtual RenderGroup_t GetRenderGroup( void );

	bool m_bLuaRenderGroupRead;		// ENT.RenderGroup has been read (it is a constant)
	int  m_nLuaRenderGroup;			// ... and its value, -1 when the script has none
#endif

	virtual void VPhysicsUpdate( IPhysicsObject *pPhysics );

#ifndef CLIENT_DLL
	// HL2SB GMod compat: ENT:PhysicsCollide( data, physObj ) -- the callback
	// weapon_nyangun's bomb entity explodes from.
	virtual void	VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	// HL2SB GMod compat (2026-10-08): the three map-lifecycle hooks GMod
	// dispatches on scripted entities and this fork never did --
	// ENTITY:KeyValue( key, value ) (BSP keyvalues, env_skypaint's colours),
	// ENTITY:AcceptInput( activator, caller, data ) (Set<NetworkVar> I/O) and
	// ENTITY:UpdateTransmitState() (TRANSMIT_* answers; env_skypaint must be
	// transmitted always or the client sky proxy has no data).
	virtual bool	KeyValue( const char *szKeyName, const char *szValue );
	virtual bool	AcceptInput( const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, variant_t Value, int outputID );
	virtual int		UpdateTransmitState( void );

	// HL2SB GMod compat (2026-09-21): ENT:Use( activator, caller ) -- GMod
	// routes +use to every scripted entity; see basescripted.cpp UseHandler.
	virtual int		ObjectCaps( void );
	void	UseHandler( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	// HL2SB GMod compat (2026-09-29): ENT:OnTakeDamage( damageInfo ) -- GMod's
	// scripted entities get every damage event through the same CBaseEntity
	// funnel (bullets, radius blasts, util.BlastDamage); the class never
	// overrode it, so minecraft's ENT:OnTakeDamage (TNT ignition) and cod_c4's
	// (shot-to-detonate) never ran and those entities were invulnerable
	// statues.  When the script defines OnTakeDamage the SCRIPT consumes the
	// event (GMod: the base health pipeline is not entered); otherwise the
	// inherited CBaseEntity handler applies, as before.
	virtual int		OnTakeDamage( const CTakeDamageInfo &info );

	// HL2SB GMod compat (2026-09-29): scripted entities default m_takedamage to
	// DAMAGE_NO (nothing sets it), so the base CBaseEntity::TraceAttack `if
	// (m_takedamage)` gate drops BULLET damage on the floor before OnTakeDamage
	// ever dispatches.  The dirt block is provably solid server-side (contents=
	// CONTENTS_SOLID, vcollide solidCount=1, right AABB) yet OnTakeDamage fired
	// zero times per shotgun volley -- the bullets were simply refused entry.
	// A GMod script that defines ENT:OnTakeDamage has opted the entity into the
	// damage system, so open that gate for exactly those entities.  Entities
	// without the handler keep DAMAGE_NO and stay non-targetable (parity).
	virtual void	TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator = NULL );

	// Shared "script table defines ENT:OnTakeDamage" test for the two overrides.
	bool			HasLuaOnTakeDamage();
#endif

#ifdef CLIENT_DLL
// IClientThinkable.
public:
	// Called whenever you registered for a think message (with SetNextClientThink).
	virtual void	ClientThink();

	virtual void	OnDataChanged( DataUpdateType_t updateType );
	virtual const char *GetScriptedClassname( void );
#endif

private:
	CBaseScripted( const CBaseScripted & ); // not defined, not accessible

	CNetworkString( m_iScriptedClassname, 255 );
};

void RegisterScriptedEntity( const char *szClassname );
void ResetEntityFactoryDatabase( void );

#endif


