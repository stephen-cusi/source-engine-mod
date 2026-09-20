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
#include "utlstring.h"	// HL2SB: CUtlString for the client draw-probe classnames
#ifndef CLIENT_DLL
// HL2SB: gamevcollisionevent_t, for ENT:PhysicsCollide.
#include "physics.h"
#include "luanextbot.h"	// HL2SB: IsLuaNextBot -- nextbots bind via LoadNextBotScript, not here
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

#ifdef CLIENT_DLL
	// HL2SB: the script's render group is only read once, and "not read yet" has to
	// be distinguishable from "read and absent" -- uninitialised memory would make
	// the first GetRenderGroup() answer with garbage.
	m_bLuaRenderGroupRead = false;
	m_nLuaRenderGroup = -1;
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

void CBaseScripted::InitScriptedEntity( bool bCallInitialize )
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
#ifndef CLIENT_DLL
	// HL2SB GMod compat (2026-09-21): route the player's +use to ENT:Use.
	// GMod scripted entities are always use targets; without SetUse() the
	// engine's use dispatch never selected them and E did nothing (the Nuke
	// Pack arms its bombs this way).
	SetUse( &CBaseScripted::UseHandler );
#endif

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
#ifndef CLIENT_DLL
		// HL2SB: a Lua nextbot binds through CLuaNextBot::LoadNextBotScript()
		// (luanextbot.cpp), which pushes self.loco BEFORE the script ever sees
		// the table -- the generic bind below cannot do that, and the first
		// ENT:Initialize indexed a nil self.loco (scp049-2's CollisionSetup at
		// npc_scp_049-2.lua:120, skipping the model, the collision setup and
		// everything after it).  CLuaNextBot::Spawn() calls LoadNextBotScript()
		// right after this function returns; Think/Touch are already wired
		// above and Think is overridden to CLuaNextBot's own.
		if ( IsLuaNextBot( className ) )
			return;
