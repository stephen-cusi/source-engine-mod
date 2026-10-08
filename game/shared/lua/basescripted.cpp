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
#include "ltakedamageinfo.h"	// HL2SB (2026-09-29): lua_pushdamageinfo for the OnTakeDamage dispatch
#include "lvphysics_interface.h"
#include "mathlib/lvector.h"
#include "utlstring.h"	// HL2SB: CUtlString for the client draw-probe classnames
#ifdef CLIENT_DLL
#include "clientleafsystem.h"	// HL2SB (2026-10-02): re-report the render group after the script class binds
#endif
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

	// HL2SB (2026-10-02): belt+braces - the CNetworkString ctor already zeroes
	// its buffer; this keeps the invariant explicit against future custom
	// initialization paths.  The 2026-10-02 "cod-c4 binding gmod_hands" log
	// turned out to be a classmap-constant DISPLAY bug (GetClassname() reads
	// the first registered SENT's name pre-bind), not stale slot data.
	m_iScriptedClassname.GetForModify()[0] = '\0';

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
			// HL2SB (2026-09-21, the "ball shows the C4 model" bug): look the
			// script up by the SCRIPTED classname.  The client's GetClassname()
			// collapses every scripted entity to the FIRST-registered SENT
			// ("cod-c4") through the classmap's reverse lookup, so binding by
			// it gave EVERY scripted entity the cod_c4 client table on the
			// client -- its ENT:Draw is self:DrawModel(), which is why the
			// ball rendered a bomb model and the nyan grenade rendered the
			// suitcase instead of their sprites.
#ifdef CLIENT_DLL
			const char *pszScriptClass = GetScriptedClassname();
#else
			const char *pszScriptClass = GetClassname();
#endif
			if ( pszScriptClass == NULL || pszScriptClass[0] == 0 )
				pszScriptClass = GetClassname();
			lua_pushstring( L, pszScriptClass );
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

