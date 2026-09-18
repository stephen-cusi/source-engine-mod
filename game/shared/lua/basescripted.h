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


