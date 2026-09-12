//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

#include "cbase.h"
#include "basescripted.h"
#include "luamanager.h"
#ifdef CLIENT_DLL
#include "lc_baseanimating.h"
#else
#include "lbaseanimating.h"
#endif
#include "lbaseentity_shared.h"
#include "lvphysics_interface.h"
#include "mathlib/lvector.h"
#ifndef CLIENT_DLL
// HL2SB: gamevcollisionevent_t, for ENT:PhysicsCollide.
#include "physics.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( BaseScripted, DT_BaseScripted )

BEGIN_NETWORK_TABLE( CBaseScripted, DT_BaseScripted )
#ifdef CLIENT_DLL
	RecvPropString( RECVINFO( m_iScriptedClassname ) ),
#else
	SendPropString( SENDINFO( m_iScriptedClassname ) ),
#endif
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA(	CBaseScripted )
END_PREDICTION_DATA()

#ifdef CLIENT_DLL
static C_BaseEntity *CCBaseScriptedFactory( void )
{
	return static_cast< C_BaseEntity * >( new CBaseScripted );
};
#endif

#ifndef CLIENT_DLL
static CUtlDict< CEntityFactory<CBaseScripted>*, unsigned short > m_EntityFactoryDatabase;
#endif

void RegisterScriptedEntity( const char *className )
{
#ifdef CLIENT_DLL
	if ( GetClassMap().FindFactory( className ) )
	{
		return;
	}

	GetClassMap().Add( className, "CBaseScripted", sizeof( CBaseScripted ),
		&CCBaseScriptedFactory, true );
#else
	if ( EntityFactoryDictionary()->FindFactory( className ) )
	{
		return;
	}

	unsigned short lookup = m_EntityFactoryDatabase.Find( className );
	if ( lookup != m_EntityFactoryDatabase.InvalidIndex() )
	{
		return;
	}

	CEntityFactory<CBaseScripted> *pFactory = new CEntityFactory<CBaseScripted>( className );

	lookup = m_EntityFactoryDatabase.Insert( className, pFactory );
	Assert( lookup != m_EntityFactoryDatabase.InvalidIndex() );
#endif
}

void ResetEntityFactoryDatabase( void )
{
#ifdef CLIENT_DLL
#ifdef LUA_SDK
	GetClassMap().RemoveAllScripted();
#endif
#else
	for ( int i=m_EntityFactoryDatabase.First(); i != m_EntityFactoryDatabase.InvalidIndex(); i=m_EntityFactoryDatabase.Next( i ) )
	{
		delete m_EntityFactoryDatabase[ i ];
	}
	m_EntityFactoryDatabase.RemoveAll();
#endif
}

CBaseScripted::CBaseScripted( void )
{
#ifdef LUA_SDK
	// UNDONE: We're done in CBaseEntity
	m_nTableReference = LUA_NOREF;
#endif
}

CBaseScripted::~CBaseScripted( void )
{
	// Andrew; This is actually done in CBaseEntity. I'm doing it here because
	// this is the class that initialized the reference.
	//
	// HL2SB: m_nTableReference is CBaseEntity's member (there is no shadowing
	// field in this class), so ~CBaseEntity runs lua_unref() on the SAME ref
	// right after this.  Unref'ing a ref twice is not harmless: luaL_unref()
	// splices the slot into the registry free list by writing t[ref] = t[0],
	// so the second call stores the number ref into the slot and leaves the
	// free list pointing at it again.  The next luaL_ref() then hands that
	// same slot to a second live object, and lua_getref() on the stale ref
	// reads a NUMBER -- lua_getfield() on it raises "attempt to index a number
	// value" from an unprotected context, which aborts the process.
	//
	// Clearing the member makes the base-class unref a no-op (luaL_unref
	// ignores negative refs), and the L != NULL test covers shutdown, where
	// entities are destroyed after the lua_State is already gone
	// (see the L = NULL comment in luamanager.cpp).
#ifdef LUA_SDK
	if ( L != NULL && m_nTableReference >= 0 )
		lua_unref( L, m_nTableReference );

	m_nTableReference = LUA_NOREF;
#endif
}

void CBaseScripted::LoadScriptedEntity( void )
{
	lua_getglobal( L, "entity" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "get" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_remove( L, -2 );
			lua_pushstring( L, GetClassname() );
			luasrc_pcall( L, 1, 1, 0 );
		}
		else
		{
			lua_pop( L, 2 );
		}
	}
	else
	{
		lua_pop( L, 1 );
	}
}