#endif
		LoadScriptedEntity();

		// HL2SB: diagnostic for the "attempt to call a nil value (method ...)"
		// family on freshly created scripted entities.  Reports exactly what
		// entity.get() produced: no table at all, or a table with how many
		// functions -- a count of 0..2 against an addon class that defines a
		// dozen ENT methods means the registration was PARTIAL (e.g. shared.lua
		// registered without init.lua's methods).
		{
			const bool bGotTable = lua_istable( L, -1 ) != 0;
			int nFuncs = 0;
			if ( bGotTable )
			{
				lua_pushnil( L );
				while ( lua_next( L, -2 ) != 0 )
				{
					if ( lua_isfunction( L, -1 ) )
						++nFuncs;
					lua_pop( L, 1 );
				}
			}

			static int s_nBindReports = 0;
			if ( s_nBindReports < 40 )
			{
				++s_nBindReports;
				luasrc_LuaWarnMsgF( "[HL2SB] bind '%s': table=%d funcs=%d\n",
					className, bGotTable ? 1 : 0, nFuncs );
			}
		}

		// HL2SB GMod SENT compat: GMod's engine calls ENT:SetupDataTables() while
		// it sets a scripted entity up, and that is where ENT:NetworkVar()
		// declares the per-instance accessors the script uses.  GMod's sent_ball
		// declares BallSize/BallColor there and its SpawnFunction calls
		// SetBallSize() before Spawn(), so without this call the entity throws on
		// its first line ("attempt to call a method 'SetBallSize'").
		//
		// The shim itself is not defined by the entity script: it is installed
		// here from the globals the engine publishes, so a stock GMod entity
		// script runs unmodified.  When those globals are absent this is a no-op,
		// which keeps entities that do not use NetworkVar working.
		if ( lua_istable( L, -1 ) )
		{
			// HL2SB GMod compat: `self.Entity` is the entity, exactly as GMod's
			// scripted-entity tables have it (GMod's engine sets it, and addons
			// call self.Entity:Foo() as freely as self:Foo()).  It has to be in
			// place before ENT:Initialize() runs - that is where a script
			// usually reaches for it first (SCP-096's nextbot opens its
			// Initialize() with self.Entity:SetCollisionBounds(...), and with
			// the field nil that raised on line one and skipped the whole setup).
			lua_pushanimating( L, this );
			lua_setfield( L, -2, "Entity" );

			lua_getglobal( L, "HL2SB_EntityNetworkVar" );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_setfield( L, -2, "NetworkVar" );
			}
			else
			{
				lua_pop( L, 1 );
			}

			lua_getglobal( L, "HL2SB_EntityNetworkVarNotify" );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_setfield( L, -2, "NetworkVarNotify" );
			}
			else
			{
				lua_pop( L, 1 );
			}

			// HL2SB: GMod's Entity:DTVar needs the same treatment as NetworkVar above, and
			// for the same reason: SetupDataTables() is invoked with the entity's LUA TABLE
			// (lua_pushvalue below, "self: the entity's Lua table"), not with the entity
			// userdata - so a method that lives only on the entity metatable is invisible
			// inside it.  cod_c4's ENT:SetupDataTables calls self:DTVar( "Float", 0, ... )
			// and raised "attempt to call a nil value (method 'DTVar')" on every C4 spawn.
			lua_getglobal( L, "HL2SB_EntityDTVar" );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_setfield( L, -2, "DTVar" );
			}
			else
			{
				lua_pop( L, 1 );
			}

			lua_getfield( L, -1, "SetupDataTables" );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_pushvalue( L, -2 );		// self: the entity's Lua table
				luasrc_pcall( L, 1, 0, 0 );
			}
			else
			{
				lua_pop( L, 1 );
			}
		}

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

	// HL2SB GMod SENT compat: the engine defaults for an "anim" scripted entity.
	//
	// Measured, not guessed.  A live sent_ball spawned inside Garry's Mod reports
	//
	//     GetSolid()    = 6   SOLID_VPHYSICS
	//     GetMoveType() = 6   MOVETYPE_VPHYSICS
	//     GetModel()    = models/combine_helicopter/helicopter_bomb01.mdl
	//
	// while the SAME script under this fork left the entity at
	//
	//     solid = 0   SOLID_NONE
	//
	// lua/entities/sent_ball.lua never calls SetSolid or SetMoveType (its
	// Initialize only sets the model, rebuilds physics, and picks a size/colour),
	// and base_anim.lua -- which in GMod supplies ENT.Type = "anim" and nothing
	// else of substance -- has no SetSolid either.  So those two values come from
	// GMod's ENGINE, keyed off the entity's type:
	//
	//     BaseClasses["anim"] = "base_anim"        (scripted_ents.lua:13)
	//
	// A SOLID_NONE entity also cannot take part in physics collisions, which is
	// not what a bouncy ball is for; ent_nyan_bomb escaped this only because its
	// own Initialize() happens to call SetMoveType/SetSolid explicitly.
	//
	// STATUS: this restores GMod's documented-by-measurement state for an "anim"
	// SENT.  It is NOT a confirmed fix for the separate "invisible sent_ball"
	// report -- diagnostics showed the server reaching FL_EDICT_PVSCHECK (i.e.
	// deciding to transmit) for sent_ball either way, while the client still never
	// created the entity.  That report is unresolved; see AGENTS.md 9.17.
	//
	// Applied BEFORE the Initialize dispatch so a script that overrides either
	// value still wins.  Only the types this fork can back are handled; a script
	// that declares no Type at all keeps the inherited behaviour.
#ifndef CLIENT_DLL
	{
		const char *pszType = NULL;

		if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) )
		{
			lua_getref( L, m_nTableReference );
			if ( lua_istable( L, -1 ) )
			{
				luasrc_PushScriptField( L, -1, "Type" );
				if ( lua_type( L, -1 ) == LUA_TSTRING )
					pszType = lua_tostring( L, -1 );
			}
			lua_pop( L, 2 );
		}

		// GMod also derives the base class from the type, and a SENT that only
		// says DEFINE_BASECLASS("base_anim") while omitting ENT.Type still ends
		// up anim-typed there (base_anim.lua sets it).  This fork maps base_anim
		// onto prop_scripted, which carries no Type, so fall back to the declared
		// base name -- otherwise sent_ball, whose own file sets no Type, would
		// miss these defaults even though it is plainly an anim entity.
		if ( pszType == NULL )
		{
			if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) )
			{
				lua_getref( L, m_nTableReference );
				if ( lua_istable( L, -1 ) )
				{
					luasrc_PushScriptField( L, -1, "Base" );
					if ( lua_type( L, -1 ) == LUA_TSTRING &&
					     !Q_stricmp( lua_tostring( L, -1 ), "base_anim" ) )
						pszType = "anim";
				}
				lua_pop( L, 2 );
			}
		}

		if ( pszType != NULL && !Q_stricmp( pszType, "anim" ) )
		{
			if ( GetSolid() == SOLID_NONE )
				SetSolid( SOLID_VPHYSICS );

			if ( GetMoveType() == MOVETYPE_NONE )
				SetMoveType( MOVETYPE_VPHYSICS );
		}
	}