static int HL2SB_EntityTableIsValid (lua_State *L)
{
	// Upvalue 1: the entity this script table belongs to (see the seeding in
	// InitScriptedEntity).  Same contract as the metatable IsValid: a NULL
	// (removed / stale handle) entity is not valid.  toanimating fallback
	// because the seeding pushes through the animating userdata.
	CBaseEntity *pEntity = lua_toentity( L, lua_upvalueindex( 1 ) );
	if ( pEntity == NULL )
		pEntity = lua_toanimating( L, lua_upvalueindex( 1 ) );
	lua_pushboolean( L, pEntity != NULL );
	return 1;
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
	// HL2SB (2026-10-04) GMod-faithful bind: fresh (no ref yet) loads the class
	// table; a re-run merges it into an existing instance table.  The re-run is
	// the healed path for the burnable one-shot the OnDataChanged gate used to
	// have (a pre-bind Lua field write allocates the auto-instance table via
	// CBaseAnimating___newindex and used to veto the bind forever) -- GMod's
	// bind is a re-runnable content copy (scripted_ents.Get(name, retval)), so
	// the merge + reseed below is the fork-idiomatic equivalent.
	const bool bFreshBind = ( m_nTableReference < 0 );

	if ( bFreshBind )
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

		// HL2SB GMod compat (2026-09-24): ENT.ClassName -- GMod's engine stamps
		// the class name onto every scripted table, and addon scripts read it
		// (e.g. a grenade SWEP calls owner:StripWeapon( self.ClassName ) when it
		// runs dry).  Without the field that read is nil and the call errors.
		if ( lua_istable( L, -1 ) )
		{
			lua_pushstring( L, className );
			lua_setfield( L, -2, "ClassName" );
		}

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
				/* NOTE: no pop here.  The pcall was requested with ZERO results,
				** so table.merge's return never lands on the stack - success and
				** the error path (no nil placeholder at nresults=0) both leave
				** exactly [].  Popping "the merge result" here underflowed the
				** shared Lua stack and aborted at map load (2026-10-08).  The
				** only extra slot this branch pushes is the getref below, and
				** the bFreshBind else-branch pops that one. */
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

		// put the instance table back on top for the shared seeding below
		lua_getref( L, m_nTableReference );
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
	//
	// HL2SB (2026-10-04) shared by BOTH branches: the fresh bind's class table
	// and the healed merge's instance table land here, and an instance table
	// created by a pre-bind field write has none of the seeds the fresh branch
	// used to install -- GMod's scripted_ents.Get re-copies them on every
	// re-run, so re-seed here whenever the Entity field is missing.
	bool bSeeded = false;
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Entity" );
		bSeeded = !lua_isnil( L, -1 );
		lua_pop( L, 1 );
	}
	if ( lua_istable( L, -1 ) && !bSeeded )
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

		// HL2SB GMod compat (2026-10-04): self:IsValid() on the SCRIPT TABLE.
		// Entity-file timer closures capture the table as self (the portalgun
		// projectile does timer.Simple(.01, function() if self:IsValid() ...
		// end)), and indexing the table resolved to a boolean somewhere down
		// its chain - "attempt to call a boolean value (method 'IsValid')".
		// Seed the method form, with the entity bound as the upvalue; the
		// answer matches the metatable's IsValid (toentity != NULL).
		lua_pushanimating( L, this );
		lua_pushcclosure( L, HL2SB_EntityTableIsValid, 1 );
		lua_setfield( L, -2, "IsValid" );

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

		// HL2SB GMod compat (2026-10-08): NetworkVarElement gets the same
		// treatment -- env_skypaint packs its three star values into one
		// Angle slot through self:NetworkVarElement("Angle", 0, "p", ...).
		// The implementation is a Lua shim (HL2SB_EntityNetworkVarElement);
		// absent global, this is a no-op like the others.
		lua_getglobal( L, "HL2SB_EntityNetworkVarElement" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_setfield( L, -2, "NetworkVarElement" );
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
	else if ( lua_istable( L, -1 ) && !bFreshBind )
	{
		// healed/merge path on an already-seeded table: re-stamp ClassName so
		// the OnDataChanged gate sees this class as bound even when the merge
		// source was a different registration (addon-reload semantics).
		lua_pushstring( L, className );
		lua_setfield( L, -2, "ClassName" );
	}

	if ( bFreshBind )
	{
		// HL2SB: < 0, not == LUA_NOREF.  luaL_ref() returns LUA_REFNIL (-1) when
		// entity.get() yielded no table (a classname with no lua/entities script),
		// and that value is not LUA_NOREF (-2) -- the ref is taken only here, in
		// the fresh branch, AFTER the shared seeding above has run.
		m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
	}
	else
	{
		// HL2SB (2026-10-08): the merge/healed branch above pushed the instance
		// table (lua_getref) and nothing popped it - one leaked ENT-instance
		// table per InitScriptedEntity re-run, i.e. per Spawn of an
		// already-bound entity.  The HUD stack probe fingerprinted it exactly:
		// a table whose first keys are ENT method names (VPhysicsUpdate,
		// StartTouch, OnRemove, NetworkVarNotify) sitting under the first
		// ShouldDraw of a session.  The fresh branch's luaL_ref pops its table;
		// this branch has to as well.
		lua_pop( L, 1 );
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
#if defined( LUA_SDK ) && defined( CLIENT_DLL )
	// HL2SB (2026-10-03): the per-entity clip plane (Entity:SetRenderClipPlane /
	// SetRenderClipPlaneEnabled) applies for the whole draw - the portalgun's
	// player clone must be cut at the portal plane no matter which Lua draw
	// path takes over below.  Scope object, because the Lua dispatch has
	// several return paths.  Local extern on purpose: waf has no header
	// dependency propagation (the definition lives in lc_baseanimating.cpp).
	Vector vecClipNormal;
	float flClipDist = 0.0f;
	extern bool HL2SB_GetEntityRenderClipPlane( C_BaseEntity *pEntity, Vector &outNormal, float &outDist );
	const bool bClip = HL2SB_GetEntityRenderClipPlane( this, vecClipNormal, flClipDist );
	struct HL2SB_ClipPlaneScope
	{
		bool m_bActive;
		HL2SB_ClipPlaneScope( bool bActive, const Vector &vecNormal, float flDist ) : m_bActive( bActive )
		{
			if ( !m_bActive )
				return;
			CMatRenderContextPtr pRenderContext( materials );
			Vector4D vecPlane;
			VectorCopy( vecNormal, vecPlane.AsVector3D() );
			vecPlane.w = flDist;
			pRenderContext->PushCustomClipPlane( vecPlane.Base() );
		}
		~HL2SB_ClipPlaneScope()
		{
			if ( !m_bActive )
				return;
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PopCustomClipPlane();
		}
	} clipScope( bClip, vecClipNormal, flClipDist );
#endif

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

	// HL2SB: bind on CREATED and RETRY on later datatable updates -- the
	// networked script class can arrive after the create notification.
	if ( updateType == DATA_UPDATE_CREATED || updateType == DATA_UPDATE_DATATABLE_CHANGED )
	{
		if ( updateType == DATA_UPDATE_CREATED )
		{
			// HL2SB: probe.  A Lua nextbot's client entity has to be a
			// C_NextBotCombatCharacter (that is where the RenderGroup/DrawModel hooks
			// live); if it shows up here instead, it was built as a plain scripted
			// entity and the whole nextbot draw path is unreachable.
			static int s_nScriptedReports = 0;

			if ( s_nScriptedReports < 6 )
			{
				++s_nScriptedReports;
				// HL2SB (2026-10-08): the first field is the PRE-BIND
				// classname and reads as the classmap constant (the first
				// registered scripted class -- typically "cod-c4" with this
				// addon set) until the networked name lands and SetClassname
				// fixes it below; that collapse is a known display quirk,
				// not the entity's identity.  The networked script class is
				// the real one.  Cap lowered: this fired per scripted entity
				// and users read the collapsed constant as addon pollution.
				luasrc_LuaWarnMsgF( "[HL2SB] CLIENT scripted entity created (classmap name='%s', real class='%s')",
					GetClassname(),
					( m_iScriptedClassname.Get() != NULL ) ? m_iScriptedClassname.Get() : "(syncing)" );
			}
		}

		// HL2SB: the networked script class must have ARRIVED before binding
		// (GMod loads the entity only once its class name is on the wire).
		// Get() never returns NULL -- an empty string means "not synced yet"
		// and must NOT fall through to GetClassname(): that is the classmap
		// constant, i.e. the first registered scripted class, and binding it
		// burns the wrong name via SetClassname below.
		//
		// HL2SB (2026-10-04) GMod-faithful gate: bind = "this entity's table
		// carries this class", retried on every update.  The old one-shot
		// (m_nTableReference == LUA_NOREF) was burnable: any Lua field write
		// between entity creation and OnDataChanged allocates the auto-instance
		// table (CBaseAnimating___newindex), consumed the one-shot, and
		// permanently vetoed the bind -- the SP gmod_hands regression (the
		// entity rendered by the world pass under the classmap name while
		// hands:DrawModel no-oped).  GMod's bind is a re-runnable content copy
		// (scripted_ents.Get(name, retval)); mirror that by testing the
		// ClassName stamp the bind installs.  A plain lua_getfield compare --
		// no protected call, so the 0080ddd6 nil-fill convention is untouched.
		bool bBoundToClass = false;
		if ( m_nTableReference >= 0 && L != NULL && m_iScriptedClassname.Get()[0] != '\0' )
		{
			lua_getref( L, m_nTableReference );						// [t]
			if ( lua_istable( L, -1 ) )
			{
				lua_getfield( L, -1, "ClassName" );					// [t][s]
				const char *pszBound = lua_tostring( L, -1 );
				bBoundToClass = ( pszBound != NULL &&
					Q_stricmp( pszBound, m_iScriptedClassname.Get() ) == 0 );
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 );
		}

		if ( m_iScriptedClassname.Get()[0] != '\0' &&
			 !bBoundToClass && m_nTableReference != LUA_REFNIL )
		{
			SetClassname( m_iScriptedClassname.Get() );

			// a pre-bind GetRenderGroup (AddToLeafSystem runs during
			// PostDataUpdate, before OnDataChanged) may have latched the
			// empty-table answer; let it read again now that the instance
			// table will carry ENT.RenderGroup
			m_bLuaRenderGroupRead = false;

			InitScriptedEntity();

			// HL2SB (2026-10-02): the render group may have been cached from the
			// pre-bind fallback answer - AddToLeafSystem runs before the Lua
			// table exists, and GetRenderGroup() only reads ENT.RenderGroup once
			// the table is bound.  Re-report now so ENT.RenderGroup =
			// RENDERGROUP_OTHER (the gmod_hands entity must never be drawn by
			// the world pass; GM:PostDrawViewModel owns its single draw) takes
			// effect immediately instead of waiting for the next GetFxBlend.
			if ( GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
			{
				ClientLeafSystem()->SetRenderGroup( GetRenderHandle(), GetRenderGroup() );
				ClientLeafSystem()->RenderableChanged( GetRenderHandle() );
			}
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

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB GMod compat (2026-10-08): the map-lifecycle dispatches.  GMod gives
// every scripted entity ENTITY:KeyValue / ENTITY:AcceptInput /
// ENTITY:UpdateTransmitState; this fork never did, so a map-placed scripted
// entity (gm_construct's env_skypaint) could not receive its BSP keyvalues
// and its colours never reached the client sky proxy.
//
// GMod lifecycle note: the instance table (and therefore SetupDataTables and
// the networked defaults) exists BEFORE map keyvalues are applied, so an
// entity script sees the defaults and the keyvalues replace them.  This
// engine binds at Spawn instead, which would leave env_skypaint's KeyValue
// with no instance at all -- so KeyValue forces the create-time bind
// (bCallInitialize=false, exactly the ents.Create path); the later Spawn
// bind sees the seeded table and does not re-run SetupDataTables, so the
// map values survive.
//-----------------------------------------------------------------------------

bool CBaseScripted::KeyValue( const char *szKeyName, const char *szValue )
{
#ifdef LUA_SDK
	if ( L != NULL && m_nTableReference < 0 )
	{
		// nextbots bind through CLuaNextBot::LoadNextBotScript instead
		// (same guard as the bFreshBind branch in InitScriptedEntity).
		char className[ 255 ];
		const char *pszClass = GetClassname();
		Q_strncpy( className, pszClass != NULL ? pszClass : "", sizeof( className ) );
		Q_strlower( className );
		if ( !IsLuaNextBot( className ) )
		{
			InitScriptedEntity( false );
		}
	}

	if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) )
	{
		BEGIN_LUA_CALL_ENTITY_METHOD( "KeyValue" );
		lua_pushstring( L, szKeyName );
		lua_pushstring( L, szValue != NULL ? szValue : "" );
		END_LUA_CALL_ENTITY_METHOD( 2, 1 );
		// the macro leaves one result (or the nil placeholder) on top
		bool bHandled = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		if ( bHandled )
			return true;
	}
#endif
	return BaseClass::KeyValue( szKeyName, szValue );
}

bool CBaseScripted::AcceptInput( const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, variant_t Value, int outputID )
{
#ifdef LUA_SDK
	if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) )
	{
		BEGIN_LUA_CALL_ENTITY_METHOD( "AcceptInput" );
		// GMod's ENTITY:AcceptInput( name, activator, caller, data ) -- the
		// input NAME is the first argument.  Pushing only the three entity
		// arguments shifted every name into the activator slot: env_skypaint's
		// SetNetworkVarsFromMapInput never saw a "Set<Var>" name, answered
		// false, and the ent_fire input fell through to the data-desc lookup
		// that has no such input -- silently dropped.
		lua_pushstring( L, szInputName != NULL ? szInputName : "" );
		if ( pActivator != NULL )
			CBaseEntity::PushLuaInstanceSafe( L, pActivator );
		else
			lua_pushnil( L );
		if ( pCaller != NULL )
			CBaseEntity::PushLuaInstanceSafe( L, pCaller );
		else
			lua_pushnil( L );
		const char *pszData = Value.String();
		lua_pushstring( L, pszData != NULL ? pszData : "" );
		END_LUA_CALL_ENTITY_METHOD( 4, 1 );
		bool bHandled = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		if ( bHandled )
			return true;
	}
#endif
	return BaseClass::AcceptInput( szInputName, pActivator, pCaller, Value, outputID );
}

int CBaseScripted::UpdateTransmitState( void )
{
#ifdef LUA_SDK
	if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) )
	{
		BEGIN_LUA_CALL_ENTITY_METHOD( "UpdateTransmitState" );
		END_LUA_CALL_ENTITY_METHOD( 0, 1 );
		int iScript = -1;
		if ( lua_isnumber( L, -1 ) )
			iScript = ( int )lua_tointeger( L, -1 );
		lua_pop( L, 1 );
		// GMod Enums/TRANSMIT: TRANSMIT_ALWAYS = 0, TRANSMIT_NEVER = 1,
		// TRANSMIT_PVS = 2 (wiki Enums/TRANSMIT).
		if ( iScript == 0 )
			return SetTransmitState( FL_EDICT_ALWAYS );
		if ( iScript == 1 )
			return SetTransmitState( FL_EDICT_DONTSEND );
		if ( iScript == 2 )
			return SetTransmitState( FL_EDICT_PVSCHECK );
	}