void CBaseScripted::InitScriptedEntity( void )
{
#if defined ( LUA_SDK )
#if 0
#ifndef CLIENT_DLL
	// Let the instance reinitialize itself for the client.
	if ( m_nTableReference != LUA_NOREF )
		return;
#endif
#endif

	SetThink( &CBaseScripted::Think );
#ifdef CLIENT_DLL
	SetNextClientThink( gpGlobals->curtime );
#endif
	SetNextThink( gpGlobals->curtime );

	SetTouch( &CBaseScripted::Touch );

	char className[ 255 ];
#if defined ( CLIENT_DLL )
	if ( strlen( GetScriptedClassname() ) > 0 )
		Q_strncpy( className, GetScriptedClassname(), sizeof( className ) );
	else
		Q_strncpy( className, GetClassname(), sizeof( className ) );
#else
	Q_strncpy( m_iScriptedClassname.GetForModify(), GetClassname(), sizeof( className ) );
 	Q_strncpy( className, GetClassname(), sizeof( className ) );
#endif
 	Q_strlower( className );
	SetClassname( className );

	// HL2SB: < 0, not == LUA_NOREF.  luaL_ref() returns LUA_REFNIL (-1) when
	// entity.get() yielded no table (a classname with no lua/entities script),
	// and that value is not LUA_NOREF (-2) -- so the old test fell into the
	// "already loaded" branch below and tried table.merge() against a bogus
	// reference on every subsequent Spawn.
	if ( m_nTableReference < 0 )
	{
		LoadScriptedEntity();
		m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
	}
	else
	{
		lua_getglobal( L, "table" );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "merge" );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_remove( L, -2 );
				lua_getref( L, m_nTableReference );
				LoadScriptedEntity();
				luasrc_pcall( L, 2, 0, 0 );
			}
			else
			{
				lua_pop( L, 2 );
			}
		}
		else
		{
			lua_pop( L, 1 );
		}
	}

	BEGIN_LUA_CALL_ENTITY_METHOD( "Initialize" );
	END_LUA_CALL_ENTITY_METHOD( 0, 0 );
#endif
}

#ifdef CLIENT_DLL
int CBaseScripted::DrawModel( int flags )
{
#ifdef LUA_SDK
	// HL2SB: GMod's ENT:Draw() REPLACES the default model draw -- the example on
	// GMod's own wiki is
	//
	//     function ENT:Draw()
	//         self:DrawModel()          -- ask for the model explicitly
	//     end
	//
	// so a scripted entity that renders itself and never calls DrawModel (the
	// nyan bomb draws two textured quads and nothing else) must not also have its
	// model drawn underneath.  Nothing dispatched "Draw" before this, and the
	// entity's own ENT:Draw never ran at all -- which for weapon_nyangun's bomb
	// meant no visible rendering of any kind.
	//
	// The method's PRESENCE decides, because the dispatch macro cannot tell "no
	// such method" from "the method returned nil".  An explicit `false` is
	// honoured as "also draw the model".
	bool bHasDraw = false;

	if ( L != NULL && m_nTableReference >= 0 )
	{
		lua_getref( L, m_nTableReference );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "Draw" );
			bHasDraw = lua_isfunction( L, -1 ) != 0;
			lua_pop( L, 1 );
		}
		lua_pop( L, 1 );
	}

	if ( bHasDraw )
	{
		BEGIN_LUA_CALL_ENTITY_METHOD( "Draw" );
		END_LUA_CALL_ENTITY_METHOD( 0, 1 );

		if ( !( lua_isboolean( L, -1 ) && lua_toboolean( L, -1 ) == 0 ) )
		{
			lua_pop( L, 1 );
			return 1;
		}

		lua_pop( L, 1 );
	}

	BEGIN_LUA_CALL_ENTITY_METHOD( "DrawModel" );
		lua_pushinteger( L, flags );
	END_LUA_CALL_ENTITY_METHOD( 1, 1 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::DrawModel( flags );
}

void CBaseScripted::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	if ( updateType == DATA_UPDATE_CREATED )
	{
		if ( m_iScriptedClassname.Get() )
		{
			SetClassname( m_iScriptedClassname.Get() );
			InitScriptedEntity();
		}
	}
}

const char *CBaseScripted::GetScriptedClassname( void )
{
	if ( m_iScriptedClassname.Get() )
		return m_iScriptedClassname.Get();
	return BaseClass::GetClassname();
}
#endif

void CBaseScripted::Spawn( void )
{
	BaseClass::Spawn();

#ifndef CLIENT_DLL
	InitScriptedEntity();
#endif
}

void CBaseScripted::Precache( void )
{
	BaseClass::Precache();

	// InitScriptedEntity();
}

#ifdef CLIENT_DLL
void CBaseScripted::ClientThink()
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_ENTITY_METHOD( "ClientThink" );
	END_LUA_CALL_ENTITY_METHOD( 0, 0 );
#endif
}
#endif

void CBaseScripted::Think()
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_ENTITY_METHOD( "Think" );
	END_LUA_CALL_ENTITY_METHOD( 0, 0 );
#endif
}

void CBaseScripted::StartTouch( CBaseEntity *pOther )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_ENTITY_METHOD( "StartTouch" );
		lua_pushentity( L, pOther );
	END_LUA_CALL_ENTITY_METHOD( 1, 0 );
#endif
}

void CBaseScripted::Touch( CBaseEntity *pOther )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_ENTITY_METHOD( "Touch" );
		lua_pushentity( L, pOther );
	END_LUA_CALL_ENTITY_METHOD( 1, 0 );
#endif
}