#endif

	// HL2SB GMod compat: Initialize dispatches at SPAWN, not at create.  GMod's
	// ents.Create does not run ENT:Initialize -- scripts configure the entity
	// (SetModel/SetPos) AFTER ents.Create and Initialize runs inside :Spawn(),
	// where PhysicsInit can actually build vphysics against the model.  The
	// create-time binding call passes bCallInitialize=false; the Spawn call
	// keeps the default true.
	if ( bCallInitialize )
	{
		BEGIN_LUA_CALL_ENTITY_METHOD( "Initialize" );
		END_LUA_CALL_ENTITY_METHOD( 0, 0 );
	}
#endif
}

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB: is this render group drawn in the translucent pass?
//-----------------------------------------------------------------------------
static bool CBaseScripted_IsTranslucentGroup( RenderGroup_t group )
{
	return group == RENDER_GROUP_TRANSLUCENT_ENTITY
		|| group == RENDER_GROUP_VIEW_MODEL_TRANSLUCENT
		|| group == RENDER_GROUP_TWOPASS;
}

//-----------------------------------------------------------------------------
// HL2SB: Entity:GetRenderGroup() answers the script's ENT.RenderGroup field.
//
// Reading it also decides whether the entity is put into the translucent render
// list, which is what makes a sprite-drawn entity (npc_verity: no model at all,
// drawn with render.SetMaterial + render.DrawQuadEasy) end up in a pass that
// draws it.
//-----------------------------------------------------------------------------
RenderGroup_t CBaseScripted::GetRenderGroup( void )
{
#ifdef LUA_SDK
	if ( L != NULL && !m_bLuaRenderGroupRead && m_nTableReference >= 0 )
	{
		lua_getref( L, m_nTableReference );

		if ( lua_istable( L, -1 ) )
		{
			m_bLuaRenderGroupRead = true;
			m_nLuaRenderGroup = -1;

			// Protected read: the table can answer through an __index metamethod,
			// and an error raised there from C is not protected by anything.
			luasrc_PushScriptField( L, -1, "RenderGroup" );

			if ( lua_isnumber( L, -1 ) )
			{
				const int nGroup = (int)lua_tonumber( L, -1 );

				if ( nGroup >= 0 && nGroup < RENDER_GROUP_COUNT )
					m_nLuaRenderGroup = nGroup;
			}

			lua_pop( L, 1 );
		}

		lua_pop( L, 1 );
	}

	if ( m_nLuaRenderGroup >= 0 )
		return (RenderGroup_t)m_nLuaRenderGroup;
#endif

	return BaseClass::GetRenderGroup();
}