#endif
	return BaseClass::UpdateTransmitState();
}
#endif

#ifdef CLIENT_DLL
void CBaseScripted::ClientThink()
{
#ifdef LUA_SDK
	// HL2SB GMod parity (2026-09-24, the silent nuke client visuals): GMod
	// dispatches ENT:Think on the CLIENT too - nukepack's cl_init defines
	// exactly that name (the blast-wave sound cues, and client visual scripts
	// generally), never "ClientThink", so this dispatch never matched anything;
	// and the schedule was one-shot (Spawn armed it once).  Re-arm for the next
	// tick BEFORE dispatching so an explicit NextThink() inside the script
	// still wins, then prefer GMod's "Think" name with a fallback to this
	// fork's older "ClientThink" spelling.
	SetNextClientThink( gpGlobals->curtime );

	if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) )
	{
		lua_getref( L, m_nTableReference );			// [table]
		if ( lua_istable( L, -1 ) )
		{
			bool bFn = luasrc_PushScriptField( L, -1, "Think" );	// [table][fn|nil]
			if ( !bFn )
			{
				lua_pop( L, 1 );
				bFn = luasrc_PushScriptField( L, -1, "ClientThink" );
			}
			if ( bFn )
			{
				lua_remove( L, -2 );					// [fn]
				lua_pushanimating( L, this );			// [fn][self]
				luasrc_pcall( L, 1, 0, 0 );
			}
			else
			{
				lua_pop( L, 2 );						// nil + table
			}
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
#endif
}
#endif

void CBaseScripted::Think()
{
#ifdef LUA_SDK
	// HL2SB GMod parity (2026-10-07): the Think RETURN value decides how the
	// next think is scheduled.  GMod's contract (wiki ENTITY:Think): return
	// true = "I used Entity:NextThink to override the next execution time"
	// (the script's own NextThink is honoured, and if it did not set one the
	// entity thinks again as soon as possible); return false/nil = the engine
	// resets the think to CurTime() + 0.2 (the stock 5-6 Hz cadence).  This
	// dispatch used to swallow the result and re-arm every tick regardless,
	// so a GMod grenade's NextThink(CurTime() + 0.1) was ignored and its
	// 0.1s-cadence logic (damage timer, AI sound respawn) ran at tick rate.
	BEGIN_LUA_CALL_ENTITY_METHOD( "Think" );
	END_LUA_CALL_ENTITY_METHOD( 0, 1 );

	bool bHonourScriptThink = lua_isboolean( L, -1 ) ? lua_toboolean( L, -1 ) != 0
													: false;
	lua_pop( L, 1 );

#ifndef CLIENT_DLL
	if ( !bHonourScriptThink )
	{
		SetNextThink( gpGlobals->curtime + 0.2f );
	}
	else if ( GetNextThink() <= gpGlobals->curtime )
	{
		// script returned true without a NextThink: think again ASAP
		SetNextThink( gpGlobals->curtime );
	}
#endif
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
// HL2SB GMod compat (2026-09-21): ENT:Use( activator, caller, useType, value ).
// GMod routes the player's +use to every scripted entity (wiki ENTITY:Use);
// this fork never called SetUse(), so E did nothing on all SENTs.
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

	lua_getref( L, m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}

	if ( !luasrc_PushScriptField( L, -1, "Use" ) )
	{
		// GMod: an entity without an ENT:Use simply ignores +use.
		lua_pop( L, 2 );
		return;
	}

	// stack: table, Use
	// HL2SB (2026-09-21): self MUST be the entity userdata -- the earlier
	// lua_pushvalue( L, -3 ) pushed whatever sat below the table (stack
	// garbage), so self.isarmed = 1 landed on a dead table and self:MakeNuke()
	// read as nil ('attempt to call a nil value (method MakeNuke)').
	lua_pushanimating( L, this );			// self: the entity, like every engine dispatch
	if ( pActivator != NULL )
		lua_pushentity( L, pActivator );
	else
		lua_pushnil( L );
	if ( pCaller != NULL )
		lua_pushentity( L, pCaller );
	else
		lua_pushnil( L );
	lua_pushinteger( L, (int)useType );
	lua_pushnumber( L, value );

	luasrc_pcall( L, 5, 0, 0 );
	lua_pop( L, 1 );						// the entity table
#endif
}

void CBaseScripted::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
#ifdef LUA_SDK
	// HL2SB (2026-09-28): GMod gates the default physics sound / dust path for
	// SCRIPTED entities behind the script's own "PhysicsSounds" field --
	// reference GMod win64 server.dll (): the vanilla-style
	// sound player (: PhysCollisionSound + dust + the material
	// 'X' check) only runs when the ENT table HAS that field.  sent_ball
	// defines no PhysicsSounds, so in GMod its ball bounces with nothing but
	// the balloon_pop_cute sound from its own Lua PhysicsCollide; our
	// unconditional BaseClass call added the solidmetal clank on every
	// bounce.  Presence is the whole check -- the value is never read, which
	// is also why no GMod addon in the wild sets it to anything but true.
	bool bDefaultPhysicsSounds = false;
	if ( L != NULL && m_nTableReference >= 0 )
	{
		lua_getref( L, m_nTableReference );
		if ( lua_istable( L, -1 ) )
		{
			bDefaultPhysicsSounds = luasrc_PushScriptField( L, -1, "PhysicsSounds" );
			lua_pop( L, 1 );
		}
		lua_pop( L, 1 );
	}

	if ( bDefaultPhysicsSounds )
		BaseClass::VPhysicsCollision( index, pEvent );
#else
	BaseClass::VPhysicsCollision( index, pEvent );
#endif

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
		luasrc_LuaInfoMsgF( "[HL2SB][PhysicsCollide] %s#%d hit '%s' speed %.1f pos (%.1f %.1f %.1f) normal (%.2f %.2f %.2f) phys=%s\n",
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

			// HL2SB GMod compat (2026-09-24): CollisionData.HitSpeed -- "the speed
			// at which the impact happened" (wiki Structures/CollisionData).  GMod
			// hands the script the RELATIVE velocity of the two bodies; a thrown
			// grenade addon gates its detonation on HitSpeed:LengthSqr(), so with
			// the field missing the whole PhysicsCollide body never ran.
			lua_pushstring( L, "HitSpeed" );
			lua_pushvector( L, pEvent->preVelocity[ index ] - pEvent->preVelocity[ nOther ] );
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

//-----------------------------------------------------------------------------
// Purpose: HL2SB GMod compat (2026-09-29): ENT:OnTakeDamage( damageInfo ).
//
// GMod's scripted entities receive every damage event -- bullets, blasts,
// util.BlastDamage -- through the CBaseEntity::TakeDamage funnel, and the
// dispatch of that event to the script lives in the scripted entity class.
// This fork's CBaseScripted never overrode it, so ENT:OnTakeDamage never ran
// anywhere: minecraft's TNT never ignited when shot, cod_c4 never detonated
// when shot, and the entities behaved like invulnerable statues (only the
// MC weapon's own Remove() made them disappear).
//
// GMod precedence: when the ENT table defines OnTakeDamage the SCRIPT is the
// handler -- the base health pipeline is not entered (the script's own
// TakePhysicsDamage / expiry logic owns the entity).  Without the script
// handler, the inherited CBaseEntity response runs unchanged.
//
// The lookup is a protected PushScriptField like every other dispatch here
// (an __index metamethod raising from C would abort the process -- the
// SCP-096 lesson in luamanager.h).
//-----------------------------------------------------------------------------
bool CBaseScripted::HasLuaOnTakeDamage()
{
#ifdef LUA_SDK
	if ( L == NULL || m_nTableReference < 0 )
		return false;
	lua_getref( L, m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}
	luasrc_PushScriptField( L, -1, "OnTakeDamage" );
	bool bHasHandler = lua_isfunction( L, -1 ) != 0;
	lua_pop( L, 2 );		// field + table
	return bHasHandler;
#else
	return false;
#endif
}


//-----------------------------------------------------------------------------
// HL2SB GMod compat (2026-09-29): bullet damage enters through
// DispatchTraceAttack -> CBaseEntity::TraceAttack, whose body gates on
// `if (m_takedamage)` -- and nothing ever sets m_takedamage on a scripted
// entity (GMod scripts opt into damage by DEFINING ENT:OnTakeDamage, not by
// calling SetDamage), so shotgun volleys landing square on a solid, correctly
// placed dirt block dispatched ZERO OnTakeDamage.  Proven this round: the
// block's server vphysics body is real (CONTENTS_SOLID, vcollide solidCount=1,
// AABB on the placed spot) yet 38 pellets produced no damage event.
// Open the gate for exactly the entities whose script defines OnTakeDamage:
// run the full base body (accumulator batching, AddMultiDamage, blood -- a
// SENT's BloodColor is DONT_BLEED so nothing extra appears) with the damage
// state temporarily accepted, then restore it.  No handler => untouched HL2
// behavior (DAMAGE_NO statue stays inert).
//-----------------------------------------------------------------------------
void CBaseScripted::TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator )
{
	if ( HasLuaOnTakeDamage() )
	{
		const int nOldTakedamage = m_takedamage;
		m_takedamage = DAMAGE_AIM;	// nonzero, non-events: run the whole base damage body
		BaseClass::TraceAttack( info, vecDir, ptr, pAccumulator );
		m_takedamage = nOldTakedamage;
		return;
	}

	BaseClass::TraceAttack( info, vecDir, ptr, pAccumulator );
}


//-----------------------------------------------------------------------------
// (entry for bullets: see the TraceAttack override above; explosions/ApplyMultiDamage
// reach TakeDamage directly)
//-----------------------------------------------------------------------------
int CBaseScripted::OnTakeDamage( const CTakeDamageInfo &info )
{
#ifdef LUA_SDK
	bool bHasHandler = HasLuaOnTakeDamage();

	if ( bHasHandler )
	{
		// lua_pushdamageinfo copies into the userdata; the script cannot
		// mutate the C event either way, the local only satisfies its
		// non-const signature.
		CTakeDamageInfo dmgInfo = info;
		BEGIN_LUA_CALL_ENTITY_METHOD( "OnTakeDamage" );
			lua_pushdamageinfo( L, dmgInfo );
		END_LUA_CALL_ENTITY_METHOD( 1, 0 );
		return 1;
	}
#endif

	return BaseClass::OnTakeDamage( info );
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