void CBaseScripted::EndTouch( CBaseEntity *pOther )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_ENTITY_METHOD( "EndTouch" );
		lua_pushentity( L, pOther );
	END_LUA_CALL_ENTITY_METHOD( 1, 0 );
#endif
}

void CBaseScripted::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	BaseClass::VPhysicsUpdate( pPhysics );

#ifdef LUA_SDK
	BEGIN_LUA_CALL_ENTITY_METHOD( "VPhysicsUpdate" );
		lua_pushphysicsobject( L, pPhysics );
	END_LUA_CALL_ENTITY_METHOD( 1, 0 );

	// GMod name for the same callback.
	BEGIN_LUA_CALL_ENTITY_METHOD( "PhysicsUpdate" );
		lua_pushphysicsobject( L, pPhysics );
	END_LUA_CALL_ENTITY_METHOD( 1, 0 );
#endif
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// Purpose: HL2SB GMod compat: ENT:PhysicsCollide( data, physObj ).
//
// GMod hands a scripted entity a CollisionData table every time its physics
// object collides with something, and weapon_nyangun's bomb is entirely built
// around it: PhysicsCollide() is where the explosion, the blast damage and
// self:Remove() live.  Nothing dispatched it, so the bomb bounced forever and
// never went off.
//
// The table carries GMod's documented keys.  (The script here only reads self,
// but the shape is what addons are written against.)
//-----------------------------------------------------------------------------
void CBaseScripted::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	BaseClass::VPhysicsCollision( index, pEvent );

#ifdef LUA_SDK
	if ( pEvent == NULL || index < 0 || index > 1 )
		return;

	const int nOther = 1 - index;

	// HL2SB: the bomb's whole detonation lives in this dispatch, and it hands the
	// script a physics object and the entity it hit.  Both can legitimately be
	// missing (an impact against the world / a brush), and lua_pushphysicsobject
	// / lua_pushentity would then push a handle to nothing -- so name the path
	// once if it ever happens with a NULL, so the next crash log says which of
	// these it was.
	if ( pEvent->pObjects[ index ] == NULL ) {
		HL2SB_WarnOnce( "physicscollide-noobject",
			"ENT:PhysicsCollide: pObjects[%d] is NULL (no physics object for this side of the collision)", index );
	}
	if ( pEvent->pEntities[ nOther ] == NULL ) {
		HL2SB_WarnOnce( "physicscollide-noentity",
			"ENT:PhysicsCollide: pEntities[%d] is NULL (the collision was against the world)", nOther );
	}

	Vector vecHitPos = vec3_origin;
	Vector vecHitNormal = vec3_origin;
	if ( pEvent->pInternalData != NULL )
	{
		pEvent->pInternalData->GetContactPoint( vecHitPos );
		pEvent->pInternalData->GetSurfaceNormal( vecHitNormal );
	}

	BEGIN_LUA_CALL_ENTITY_METHOD( "PhysicsCollide" );
		{
			lua_newtable( L );

			lua_pushstring( L, "HitEntity" );
			lua_pushentity( L, pEvent->pEntities[ nOther ] );
			lua_settable( L, -3 );

			lua_pushstring( L, "HitPos" );
			lua_pushvector( L, vecHitPos );
			lua_settable( L, -3 );

			lua_pushstring( L, "HitNormal" );
			lua_pushvector( L, vecHitNormal );
			lua_settable( L, -3 );

			lua_pushstring( L, "Speed" );
			lua_pushnumber( L, pEvent->collisionSpeed );
			lua_settable( L, -3 );

			lua_pushstring( L, "DeltaTime" );
			lua_pushnumber( L, pEvent->deltaCollisionTime );
			lua_settable( L, -3 );

			lua_pushstring( L, "OurOldVelocity" );
			lua_pushvector( L, pEvent->preVelocity[ index ] );
			lua_settable( L, -3 );

			lua_pushstring( L, "TheirOldVelocity" );
			lua_pushvector( L, pEvent->preVelocity[ nOther ] );
			lua_settable( L, -3 );

			lua_pushstring( L, "PhysObject" );
			lua_pushphysicsobject( L, pEvent->pObjects[ index ] );
			lua_settable( L, -3 );

			lua_pushstring( L, "HitPhysicsObject" );
			lua_pushphysicsobject( L, pEvent->pObjects[ nOther ] );
			lua_settable( L, -3 );
		}
		lua_pushphysicsobject( L, pEvent->pObjects[ index ] );
	END_LUA_CALL_ENTITY_METHOD( 2, 0 );
#endif
}
#endif // !CLIENT_DLL


//-----------------------------------------------------------------------------
// Purpose: GMod ENT contract: OnRemove is called before the entity is deleted.
//-----------------------------------------------------------------------------
void CBaseScripted::UpdateOnRemove( void )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_ENTITY_METHOD( "OnRemove" );
		lua_pushboolean( L, true );  /* fullUpdate */
	END_LUA_CALL_ENTITY_METHOD( 1, 0 );
#endif

	BaseClass::UpdateOnRemove();
}