int CBaseScripted::DrawModel( int flags )
{
#ifdef LUA_SDK
	// HL2SB: the script decides which of the draw hooks runs, exactly as GMod's
	// wiki describes it -- ENTITY:RenderOverride() first, then the render group
	// picks between ENTITY:DrawTranslucent() and ENTITY:Draw().
	//
	// Dispatching "Draw" alone was enough for entities that draw a model, because
	// GMod's ENT:Draw() REPLACES the model draw and the inherited implementation
	// asks for the model explicitly.  It is not enough for a sprite entity that
	// defines ONLY DrawTranslucent: npc_verity draws itself with
	// render.SetMaterial + render.DrawQuadEasy and has no model at all, so it fell
	// through to the inherited ENT:Draw() -> self:DrawModel() -> nothing to draw,
	// and the bot was invisible while behaving perfectly.
	//
	// The method's PRESENCE decides, because the dispatch macro cannot tell "no
	// such method" from "the method returned nil".  An explicit `false` is
	// honoured as "also draw the model".
	bool bHasOverride = false;
	bool bHasTranslucent = false;
	bool bHasDraw = false;

	if ( L != NULL && m_nTableReference >= 0 )
	{
		lua_getref( L, m_nTableReference );
		if ( lua_istable( L, -1 ) )
		{
			// Protected: this runs from DrawModel() on every frame, and the script
			// table can answer through an __index metamethod -- an error there used
			// to reach lua_atpanic and abort the process with an empty traceback.
			bHasOverride = luasrc_PushScriptField( L, -1, "RenderOverride" );
			lua_pop( L, 1 );

			bHasTranslucent = luasrc_PushScriptField( L, -1, "DrawTranslucent" );
			lua_pop( L, 1 );

			bHasDraw = luasrc_PushScriptField( L, -1, "Draw" );
			lua_pop( L, 1 );
		}
		lua_pop( L, 1 );
	}

	const char *pszFunc = NULL;

	if ( bHasOverride )
		pszFunc = "RenderOverride";
	else if ( CBaseScripted_IsTranslucentGroup( GetRenderGroup() ) && bHasTranslucent )
		pszFunc = "DrawTranslucent";
	else if ( bHasDraw )
		pszFunc = "Draw";

	if ( pszFunc != NULL )
	{
		// HL2SB diagnostic: InfoMsg, not WarnOnce -- the one-shot budget was
		// already spent whenever this first fired, so "no line" was unreadable.
		// Bounded by count instead: 40 lines across all classes is plenty to
		// tell a "has no model" bomb from an "ENT:Draw never ran" one.
		static int s_nDrawReports = 0;
		if ( s_nDrawReports < 40 )
		{
			++s_nDrawReports;
			luasrc_LuaInfoMsgF( "[HL2SB] script DrawModel '%s': hook=%s group=%d modelIndex=%d\n",
				GetClassname(), pszFunc, (int)GetRenderGroup(), GetModelIndex() );
		}

		BEGIN_LUA_CALL_ENTITY_METHOD( pszFunc );
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
		// HL2SB: probe.  A Lua nextbot's client entity has to be a
		// C_NextBotCombatCharacter (that is where the RenderGroup/DrawModel hooks
		// live); if it shows up here instead, it was built as a plain scripted
		// entity and the whole nextbot draw path is unreachable.
		static int s_nScriptedReports = 0;

		if ( s_nScriptedReports < 40 )
		{
			++s_nScriptedReports;
			luasrc_LuaWarnMsgF( "[HL2SB] CLIENT CBaseScripted created: classname='%s' networkedScriptClass='%s'",
				GetClassname(),
				( m_iScriptedClassname.Get() != NULL ) ? m_iScriptedClassname.Get() : "(none)" );
		}

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
	// HL2SB GMod compat: a scripted entity reaches the client even with no model.
	//
	// CBaseEntity::UpdateTransmitState() drops anything without a model index or
	// model name unless it carries EFL_FORCE_CHECK_TRANSMIT, and plenty of Lua
	// entities are model-less by design: windgrin_npc's attack spawns
	// ent_windgrin_blaster / ent_windgrin_throw, which draw themselves from a script
	// the client never got to run because the entity was never sent -- the attack
	// simply did nothing on screen.  (CLuaNextBot::Spawn() sets the same flag for
	// nextbots.)  GMod transmits scripted entities regardless of a model.
	AddEFlags( EFL_FORCE_CHECK_TRANSMIT );

	InitScriptedEntity();
#endif
}

void CBaseScripted::Precache( void )
{
	BaseClass::Precache();

	// HL2SB: NOT dispatching ENT:Precache() and NOT precaching ENT.Model here.
	// GMod scripted entities name their model for the first time inside
	// ENT:Initialize() and declare no ENT.Model (lua/entities/sent_ball.lua does
	// exactly that), so the model does not exist yet at this point.  The load is
	// handled on demand where GMod handles it: UTIL_SetModel() precaches a model
	// that was never registered instead of falling through to an Error() that is
	// compiled out in release (see the comment there).
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
#include "world.h"
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
static ConVar hl2sb_physicscollide_debug(
	"hl2sb_physicscollide_debug", "0", FCVAR_ARCHIVE,
	"Log every ENT:PhysicsCollide dispatch (entity hit, speed, contact point)" );

//-----------------------------------------------------------------------------
// HL2SB GMod compat (2026-09-21): ENT:Use( activator, caller ).
//
// GMod routes the player's +use to every scripted entity: the engine selects
// use targets among entities advertising FCAP_IMPULSE_USE and calls their use
// function, which GMod's system dispatches to ENT:Use.  This fork never called
// SetUse() on scripted entities, so the Nuke Pack's arming flow
// (mk-82_sent_he_missile/init.lua ENT:Use -> self.isarmed = 1) never ran and
// the bombs could not be armed, hence never detonated.
//-----------------------------------------------------------------------------
int CBaseScripted::ObjectCaps( void )
{
	return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE;
}

void CBaseScripted::UseHandler( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
#ifdef LUA_SDK
	if ( L == NULL || m_nTableReference < 0 )
		return;

	// HL2SB: one line, once per classname -- the next "E does nothing" round
	// is settled by this line's presence or absence (dispatch vs script).
	static CUtlVector<CUtlString> s_UseLogged;
	const char *pszClass = GetClassname();	// server: the real scripted classname
	bool bLogged = false;
	for ( int i = 0; i < s_UseLogged.Count(); ++i )
	{
		if ( !Q_stricmp( s_UseLogged[i], pszClass ) ) { bLogged = true; break; }
	}
	if ( !bLogged && s_UseLogged.Count() < 16 )
	{
		s_UseLogged.AddToTail( pszClass );
		luasrc_LuaInfoMsgF( "[HL2SB] Use dispatched to '%s' (activator %s)\n",
			pszClass, pActivator ? pActivator->GetClassname() : "<none>" );
	}

	lua_getref( L, m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}

	if ( !luasrc_PushScriptField( L, -1, "Use" ) )
	{
		// GMod: an entity without an ENT:Use simply ignores +use.
		// PushScriptField pushed a nil; drop it and the table.
		lua_pop( L, 2 );
		return;
	}

	// stack: table, Use
	lua_pushvalue( L, -3 );					// self
	if ( pActivator != NULL )
		lua_pushentity( L, pActivator );
	else
		lua_pushnil( L );
	if ( pCaller != NULL )
		lua_pushentity( L, pCaller );
	else
		lua_pushnil( L );
	// GMod's full signature is Use( activator, caller, useType, value )
	// (wiki ENTITY:Use); the nukepack scripts read only the first two.
	lua_pushinteger( L, (int)useType );
	lua_pushnumber( L, value );

	luasrc_pcall( L, 5, 0, 0 );
	lua_pop( L, 1 );						// the entity table
#endif
}

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

	// HL2SB: GMod pushes the world entity (worldspawn) when the other side of
	// the collision is the world -- pEntities[nOther] is NULL for world hits.
	// Pushing NULL made data.HitEntity a NULL handle userdata, so addons that
	// check `if IsValid(ent) and ent:IsWorld()` (the C4 sticker, for example)
	// fell through to the wrong branch and the entity never stuck to surfaces.
	CBaseEntity *pHitEntity = pEvent->pEntities[ nOther ];
	if ( pHitEntity == NULL )
	{
		pHitEntity = GetWorldEntity();
	}

	Vector vecHitPos = vec3_origin;
	Vector vecHitNormal = vec3_origin;
	if ( pEvent->pInternalData != NULL )
	{
		pEvent->pInternalData->GetContactPoint( vecHitPos );
		pEvent->pInternalData->GetSurfaceNormal( vecHitNormal );
		// HL2SB: push the RAW vphysics normal -- it IS GMod's convention.  For a
		// C4 landing on the floor it points INTO the surface ((0,0,-1)), and the
		// addons are written against that: stock VectorAngles maps (0,0,-1) to
		// pitch 90, cod_c4 adds 270 -> 360 == flat face-up, and the SetPos
		// offset seats the model ON the surface.  An earlier fix of mine negated
		// it for index == 0 "to match GMod" -- that was wrong: with (0,0,1) the
		// chain became pitch 270 + 270 = 540, the RotateAroundAxis basis rebuild
		// rolled the model 180 (c4check: ang=(0,-164,180)) and the planted C4
		// sat upside-down/buried -- invisible.  Reverted; keep the raw sign.
	}

	// HL2SB: a scripted projectile that "sticks" by disabling its physics motion
	// (cod_c4) looks identical to one whose stick logic silently never ran if
	// nobody can see the dispatches.  With this cvar on, every collision prints:
	// repeated dispatches for the same entity = it is still moving (the Lua side
	// never froze it); a single dispatch then silence = the stick took effect.
	if ( hl2sb_physicscollide_debug.GetBool() )
	{
		Msg( "[HL2SB][PhysicsCollide] %s#%d hit '%s' speed %.1f pos (%.1f %.1f %.1f) normal (%.2f %.2f %.2f) phys=%s\n",
			GetClassname(), entindex(),
			pHitEntity ? pHitEntity->GetClassname() : "<NULL>",
			pEvent->collisionSpeed,
			vecHitPos.x, vecHitPos.y, vecHitPos.z,
			vecHitNormal.x, vecHitNormal.y, vecHitNormal.z,
			pEvent->pObjects[ index ] ? "ok" : "NULL" );
	}

	BEGIN_LUA_CALL_ENTITY_METHOD( "PhysicsCollide" );
		{
			lua_newtable( L );

			lua_pushstring( L, "HitEntity" );
			lua_pushentity( L, pHitEntity );
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

