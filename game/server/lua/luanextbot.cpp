//========== HL2SB - GMod compat ==========--
//
// Purpose: implementation of the Lua nextbot host (see luanextbot.h).
//
//===========================================================================//

#include "cbase.h"
#include "luanextbot.h"

#ifdef LUA_SDK

#include "luamanager.h"			// pulls the Lua C API in first: luasrclib.h uses
								// LUA_API / LUALIB_API without including lua.h itself
#include "luasrclib.h"
#include "lbaseanimating.h"
#include "lbaseentity_shared.h"
#include "ltakedamageinfo.h"
#include "mathlib/lvector.h"	// lua_pushvector / luaL_checkvector
#include "npcevent.h"			// animevent_t (HandleAnimEvent)
#include "lnavmesh.h"			// LuaNavMesh_Install

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// HL2SB: the entity factories for Lua nextbots.  Same shape as
// m_EntityFactoryDatabase in basescripted.cpp: CEntityFactory<T> installs
// itself into EntityFactoryDictionary() (that is what makes
// CreateEntityByName(name) work), and this dictionary only exists so the
// factories can be deleted when the Lua state is torn down.
//-----------------------------------------------------------------------------
static CUtlDict< CEntityFactory<CLuaNextBot>*, unsigned short > s_LuaNextBotFactories;

// Defined further down; installing the metatable methods is part of registering
// the first bot (the Lua state and its libraries are up by then).
static void InstallNextBotEntityMethods( void );

// Defined further down with the locomotion subclass it constructs.
static NextBotGroundLocomotion *CreateLuaNextBotLocomotion( INextBot *pBot );

void RegisterLuaNextBot( const char *pszClassname )
{
	if ( !pszClassname || !pszClassname[0] )
		return;

	if ( EntityFactoryDictionary()->FindFactory( pszClassname ) )
	{
		// A stock class or an already registered script owns this name: a Lua
		// script never silently replaces engine content.
		return;
	}

	if ( s_LuaNextBotFactories.Find( pszClassname ) != s_LuaNextBotFactories.InvalidIndex() )
		return;

	CEntityFactory<CLuaNextBot> *pFactory = new CEntityFactory<CLuaNextBot>( pszClassname );

	s_LuaNextBotFactories.Insert( pszClassname, pFactory );

	InstallNextBotEntityMethods();

	Msg( "[HL2SB] Lua nextbot registered: %s\n", pszClassname );
}

bool IsLuaNextBot( const char *pszClassname )
{
	return pszClassname && s_LuaNextBotFactories.Find( pszClassname ) != s_LuaNextBotFactories.InvalidIndex();
}

//-----------------------------------------------------------------------------
// HL2SB: say it out loud, ONCE per (class, callback).
//
// A dispatch the script table does not answer is this file's silent failure mode:
// BeginLuaCall() returns false, prints nothing, and the whole layer it drives
// never runs.  SCP-096 stood still with a completely clean log while entity.get()
// aliased base_nextbot to prop_scripted and BehaveStart was nowhere to be found.
//
// A lookup that RAISED is worse than one that found nothing -- it means the
// script's __index chain is broken -- and before luasrc_PushScriptField() it did
// not even print: it reached lua_atpanic, which aborted the process with an EMPTY
// traceback.  Both cases get a line, once per class per session, so a spawn storm
// cannot flood the log; hl2sb_nextbot_status remains the full inventory.
//-----------------------------------------------------------------------------
static bool LuaNextBot_IsCriticalCallback( const char *pszFunc )
{
	// Only the names whose absence costs behaviour.  The optional event handlers
	// (OnContact, OnNavAreaChanged, ...) are legitimately missing from most
	// scripts and are listed by hl2sb_nextbot_status instead of warned about.
	static const char *s_pCritical[] = {
		"BehaveStart", "BehaveUpdate", "RunBehaviour", "BodyUpdate",
		"Think", "Initialize", "Precache", "OnRemove",
	};

	for ( int i = 0; i < ARRAYSIZE( s_pCritical ); ++i )
	{
		if ( !Q_stricmp( s_pCritical[ i ], pszFunc ) )
			return true;
	}

	return false;
}

static void LuaNextBot_ReportLookupMiss( const char *pszClassname, const char *pszFunc, bool bErrored )
{
	if ( !bErrored && !LuaNextBot_IsCriticalCallback( pszFunc ) )
		return;

	static CUtlDict< int, int > s_Reported;
	static bool s_bTruncated = false;

	const char *pszClass = ( pszClassname != NULL ) ? pszClassname : "?";

	char szKey[ 256 ];
	Q_snprintf( szKey, sizeof( szKey ), "%s:%s", pszClass, pszFunc );

	if ( s_Reported.Find( szKey ) != s_Reported.InvalidIndex() )
		return;

	if ( s_Reported.Count() >= 128 )
	{
		if ( !s_bTruncated )
		{
			s_bTruncated = true;
			Warning( "[HL2SB] nextbot lookup report: 128 distinct (class, callback) pairs seen, muting the rest\n" );
		}
		return;
	}

	s_Reported.Insert( szKey, 1 );

	if ( bErrored )
	{
		// Red: this one lost a dispatch that SHOULD have run.
		luasrc_LuaErrorMsgF( "[HL2SB] nextbot '%s': reading ENT.%s RAISED (error and traceback "
							 "above) - every %s dispatch is lost", pszClass, pszFunc, pszFunc );
	}
	else
	{
		// Orange: a script is allowed not to define an optional hook.
		luasrc_LuaWarnMsgF( "[HL2SB] nextbot '%s': the script defines no ENT.%s - that dispatch "
							"is skipped (hl2sb_nextbot_status lists all of them)", pszClass, pszFunc );
	}
}

//-----------------------------------------------------------------------------
// The Lua call protocol for a nextbot's script table.
//
// ⚠️ The table test is not decoration.  m_nTableReference is CBaseEntity's member
// and is LUA_NOREF for an entity whose script table was never taken, and
// lua_getref() on a stale reference yields whatever sits at that registry index;
// lua_getfield() on that raised "attempt to index a number value" from an
// unprotected context, which aborted the process (AGENTS.md 5.4.1).
//-----------------------------------------------------------------------------
bool CLuaNextBot::BeginLuaCall( const char *pszFunc )
{
	if ( L == NULL || pszFunc == NULL )
		return false;

	if ( m_nTableReference < 0 )
		return false;

	lua_getref( L, m_nTableReference );

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	// HL2SB: a PROTECTED read.  The script table answers through an __index
	// metamethod, and lua_getfield() from C runs that metatable OUTSIDE any
	// protected call: an error in it reached lua_atpanic and aborted the process
	// with "attempt to call a nil value" and an EMPTY traceback -- which is how
	// every SCP-096 spawn killed the game.  luasrc_PushScriptField() catches the
	// error (and prints its traceback) while still leaving exactly one value, so
	// everything below keeps its stack shape.
	bool bLookupErrored = false;
	luasrc_PushScriptField( L, -1, pszFunc, &bLookupErrored );
	lua_remove( L, -2 );			// the script table

	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 1 );

		if ( bLookupErrored )
		{
			// Not the same as "the script does not define it": the metatable chain
			// itself is broken.  Counted apart so the two can be told apart.
			++m_nLookupErrors;
		}
		else
		{
			++m_nLuaCallMisses;		// silent by nature: see hl2sb_nextbot_status
		}

		LuaNextBot_ReportLookupMiss( GetClassname(), pszFunc, bLookupErrored );
		return false;
	}

	lua_pushanimating( L, this );	// self
	return true;
}

void CLuaNextBot::EndLuaCall( int nArgs )
{
	// nArgs does NOT count `self`, which BeginLuaCall() already pushed.
	// luasrc_pcall() Warnings (with a traceback) and pops the message itself, so
	// only the tally is ours to keep.
	if ( luasrc_pcall( L, nArgs + 1, 0, 0 ) != 0 )
		++m_nLuaErrors;
}

//-----------------------------------------------------------------------------
// The intention: base_nextbot's behaviour coroutine.
//-----------------------------------------------------------------------------
CLuaNextBotIntention::CLuaNextBotIntention( INextBot *bot ) : IIntention( bot )
{
	// ⚠️ This is what every callback below hangs off: m_me.Get() is the bot whose
	// script table gets BehaveStart/BehaveUpdate/the ENT:On* events.  It was left
	// NULL for a while, and the symptom is silent - the bot spawns, the NextBot
	// manager ticks it, and NOTHING ever moves, because every dispatch walked into
	// a NULL check and returned.
	//
	// INextBot is the bot itself here: CLuaNextBot inherits NextBotCombatCharacter,
	// which inherits INextBot (NextBot.h:27), so the downcast is the identity.
	m_me = (CLuaNextBot *)bot;
}

void CLuaNextBotIntention::Reset( void )
{
	IIntention::Reset();

	CLuaNextBot *pBot = m_me.Get();
	if ( pBot == NULL )
		return;

	// GMod: BehaveStart() creates the coroutine (base_nextbot's sv_nextbot.lua:8)
	// and the engine calls it once, when the bot is created.
	if ( pBot->BeginLuaCall( "BehaveStart" ) )
		pBot->EndLuaCall( 0 );
}

void CLuaNextBotIntention::Update( void )
{
	IIntention::Update();

	CLuaNextBot *pBot = m_me.Get();
	if ( pBot == NULL )
		return;

	// GMod: BehaveUpdate( interval ) resumes that coroutine every frame
	// (sv_nextbot.lua:23).
	if ( pBot->BeginLuaCall( "BehaveUpdate" ) )
	{
		++pBot->m_nBehaveUpdateCalls;
		lua_pushnumber( L, GetUpdateInterval() );
		pBot->EndLuaCall( 1 );
	}
}

#define LUA_NEXTBOT_EVENT0( func ) \
	CLuaNextBot *pBot = m_me.Get(); \
	if ( pBot == NULL ) return; \
	if ( pBot->BeginLuaCall( func ) ) pBot->EndLuaCall( 0 );

#define LUA_NEXTBOT_EVENT_ENTITY( func, ent ) \
	CLuaNextBot *pBot = m_me.Get(); \
	if ( pBot == NULL ) return; \
	if ( pBot->BeginLuaCall( func ) ) { lua_pushentity( L, ent ); pBot->EndLuaCall( 1 ); }

void CLuaNextBotIntention::OnStuck( void )						{ LUA_NEXTBOT_EVENT0( "OnStuck" ); }
void CLuaNextBotIntention::OnUnStuck( void )					{ LUA_NEXTBOT_EVENT0( "OnUnStuck" ); }
void CLuaNextBotIntention::OnIgnite( void )						{ LUA_NEXTBOT_EVENT0( "OnIgnite" ); }
void CLuaNextBotIntention::OnLeaveGround( CBaseEntity *entity )	{ LUA_NEXTBOT_EVENT_ENTITY( "OnLeaveGround", entity ); }
void CLuaNextBotIntention::OnLandOnGround( CBaseEntity *entity ){ LUA_NEXTBOT_EVENT_ENTITY( "OnLandOnGround", entity ); }
void CLuaNextBotIntention::OnEntitySight( CBaseEntity *subject ){ LUA_NEXTBOT_EVENT_ENTITY( "OnEntitySight", subject ); }
void CLuaNextBotIntention::OnEntitySightLost( CBaseEntity *subject ){ LUA_NEXTBOT_EVENT_ENTITY( "OnEntitySightLost", subject ); }

void CLuaNextBotIntention::OnContact( CBaseEntity *other, CGameTrace *result )
{
	// GMod's ENT:OnContact( ent ) hands the entity only.
	LUA_NEXTBOT_EVENT_ENTITY( "OnContact", other );
}

#undef LUA_NEXTBOT_EVENT0
#undef LUA_NEXTBOT_EVENT_ENTITY

void CLuaNextBotIntention::OnOtherKilled( CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	CLuaNextBot *pBot = m_me.Get();
	if ( pBot == NULL )
		return;

	if ( pBot->BeginLuaCall( "OnOtherKilled" ) )
	{
		CTakeDamageInfo lInfo = info;		// lua_pushdamageinfo wants an lvalue

		lua_pushentity( L, victim );
		lua_pushdamageinfo( L, lInfo );
		pBot->EndLuaCall( 2 );
	}
}

void CLuaNextBotIntention::OnNavAreaChanged( CNavArea *enteredArea, CNavArea *leftArea )
{
	// GMod's ENT:OnNavAreaChanged( old, new ) hands two CNavArea objects.  Those
	// become Lua values with the navmesh library (lnavmesh.cpp); until it is in
	// place the callback still fires, with no arguments, so a script that only
	// counts transitions keeps working.
	CLuaNextBot *pBot = m_me.Get();
	if ( pBot == NULL )
		return;

	if ( pBot->BeginLuaCall( "OnNavAreaChanged" ) )
		pBot->EndLuaCall( 0 );
}

//-----------------------------------------------------------------------------
// The entity.
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CLuaNextBot )
END_DATADESC()

CLuaNextBot::CLuaNextBot( void )
{
	m_intention = NULL;
	m_locomotor = NULL;
	m_bScriptLoaded = false;

	// GMod's defaults for the three values Source's locomotion cannot take; see
	// the note at CLuaLocomotion_SetStepHeight below.
	m_flLuaStepHeight = 18.0f;
	m_flLuaAcceleration = 900.0f;
	m_flLuaDeceleration = 900.0f;
	m_flLuaJumpHeight = 0.0f;
	m_flLuaDeathDropHeight = 0.0f;
	m_flLuaGravity = 0.0f;
	m_flLuaMaxYawRate = 0.0f;

	m_nTableReference = LUA_NOREF;

	m_nThinkCalls = 0;
	m_nBehaveUpdateCalls = 0;
	m_nBodyUpdateCalls = 0;
	m_nLuaErrors = 0;
	m_nLuaCallMisses = 0;
	m_nLocoUpdateCalls = 0;
	m_nApproachCalls = 0;
	m_nMoveCheckTicks = 0;
}

CLuaNextBot::~CLuaNextBot( void )
{
	// ⚠️ m_intention / m_locomotor are OWNED BY INextBot: they register themselves
	// in INextBotComponent's constructor, and ~INextBot() deletes
	// m_baseIntention / m_baseLocomotion / m_baseBody / m_baseVision
	// (NextBotInterface.cpp:44-63).  Deleting them here as well is a double free;
	// only the pointers are dropped.
	m_intention = NULL;
	m_locomotor = NULL;

	// The m_nTableReference discipline from AGENTS.md 5.4.1: ~CBaseEntity unrefs
	// this same reference unconditionally, so clear it here (luaL_unref() ignores
	// negative refs) and let that one become a no-op.  The L != NULL test covers
	// shutdown, where entities are destroyed after the lua_State is gone.
	if ( L != NULL && m_nTableReference >= 0 )
		lua_unref( L, m_nTableReference );

	m_nTableReference = LUA_NOREF;
}

void CLuaNextBot::Spawn( void )
{
	BaseClass::Spawn();

	// HL2SB GMod compat: a model-less Lua nextbot still has to reach the client.
	//
	// CBaseEntity::UpdateTransmitState() refuses to send anything that has neither
	// a model index nor a model name (baseentity.cpp: "if ( !GetModelIndex() ||
	// !GetModelName() ) return SetTransmitState( FL_EDICT_DONTSEND )"), unless the
	// entity carries EFL_FORCE_CHECK_TRANSMIT.
	//
	// npc_verity is exactly that case: it has no ENT.Model at all -- its
	// ENT:DrawTranslucent draws a sprite (render.DrawQuadEasy) -- so the server
	// silently dropped it, the client never built a C_NextBotCombatCharacter for
	// it, and the bot walked around the map being completely invisible while every
	// client-side hook (Initialize, GetRenderGroup, DrawTranslucent) never ran.
	//
	// GMod transmits scripted entities regardless of a model; this is the flag the
	// engine already provides for precisely that.
	AddEFlags( EFL_FORCE_CHECK_TRANSMIT );

	// HL2SB GMod compat: tell the client which lua/entities script this bot runs.
	// The class is networked as NextBotCombatCharacter (no DECLARE_SERVERCLASS of
	// its own - see the class comment), so without this the client's GetClassname()
	// is "NextBotCombatCharacter" and scripted_ents.GetStored() misses: none of the
	// client-side ENT hooks (DrawTranslucent / RenderOverride / Initialize) could
	// ever be found.  CBaseScripted does the same thing in InitScriptedEntity().
	Q_strncpy( m_iScriptedClassname.GetForModify(), GetClassname(), 255 );

	// The pieces of INextBot a Lua nextbot uses.  There is no body or vision
	// interface on purpose: base_nextbot's scripts do their own looking (SCP-096
	// traces and spheres for its target), and GMod's own nextbot Lua never asks
	// NextBot for either.
	m_locomotor = CreateLuaNextBotLocomotion( this );
	m_intention = new CLuaNextBotIntention( this );

	// Make our think take over from the base's DoThink: the bot update still has
	// to run (it drives the locomotion and our intention), and BodyUpdate follows
	// it every frame.
	SetThink( &CLuaNextBot::Think );
	SetNextThink( gpGlobals->curtime );

	if ( !LoadNextBotScript() )
	{
		Warning( "[HL2SB] Lua nextbot '%s' has no lua/entities script - removing it\n", GetClassname() );
		UTIL_Remove( this );
		return;
	}

	// GMod hands the behaviour to the intention's Reset(), which calls
	// BehaveStart().  Reset() is also what a component reset does from here on.
	//
	// ⚠️ INextBot::Reset() (not just the intention's) is REQUIRED here, and the
	// reason is a trap the crash dump spelled out: NextBotGroundLocomotion caches
	// the entity pointer in ITS Reset() -
	//
	//     NextBotGroundLocomotion.cpp:59  m_nextBot = GetBot()->GetEntity();
	//
	// and the only Reset() the base class performs is
	// NextBotCombatCharacter::Spawn() -> Reset(), which runs BEFORE this function
	// creates the components (NextBot.cpp:227).  So the locomotion's m_nextBot
	// stayed NULL, and the first ILocomotion::Update() ->
	// ILocomotion::StuckMonitor() -> GetFeet() -> m_nextBot->GetPosition()
	// dereferenced null and took the process down (AV reading 0xB48, per
	// dumps/crash_20260916_043617).  INextBot::Reset() iterates the components
	// and resets each one, which both fills m_nextBot in and starts the
	// behaviour coroutine - and it has to happen AFTER LoadNextBotScript(),
	// because the reset dispatch reaches BehaveStart() through the script table.
	Reset();
}

void CLuaNextBot::Precache( void )
{
	BaseClass::Precache();

	// GMod's ENT:Precache().  The script names its sounds and models here; the
	// model itself is set from ENT:Initialize() (see the note in
	// CBaseScripted::Precache for why nothing is precached on the script's behalf).
	if ( m_nTableReference >= 0 && BeginLuaCall( "Precache" ) )
		EndLuaCall( 0 );
}

void CLuaNextBot::Think( void )
{
	++m_nThinkCalls;
	++m_nMoveCheckTicks;

	//-----------------------------------------------------------------------------
	// HL2SB: automatic "why is this bot not moving?" report.
	//
	// A still nextbot is silent: the script asks for a speed, the animation plays,
	// and nothing in the log says which link of the chain is dead.  Every ~2
	// seconds, if the script asked for movement and the bot has not moved, write
	// the whole chain to ds_debug.log -- locomotion Update()s, path Approach()s,
	// desired vs actual speed, stuck flag and the nav mesh state.
	//-----------------------------------------------------------------------------
	if ( m_locomotor != NULL && ( m_nMoveCheckTicks % 64 ) == 0 )
	{
		const float flDesired = m_locomotor->GetDesiredSpeed();
		const float flActual = m_locomotor->GetVelocity().Length2D();

		if ( flDesired > 0.0f && flActual < 5.0f )
		{
			Msg( "[HL2SB] nextbot '%s' is NOT moving: desiredSpeed=%.0f actualSpeed=%.0f locoUpdates=%d approaches=%d stuck=%d navLoaded=%d navAreas=%d\n",
				GetClassname(), flDesired, flActual, m_nLocoUpdateCalls, m_nApproachCalls,
				(int)m_locomotor->IsStuck(),
				( TheNavMesh != NULL && TheNavMesh->IsLoaded() ) ? 1 : 0,
				( TheNavMesh != NULL ) ? (int)TheNavMesh->GetNavAreaCount() : 0 );
		}
	}

	// A periodic, unconditional one-liner: without it "it does not move" cannot be
	// told apart from "it never asked to move" (the script's state machine) or
	// "there is no nav mesh on this map".
	if ( m_locomotor != NULL && ( m_nMoveCheckTicks % 320 ) == 0 )
	{
		// The script's own state: Count is SCP-096's anger timer (it only asks for a
		// speed inside the chase branch) and Enemy tells whether it ever picked a
		// target.  Together with desiredSpeed this separates "the script never
		// wanted to move" from "the movement did not happen".
		int nCount = -1;
		bool bHasEnemy = false;

		if ( L != NULL && m_nTableReference >= 0 )
		{
			lua_getref( L, m_nTableReference );

			if ( lua_istable( L, -1 ) )
			{
				// Protected reads: this runs every 64th tick, and a script whose
				// __index chain raises must not take the process down from a
				// diagnostic -- that is the failure this whole change is about.
				luasrc_PushScriptField( L, -1, "Count" );
				if ( lua_isnumber( L, -1 ) )
					nCount = (int)lua_tointeger( L, -1 );
				lua_pop( L, 1 );

				luasrc_PushScriptField( L, -1, "Enemy" );
				bHasEnemy = !lua_isnil( L, -1 );
				lua_pop( L, 1 );
			}

			lua_pop( L, 1 );
		}

		Msg( "[HL2SB] nextbot '%s' tick: desiredSpeed=%.0f actualSpeed=%.0f locoUpdates=%d approaches=%d navLoaded=%d navAreas=%d Count=%d Enemy=%d\n",
			GetClassname(), m_locomotor->GetDesiredSpeed(), m_locomotor->GetVelocity().Length2D(),
			m_nLocoUpdateCalls, m_nApproachCalls,
			( TheNavMesh != NULL && TheNavMesh->IsLoaded() ) ? 1 : 0,
			( TheNavMesh != NULL ) ? (int)TheNavMesh->GetNavAreaCount() : 0,
			nCount, (int)bHasEnemy );
		Msg( "[HL2SB]   ... read via m_locomotor=%p, via bot=%p (must be the same)\n",
			(void*)m_locomotor, (void*)GetLocomotionInterface() );
	}

	// GMod's ENT:Think().  Every scripted entity gets it, nextbots included --
	// and it is NOT reached through CBaseScripted, whose dispatch this class
	// bypasses with its own SetThink(&CLuaNextBot::Think).  SCP-096's nextbot
	// keeps its stuck detector's reference position there (self.LastPos =
	// self.Entity:GetPos()), so without this call that record stayed nil.
	if ( m_bScriptLoaded && BeginLuaCall( "Think" ) )
		EndLuaCall( 0 );

	// NextBotCombatCharacter's own think: it runs BeginUpdate()/Update()/
	// EndUpdate(), i.e. the locomotion and our intention - whose Update() resumes
	// the behaviour coroutine - and it re-arms the next think itself.
	DoThink();

	// GMod's ENT:BodyUpdate() follows the bot update, every frame: it is the
	// animation half (base_nextbot's BodyUpdate calls BodyMoveXY() while the bot
	// walks or runs, and FrameAdvance() otherwise).
	if ( m_bScriptLoaded && BeginLuaCall( "BodyUpdate" ) )
	{
		++m_nBodyUpdateCalls;
		EndLuaCall( 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: GMod ENT contract: OnRemove() runs before the entity is deleted.
//          NextBotCombatCharacter does not go through CBaseScripted, so the
//          dispatch has to be repeated here; without it a scripted nextbot
//          leaks whatever it started -- SCP-096's chase music and its alert /
//          lost-target sound loops kept playing after the bot was gone.
//-----------------------------------------------------------------------------
void CLuaNextBot::UpdateOnRemove( void )
{
	if ( m_bScriptLoaded && BeginLuaCall( "OnRemove" ) )
	{
		lua_pushboolean( L, true );	// fullUpdate, same as CBaseScripted::UpdateOnRemove
		EndLuaCall( 1 );
	}

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// hl2sb_nextbot_status -- "why is this bot (not) running?"
//
//   Type it in the console, no arguments.  For every Lua nextbot it prints the
//   engine half (health, position, velocity, locomotor speed, current activity)
//   and the Lua half that decides everything else: which of the functions the
//   engine looks up the entity's LIVE script table answers, the status of the
//   BehaveStart() coroutine, how often the engine actually reached Lua, and how
//   many of those lookups came back empty.
//
//   The last number is the one that matters most, because it is the silent
//   failure: BeginLuaCall() finding no such function returns false, prints
//   nothing, and drops the entire behaviour layer.  That is precisely what
//   SCP-096 looked like while entity.get() aliased base_nextbot to
//   prop_scripted -- RunBehaviour was there, BehaveStart was not, no coroutine
//   was ever created, and the bot stood still with a clean log.
//-----------------------------------------------------------------------------

static const char *LuaNextBot_FieldState (lua_State *L, int nRef, const char *pszField, char *pszBuf, size_t nBuf) {
	Q_strncpy( pszBuf, "MISSING", nBuf );

	if ( L == NULL || nRef < 0 ) {
		Q_strncpy( pszBuf, "no-reference", nBuf );
		return pszBuf;
	}

	lua_getref( L, nRef );

	if ( !lua_istable( L, -1 ) ) {
		lua_pop( L, 1 );
		Q_strncpy( pszBuf, "no-table", nBuf );
		return pszBuf;
	}

	lua_getfield( L, -1, pszField );

	if ( lua_isfunction( L, -1 ) )
		Q_strncpy( pszBuf, "fn", nBuf );
	else if ( !lua_isnil( L, -1 ) )
		Q_strncpy( pszBuf, lua_typename( L, lua_type( L, -1 ) ), nBuf );

	lua_pop( L, 2 );
	return pszBuf;
}

static void LuaNextBot_PrintScalar (lua_State *L, int nRef, const char *pszField) {
	if ( L == NULL || nRef < 0 )
		return;

	lua_getref( L, nRef );

	if ( !lua_istable( L, -1 ) ) {
		lua_pop( L, 1 );
		return;
	}

	lua_getfield( L, -1, pszField );

	switch ( lua_type( L, -1 ) ) {
		case LUA_TNUMBER:	Msg( " %s=%g", pszField, lua_tonumber( L, -1 ) ); break;
		case LUA_TBOOLEAN:	Msg( " %s=%s", pszField, lua_toboolean( L, -1 ) ? "true" : "false" ); break;
		case LUA_TSTRING:	Msg( " %s=%s", pszField, lua_tostring( L, -1 ) ); break;
		default:			Msg( " %s=<%s>", pszField, lua_typename( L, lua_type( L, -1 ) ) ); break;
	}

	lua_pop( L, 2 );
}

void CLuaNextBot::DumpStatus( void )
{
	const bool bOnGround = ( m_locomotor != NULL ) && m_locomotor->IsOnGround();
	const bool bStuck = ( m_locomotor != NULL ) && m_locomotor->IsStuck();
	const float flDesiredSpeed = ( m_locomotor != NULL ) ? m_locomotor->GetDesiredSpeed() : 0.0f;
	const Vector &vecOrigin = GetAbsOrigin();
	const Vector &vecVelocity = GetAbsVelocity();

	Msg( "[HL2SB] nextbot '%s' ent %d\n", GetClassname(), entindex() );
	Msg( "    engine : scriptLoaded=%d health=%d origin=(%.0f %.0f %.0f) vel=(%.0f %.0f %.0f) onGround=%d\n",
		(int)m_bScriptLoaded, GetHealth(), vecOrigin.x, vecOrigin.y, vecOrigin.z,
		vecVelocity.x, vecVelocity.y, vecVelocity.z, (int)bOnGround );
	Msg( "    loco   : desiredSpeed=%.0f stepHeight=%.0f accel=%.0f decel=%.0f stuck=%d\n",
		flDesiredSpeed, m_flLuaStepHeight, m_flLuaAcceleration, m_flLuaDeceleration, (int)bStuck );

	// "The bot does not move" diagnosis: the movement chain is
	// NextBot tick -> locomotion Update() -> PathFollower Update -> Approach() ->
	// velocity.  Counters on the two middle links say which one is not running.
	{
		const float flActual = ( m_locomotor != NULL ) ? m_locomotor->GetVelocity().Length2D() : 0.0f;
		const bool bAttempting = ( m_locomotor != NULL ) && m_locomotor->IsAttemptingToMove();
		Vector vecMotion = ( m_locomotor != NULL ) ? m_locomotor->GetGroundMotionVector() : vec3_origin;

		Msg( "    move   : locoUpdates=%d approaches=%d actualSpeed=%.0f attemptingToMove=%d groundMotion=(%.2f %.2f)\n",
			m_nLocoUpdateCalls, m_nApproachCalls, flActual, (int)bAttempting, vecMotion.x, vecMotion.y );
	}

	// The nav mesh is what Path:Compute() walks: without it every path is invalid
	// and a nextbot stands there playing animations.
	Msg( "    nav    : loaded=%d areas=%d generating=%d\n",
		( TheNavMesh != NULL && TheNavMesh->IsLoaded() ) ? 1 : 0,
		( TheNavMesh != NULL ) ? (int)TheNavMesh->GetNavAreaCount() : 0,
		( TheNavMesh != NULL && TheNavMesh->IsGenerating() ) ? 1 : 0 );
	Msg( "    anim   : sequence=%d activity=%d cycle=%.2f rate=%.2f\n",
		GetSequence(), GetSequenceActivity( GetSequence() ), GetCycle(), GetPlaybackRate() );
	Msg( "    calls  : think=%d behaveUpdate=%d bodyUpdate=%d luaErrors=%d lookupsFoundNothing=%d lookupsRaised=%d\n",
		m_nThinkCalls, m_nBehaveUpdateCalls, m_nBodyUpdateCalls, m_nLuaErrors, m_nLuaCallMisses, m_nLookupErrors );

	// The functions the engine looks up, checked on the very table it looks them
	// up on (a base class only counts if it was merged into this table).
	const char *pszFields[] = {
		"BehaveStart", "BehaveUpdate", "BodyUpdate", "RunBehaviour",
		"MoveToPos", "HandleStuck", "Initialize", "Precache",
	};
	char szField[ 40 ];

	Msg( "    script : reference=%d ", m_nTableReference );
	for ( int i = 0; i < ARRAYSIZE( pszFields ); ++i )
		Msg( "%s=%s ", pszFields[ i ], LuaNextBot_FieldState( L, m_nTableReference, pszFields[ i ], szField, sizeof( szField ) ) );
	Msg( "\n" );

	// The behaviour coroutine itself.
	char szThread[ 64 ] = "none";

	if ( L != NULL && m_nTableReference >= 0 ) {
		lua_getref( L, m_nTableReference );

		if ( lua_istable( L, -1 ) ) {
			luasrc_PushScriptField( L, -1, "BehaveThread" );

			if ( lua_isthread( L, -1 ) ) {
				lua_getglobal( L, "coroutine" );
				lua_getfield( L, -1, "status" );
				lua_remove( L, -2 );		// [table][thread][status]
				lua_insert( L, -2 );		// [table][status][thread]

				if ( luasrc_pcall( L, 1, 1, 0 ) == 0 && lua_isstring( L, -1 ) )
					Q_strncpy( szThread, lua_tostring( L, -1 ), sizeof( szThread ) );

				lua_pop( L, 1 );			// [table]
			}
			else if ( !lua_isnil( L, -1 ) ) {
				Q_strncpy( szThread, lua_typename( L, lua_type( L, -1 ) ), sizeof( szThread ) );
			}

			lua_pop( L, 1 );				// []
		}

		lua_pop( L, 1 );					// []
	}

	Msg( "    behave : BehaveThread=%s", szThread );
	LuaNextBot_PrintScalar( L, m_nTableReference, "Count" );
	LuaNextBot_PrintScalar( L, m_nTableReference, "Enemy" );
	Msg( "\n" );

	// Hitbox indices.  SCP-096's "you are looking at me" test is literally
	// `tr.HitBox == 0` (init.lua:745) and nothing in the script says which index
	// that is -- the model does.  This line is what separates "the engine never
	// reports a hitbox" from "the index the script tests for is a different bone".
	CStudioHdr *pStudio = GetModelPtr();

	if ( pStudio != NULL )
	{
		const int nSet = GetHitboxSet();
		const int nHitboxes = pStudio->iHitboxCount( nSet );

		Msg( "    hitboxs: set=%d count=%d ", nSet, nHitboxes );

		for ( int i = 0; i < nHitboxes && i < 12; ++i )
		{
			const int nBone = GetHitboxBone( i );

			Msg( "[%d]=%s ", i, ( nBone >= 0 && nBone < pStudio->numbones() ) ? pStudio->pBone( nBone )->pszName() : "?" );
		}

		Msg( "\n" );
	}

	// The trace SCP-096's SeeMe() opens with, unfiltered.  The addon's filter
	// callback is where its ok1 flag is set, so whether the ray reaches a player at
	// all is the difference between "the engine never asks the callback" and "the
	// bot's own bounds swallow the ray".
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();

	if ( pPlayer != NULL )
	{
		const Vector &vecEye = EyePosition();
		const QAngle &angEye = EyeAngles();
		const Vector &vecPlayerEye = pPlayer->EyePosition();

		Vector vecForward;
		AngleVectors( angEye, &vecForward );

		trace_t trUnfiltered;
		UTIL_TraceLine( vecEye, vecPlayerEye, MASK_SHOT, NULL, COLLISION_GROUP_NONE, &trUnfiltered );

		Msg( "    vision : eye=(%.0f %.0f %.0f) forward=(%.2f %.2f %.2f) playerEye=(%.0f %.0f %.0f)\n",
			vecEye.x, vecEye.y, vecEye.z, vecForward.x, vecForward.y, vecForward.z,
			vecPlayerEye.x, vecPlayerEye.y, vecPlayerEye.z );
		Msg( "             eye -> player, unfiltered: hit='%s' fraction=%.3f startsolid=%d hitbox=%d\n",
			trUnfiltered.m_pEnt ? trUnfiltered.m_pEnt->GetClassname() : "none",
			trUnfiltered.fraction, (int)trUnfiltered.startsolid, trUnfiltered.hitbox );
	}
}

static void hl2sb_nextbot_status_f (const CCommand &args) {
	int nCount = 0;

	for ( CBaseEntity *pEntity = gEntList.FirstEnt(); pEntity != NULL; pEntity = gEntList.NextEnt( pEntity ) ) {
		CLuaNextBot *pBot = dynamic_cast< CLuaNextBot * >( pEntity );

		if ( pBot == NULL )
			continue;

		++nCount;
		pBot->DumpStatus();
	}

	Msg( "[HL2SB] %d Lua nextbot(s) in the world\n", nCount );
}

static ConCommand hl2sb_nextbot_status(
	"hl2sb_nextbot_status", hl2sb_nextbot_status_f,
	"Print the engine + Lua state of every Lua nextbot." );

//-----------------------------------------------------------------------------
// Purpose: take this entity's script table, run its one-time setup, and hand it
//          the engine objects base_nextbot expects on `self`.
//-----------------------------------------------------------------------------
bool CLuaNextBot::LoadNextBotScript( void )
{
	if ( m_bScriptLoaded )
		return true;

	if ( L == NULL )
		return false;

	// HL2SB: the navmesh surface and the CLuaLocomotion metatable have to exist in
	// whatever state is loading this bot's script, so make sure of it here rather
	// than only when the first nextbot is registered.
	//
	// Registering happens while the entity scripts are being scanned at startup;
	// a LEVEL CHANGE gives the server a fresh lua_State, and nothing re-registers
	// the scripts in it.  Every bot spawned afterwards then found
	//
	//     attempt to call a nil value (global 'Path')
	//     attempt to index a userdata value (field 'loco')
	//
	// once more, self.MovePath stayed nil, and its Think() raised on every attempt
	// to recompute a path (130 times in one session).  Both installers are
	// idempotent per state, so calling them here is free when they are already in
	// place.
	InstallNextBotEntityMethods();

	const char *pszClassname = GetClassname();
	if ( !pszClassname || !pszClassname[0] )
		return false;

	// entity.get( classname ) - the same call CBaseScripted::LoadScriptedEntity()
	// makes (basescripted.cpp:131).  It answers with this entity's own table: for
	// a scripted entity that is the ENT table, per instance, and it is where a
	// nextbot's whole state lives (self.Enemy, self.loco, self.Count, ...).
	lua_getglobal( L, "entity" );

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	lua_getfield( L, -1, "get" );

	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return false;
	}

	lua_remove( L, -2 );				// [get]
	lua_pushstring( L, pszClassname );	// [get][classname]

	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
		return false;					// the error object is already popped

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;					// no lua/entities script for this name
	}

	// self.loco = <CLuaLocomotion>, before the reference is taken so the script
	// sees it in ENT:Initialize().
	LuaNextBot_PushLocomotion( L, this );
	lua_setfield( L, -2, "loco" );

	// self.Entity = the entity itself - GMod's contract for a scripted entity's
	// table (GMod's engine sets it; addons use `self.Entity:Foo()` and `self:Foo()`
	// interchangeably).  It has to be set BEFORE Initialize() runs, because
	// SCP-096's ENT:Initialize() opens with
	//
	//     self.Entity:SetCollisionBounds( Vector(-4,-4,0), Vector(4,4,64) )
	//
	// and with Entity nil that raised on its first line, skipping the model, the
	// collision bounds and everything else the rest of the function sets up.  The
	// bot then lived on with no model, and the per-frame BodyUpdate ->
	// SetPoseParameter path crashed on the NULL studiohdr (that was the instant
	// crash on spawn).  See also CBaseScripted::InitScriptedEntity, which sets the
	// same field for every other Lua entity.
	lua_pushanimating( L, this );
	lua_setfield( L, -2, "Entity" );

	m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );

	// ⚠️ The reference can legitimately be LUA_REFNIL (-1) - "no table" - which is
	// not LUA_NOREF (-2); see the note in CBaseScripted::InitScriptedEntity.
	if ( m_nTableReference < 0 )
	{
		m_bScriptLoaded = false;
		return false;
	}

	m_bScriptLoaded = true;

	// GMod's ENT:SetupDataTables() then ENT:Initialize().  CBaseScripted runs the
	// same pair in that order (basescripted.cpp:228 and :348), but a nextbot's
	// ENT table cannot use NetworkVar - the entity is networked as
	// NextBotCombatCharacter - so only Initialize() is dispatched here.
	if ( BeginLuaCall( "Initialize" ) )
		EndLuaCall( 0 );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: GMod's NextBot:BodyMoveXY() - the move_x / move_y pose parameters
//          from the ground speed, then FrameAdvance().
//
// base_nextbot's BodyUpdate() calls exactly this while the bot runs or walks
// (sv_nextbot.lua:58-73), and it warns that calling FrameAdvance() again
// afterwards corrupts layered animation playback.
//-----------------------------------------------------------------------------
void CLuaNextBot::BodyMoveXY( void )
{
	// Defensive: a script whose ENT:Initialize() did not finish (a missing API, an
	// error that was caught and logged) can leave this entity with no model at all,
	// and SetPoseParameter() / GetSequenceGroundSpeed() on a NULL studiohdr is a
	// hard access violation rather than a Lua error.  Bail out first.
	if ( GetModelPtr() == NULL )
		return;

	ILocomotion *pMover = GetLocomotionInterface();

	Vector vecVelocity = pMover ? pMover->GetVelocity() : GetAbsVelocity();

	Vector vecForward, vecRight;
	AngleVectors( GetLocalAngles(), &vecForward, &vecRight, NULL );

	float flForward = DotProduct( vecVelocity, vecForward );
	float flRight = DotProduct( vecVelocity, vecRight );

	// The pose parameters are in units of the sequence's own ground speed, so the
	// bot reads move_x = 1.0 while it moves at exactly that speed.  This is the
	// same mapping the HL2 NPCs use with the same parameters.
	//
	// REVERTED: an earlier revision also scaled the animation playback rate with
	// the ground speed here.  GMod's BodyMoveXY comment mentions that behaviour,
	// but my implementation of it was an extrapolation, not a documented contract,
	// and it made a standing bot in a run activity animate in slow motion (clamped
	// low rate) -- reported as "the animations are wrong".  The pose parameters and
	// FrameAdvance below are what the engine needs; nothing else is applied until
	// there is a wiki definition to follow.
	float flGroundSpeed = GetSequenceGroundSpeed( GetSequence() );

	if ( flGroundSpeed > 1.0f )
	{
		flForward /= flGroundSpeed;
		flRight /= flGroundSpeed;
	}

	SetPoseParameter( "move_x", clamp( flForward, -1.0f, 1.0f ) );
	SetPoseParameter( "move_y", clamp( flRight, -1.0f, 1.0f ) );

	// ⚠️ StudioFrameAdvance(), not FrameAdvance(): the latter is a CLIENT-side
	// method in this fork (C_BaseAnimating::FrameAdvance, bound in
	// game/client/lua/lc_baseanimating.cpp:184).  The GMod name FrameAdvance() is
	// bound on the server as an alias of this - see lbaseanimating.cpp.
	StudioFrameAdvance();
}

//-----------------------------------------------------------------------------
// Event dispatch into the script.
//
// These are the entity-level events NextBotCombatCharacter funnels into the
// NextBot event responders (NextBot.cpp:361-478); GMod's base_nextbot has an
// ENT: callback for each of them.
//-----------------------------------------------------------------------------
int CLuaNextBot::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	CTakeDamageInfo lInfo = info;

	// GMod calls OnTakeDamage() first, and a number it returns replaces the
	// engine's damage handling - that is how a bot becomes invulnerable or takes
	// scripted damage.
	if ( BeginLuaCall( "OnTakeDamage" ) )
	{
		lua_pushdamageinfo( L, lInfo );
		luasrc_pcall( L, 2, 1, 0 );

		int nResult = -1;

		if ( lua_isnumber( L, -1 ) )
			nResult = (int)lua_tonumber( L, -1 );

		lua_pop( L, 1 );

		if ( nResult >= 0 )
			return nResult;
	}

	// OnInjured() is the notification-only version, and unlike OnTakeDamage it runs
	// on every hit.
	if ( BeginLuaCall( "OnInjured" ) )
	{
		lua_pushdamageinfo( L, lInfo );
		EndLuaCall( 1 );
	}

	return BaseClass::OnTakeDamage_Alive( info );
}

void CLuaNextBot::Event_Killed( const CTakeDamageInfo &info )
{
	CTakeDamageInfo lInfo = info;

	// base_nextbot's ENT:OnKilled( dmginfo ) is what makes the corpse: it ends
	// with self:BecomeRagdoll( dmginfo ) and a hook.Run( "OnNPCKilled" )
	// (sv_nextbot.lua:158-164), so the script has to run BEFORE the base class
	// finishes the kill.
	if ( BeginLuaCall( "OnKilled" ) )
	{
		lua_pushdamageinfo( L, lInfo );
		EndLuaCall( 1 );
	}

	BaseClass::Event_Killed( info );
}

void CLuaNextBot::HandleAnimEvent( animevent_t *event )
{
	// GMod's ENT:HandleAnimEvent( event, eventtime, cycle, type, options ).
	if ( event && BeginLuaCall( "HandleAnimEvent" ) )
	{
		lua_pushinteger( L, event->event );
		lua_pushnumber( L, event->eventtime );
		lua_pushnumber( L, event->cycle );
		lua_pushinteger( L, event->type );
		lua_pushstring( L, event->options ? event->options : "" );
		EndLuaCall( 5 );
	}

	BaseClass::HandleAnimEvent( event );
}

void CLuaNextBot::Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
{
	BaseClass::Ignite( flFlameLifetime, bNPCOnly, flSize, bCalledByLevelDesigner );

	// ENT:OnIgnite reaches the script through the intention: the base class raises
	// it into the event responders itself.
}

//=============================================================================
// self.loco - GMod's CLuaLocomotion.
//
// GMod hands a nextbot a locomotion object whose methods ARE ILocomotion's, plus
// three numeric tuning knobs (step height, acceleration, deceleration) that
// Source's locomotion has no setter for.  Those three live on the bot and are
// honoured through the subclass below, which overrides the virtual GETTERS the
// locomotion actually consults - storing them and answering the getters would
// have been a lie the nav mesh code never reads.
//=============================================================================

//-----------------------------------------------------------------------------
// Source's locomotion takes step height / acceleration / deceleration from its own
// constants (NextBotGroundLocomotion.h:233/262/267 are inline getters), but all
// three are virtual, so overriding them is how a script-set value takes effect at
// all.  SCP-096's Initialize() asks for a 35-unit step height; without this the
// engine kept 18 and the bot could not climb the steps its own nav mesh walks over
// (it got stuck instead, which the addon then answers by teleporting itself).
//-----------------------------------------------------------------------------
class CLuaNextBotLocomotion : public NextBotGroundLocomotion
{
public:
	CLuaNextBotLocomotion( INextBot *bot ) : NextBotGroundLocomotion( bot ) { m_flLuaDesiredSpeed = 0.0f; }

	//-----------------------------------------------------------------------------
	// The speed the SCRIPT asked for.  NextBotGroundLocomotion's own run/walk
	// answers are the HL2 NPC constants -- GetRunSpeed() is a literal 150.0f,
	// GetWalkSpeed() 75.0f -- and BOTH of them are used as caps by the code that
	// actually walks the bot:
	//
	//   PathFollower::AdjustSpeed()        SetDesiredSpeed( GetRunSpeed() + ... )
	//   NextBotGroundLocomotion::Update()  if ( m_actualSpeed > GetRunSpeed() ) clamp
	//
	// so a script's `self.loco:SetDesiredSpeed( 450 )` was overwritten every frame
	// and then clamped to 150: SCP-096 chased at the player's walking speed.
	// GMod has the same problem to solve and solves it the same way -- its
	// CLuaLocomotion answers the script's value here.  Remembered rather than read
	// back from the base class because m_desiredSpeed is private.
	//-----------------------------------------------------------------------------
	virtual void SetDesiredSpeed( float speed )
	{
		// HL2SB diagnostic: the chase sets 450 and the bot still stands still with a
		// valid path, so SOMETHING re-zeroes the speed afterwards.  Log every
		// change (throttled) so the log names the value and the class.
		if ( speed != m_flLuaDesiredSpeed )
		{
			static float s_flNextSpeedReport = 0.0f;
			float flNow = (float)gpGlobals->curtime;

			if ( flNow >= s_flNextSpeedReport )
			{
				s_flNextSpeedReport = flNow + 1.0f;

				CLuaNextBot *pBot = LuaBot();
				Msg( "[HL2SB] loco %p SetDesiredSpeed %.0f -> %.0f on '%s'\n",
					this, m_flLuaDesiredSpeed, speed, ( pBot != NULL ) ? pBot->GetClassname() : "?" );
			}
		}

		m_flLuaDesiredSpeed = speed;
		// ⚠️ 必须写全名 NextBotGroundLocomotion::, 不能写 BaseClass:: !
		// `BaseClass` 是从 NextBotGroundLocomotion 的 DECLARE_CLASS 继承下来的
		// typedef (public/networkvar.h:99 `DECLARE_CLASS(c,b)` -> `typedef b BaseClass;`),
		// 也就是 **ILocomotion**, 不是直接基类。而 ILocomotion::SetDesiredSpeed 是空函数,
		// 于是 m_desiredSpeed 永远是 0 -> GetDesiredSpeed() 读回 0 ->
		// NextBotGroundLocomotion::Update() 里 maxSpeed = MIN(0,limit) = 0 -> 永远不加速。
		// 本类里所有 BaseClass:: 调用都会踩这个坑, 所以下面一律写全名。
		NextBotGroundLocomotion::SetDesiredSpeed( speed );

		// HL2SB diagnostic: prove the write reaches the member the engine reads.
		// If this ever prints, the value is written to somewhere else than it is
		// read from - which is the only remaining explanation for "AdjustSpeed wrote
		// 700 and one line later GetDesiredSpeed() says 0" on a single object.
		const float flReadBack = NextBotGroundLocomotion::GetDesiredSpeed();

		if ( flReadBack != speed )
		{
			static int s_nUnstuckLogs = 0;

			if ( s_nUnstuckLogs < 8 )
			{
				++s_nUnstuckLogs;
				Msg( "[HL2SB] loco %p: SetDesiredSpeed( %.0f ) DID NOT STICK - member reads %.0f\n",
					this, speed, flReadBack );
			}
		}
	}

	//-----------------------------------------------------------------------------
	// HL2SB diagnostic: the four OTHER writers of m_desiredSpeed.
	//
	// grep of the whole engine: m_desiredSpeed is written in exactly five places, all
	// in NextBotGroundLocomotion.cpp - SetDesiredSpeed() (logged above), Reset() :61
	// (=0), Run() :1173 (=GetRunSpeed), Walk() :1183 (=GetWalkSpeed), Stop() :1193
	// (=0).  The last four write the member DIRECTLY, so a call to them is invisible
	// to the SetDesiredSpeed log, and Stop()/Reset() both zero it.  "the script sets
	// 450, the bot stands still, GetDesiredSpeed() reads 0" can only be one of them,
	// so name it.  Bounded to 12 lines total.
	//-----------------------------------------------------------------------------
	static void LogSpeedWriter( const char *pszWhat, const void *pLoco, float flShadow )
	{
		static int s_nLogs = 0;

		if ( s_nLogs < 12 )
		{
			++s_nLogs;
			Msg( "[HL2SB] loco %p: %s() called - it writes m_desiredSpeed itself, script speed was %.0f\n",
				pLoco, pszWhat, flShadow );
		}
	}

	virtual void Stop( void )
	{
		LogSpeedWriter( "Stop", this, m_flLuaDesiredSpeed );
		NextBotGroundLocomotion::Stop();
	}

	virtual void Run( void )
	{
		LogSpeedWriter( "Run", this, m_flLuaDesiredSpeed );
		NextBotGroundLocomotion::Run();
	}

	virtual void Walk( void )
	{
		LogSpeedWriter( "Walk", this, m_flLuaDesiredSpeed );
		NextBotGroundLocomotion::Walk();
	}

	virtual void Reset( void )
	{
		LogSpeedWriter( "Reset", this, m_flLuaDesiredSpeed );
		NextBotGroundLocomotion::Reset();
	}

	//-----------------------------------------------------------------------------
	// HL2SB GMod compat: the script's speed, answered through the interface.
	//
	// GMod's CLuaLocomotion owns the desired speed (wiki: SetDesiredSpeed /
	// GetDesiredSpeed are a pair on that class), and NextBotGroundLocomotion turns
	// it into movement at exactly one place - ApplyAccumulatedApproach()'s
	// MIN( GetDesiredSpeed(), GetSpeedLimit() ), which now goes through this virtual
	// instead of the private member.  Answering here is therefore what actually
	// moves a Lua nextbot; the base member is still written by SetDesiredSpeed
	// above, but as the "DID NOT STICK" probe showed it cannot be relied on.
	//
	// Same shape as GetRunSpeed(): only a POSITIVE script value counts, so a bot
	// whose script never called SetDesiredSpeed still answers the base.
	//-----------------------------------------------------------------------------
	virtual float GetDesiredSpeed( void ) const
	{
		return ( m_flLuaDesiredSpeed > 0.0f ) ? m_flLuaDesiredSpeed : NextBotGroundLocomotion::GetDesiredSpeed();
	}

	virtual float GetRunSpeed( void ) const
	{
		// ⚠️ Only a POSITIVE script value may become the cap.  An explicit 0 must
		// not: PathFollower::AdjustSpeed() writes SetDesiredSpeed( GetRunSpeed() +
		// ... ) every frame, so a 0 here freezes the bot forever -- it plays its
		// run animation with no translation, exactly what SCP-096 did after its
		// door-breaking branch called loco:SetDesiredSpeed( 0 ).  A script's 0
		// still stops it, because that lands in m_desiredSpeed and
		// NextBotGroundLocomotion::Update() takes MIN( m_desiredSpeed, GetSpeedLimit() ).
		return ( m_flLuaDesiredSpeed > 0.0f ) ? m_flLuaDesiredSpeed : NextBotGroundLocomotion::GetRunSpeed();
	}

	virtual float GetWalkSpeed( void ) const
	{
		return ( m_flLuaDesiredSpeed > 0.0f ) ? m_flLuaDesiredSpeed : NextBotGroundLocomotion::GetWalkSpeed();
	}

	virtual float GetStepHeight( void ) const
	{
		CLuaNextBot *pBot = LuaBot();
		return pBot ? pBot->m_flLuaStepHeight : NextBotGroundLocomotion::GetStepHeight();
	}

	// HL2SB: GMod's loco:SetJumpHeight() / SetDeathDropHeight() land here.  Both
	// are virtual getters in Source's locomotion (NextBotGroundLocomotion.h:239
	// and :245, the HL2 NPC constants), and the path/retreat code reads them to
	// decide what the bot may climb and what fall kills it.
	virtual float GetMaxJumpHeight( void ) const
	{
		CLuaNextBot *pBot = LuaBot();
		return ( pBot != NULL && pBot->m_flLuaJumpHeight > 0.0f ) ? pBot->m_flLuaJumpHeight : NextBotGroundLocomotion::GetMaxJumpHeight();
	}

	virtual float GetDeathDropHeight( void ) const
	{
		CLuaNextBot *pBot = LuaBot();
		return ( pBot != NULL && pBot->m_flLuaDeathDropHeight > 0.0f ) ? pBot->m_flLuaDeathDropHeight : NextBotGroundLocomotion::GetDeathDropHeight();
	}

	virtual float GetMaxAcceleration( void ) const
	{
		CLuaNextBot *pBot = LuaBot();
		return pBot ? pBot->m_flLuaAcceleration : NextBotGroundLocomotion::GetMaxAcceleration();
	}

	virtual float GetMaxDeceleration( void ) const
	{
		CLuaNextBot *pBot = LuaBot();
		return pBot ? pBot->m_flLuaDeceleration : NextBotGroundLocomotion::GetMaxDeceleration();
	}

	// Diagnostics for "the bot does not move": how often the locomotion was ticked
	// and how often the path asked it to move.  Those are the two links in the
	// chain (INextBot::Update -> component Update; PathFollower::Update -> Approach)
	// and hl2sb_nextbot_status prints both, so a still bot says which link is dead.
	virtual void Update( void )
	{
		CLuaNextBot *pBot = LuaBot();
		if ( pBot != NULL ) ++pBot->m_nLocoUpdateCalls;

		// HL2SB diagnostic: the two gates the movement actually passes through, read
		// from the inside of the tick that has to move the bot:
		//   ApplyAccumulatedApproach(): maxSpeed = MIN( m_desiredSpeed, GetSpeedLimit() )
		//   Update():                   if ( !IsAttemptingToMove() ) velocity = 0
		// Every ~250 ticks, so a long session stays readable.
		if ( pBot != NULL && ( pBot->m_nLocoUpdateCalls % 250 ) == 1 )
		{
			// The last gate is the body: a MOTION_CONTROLLED_XY activity makes
			// Update() throw the acceleration away and take the velocity from the
			// entity instead, and a non-mobile posture stops the approach entirely.
			IBody *pBody = ( GetBot() != NULL ) ? GetBot()->GetBodyInterface() : NULL;

			Msg( "[HL2SB] loco %p Update #%d: desired(virtual)=%.0f member=%.0f shadow=%.0f speedLimit=%.0f actual=%.0f onGround=%d attempting=%d motionXY=%d motionZ=%d postureMobile=%d\n",
				this, pBot->m_nLocoUpdateCalls, GetDesiredSpeed(),
				NextBotGroundLocomotion::GetDesiredSpeed(), m_flLuaDesiredSpeed,
				GetSpeedLimit(), GetVelocity().Length2D(),
				(int)IsOnGround(), (int)IsAttemptingToMove(),
				pBody ? (int)pBody->HasActivityType( IBody::MOTION_CONTROLLED_XY ) : -1,
				pBody ? (int)pBody->HasActivityType( IBody::MOTION_CONTROLLED_Z ) : -1,
				pBody ? (int)pBody->IsPostureMobile() : -1 );
		}

		NextBotGroundLocomotion::Update();
	}

	virtual void Approach( const Vector &goal, float goalWeight )
	{
		CLuaNextBot *pBot = LuaBot();
		if ( pBot != NULL ) ++pBot->m_nApproachCalls;

		NextBotGroundLocomotion::Approach( goal, goalWeight );
	}

private:
	CLuaNextBot *LuaBot( void ) const
	{
		INextBot *pBot = GetBot();
		return ( pBot != NULL ) ? dynamic_cast< CLuaNextBot * >( pBot->GetEntity() ) : NULL;
	}

	// The script's desired speed; see the note above.
	float m_flLuaDesiredSpeed;
};

static NextBotGroundLocomotion *CreateLuaNextBotLocomotion( INextBot *pBot )
{
	return new CLuaNextBotLocomotion( pBot );
}

struct LuaLocomotion_t
{
	EHANDLE m_hBot;
};

static CLuaNextBot *CheckLocoBot( lua_State *L )
{
	LuaLocomotion_t *pLoco = (LuaLocomotion_t *)luaL_checkudata( L, 1, "CLuaLocomotion" );
	if ( pLoco == NULL )
		return NULL;

	return (CLuaNextBot *)pLoco->m_hBot.Get();
}

static int CLuaLocomotion_SetDesiredSpeed( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
	{
		const float flSpeed = (float)luaL_checknumber( L, 2 );

		// HL2SB diagnostic: prove whether the SCRIPT's call arrives at all.
		static float s_flNextScriptSpeedReport = 0.0f;
		float flNow = (float)gpGlobals->curtime;

		if ( flNow >= s_flNextScriptSpeedReport )
		{
			s_flNextScriptSpeedReport = flNow + 1.0f;
			Msg( "[HL2SB] loco:SetDesiredSpeed( %.0f ) called from script on '%s'\n",
				flSpeed, ( pBot != NULL ) ? pBot->GetClassname() : "?" );
		}

		pMover->SetDesiredSpeed( flSpeed );
	}

	return 0;
}

static int CLuaLocomotion_GetDesiredSpeed( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetDesiredSpeed() : 0.0f );
	return 1;
}

static int CLuaLocomotion_SetSpeedLimit( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->SetSpeedLimit( (float)luaL_checknumber( L, 2 ) );

	return 0;
}

static int CLuaLocomotion_GetSpeedLimit( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetSpeedLimit() : 0.0f );
	return 1;
}

//-----------------------------------------------------------------------------
// The three knobs Source's NextBotGroundLocomotion has no setter for.
//
// In GMod CLuaLocomotion owns step height, acceleration and deceleration as plain
// numbers and feeds them to its own movement code.  Source's ground locomotion
// instead takes them from the NAV MESH (the connections and their traversal
// costs are baked at generation time) and from its own constants: the header
// exposes GetStepHeight() / GetMaxAcceleration() / GetMaxDeceleration() and
// nothing that sets them, and SetAcceleration(Vector) is the *instantaneous*
// world-space acceleration of one physics step, not a limit.
//
// So the value is stored on the bot and reported back by the getters.  What a
// script actually uses to move - SetDesiredSpeed() - IS wired through, which is
// what SCP-096 depends on.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// HL2SB GMod compat: the rest of the CLuaLocomotion method set the wiki lists.
//
// Everything that Source's ILocomotion actually answers is wired straight
// through; the ones it has no setter for (gravity, yaw rate) remember the script's
// value on the bot, and the three "allowed" switches drive the engine's own
// nb_allow_* convars, which is what they mean during path generation
// (NextBotPathFollow.cpp:28-30).
//-----------------------------------------------------------------------------
#define LUA_LOCO_BOT_AND_MOVER \
	CLuaNextBot *pBot = CheckLocoBot( L ); \
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

static int CLuaLocomotion_GetGroundMotionVector( lua_State *L )
{
	LUA_LOCO_BOT_AND_MOVER
	lua_pushvector( L, pMover ? pMover->GetGroundMotionVector() : vec3_origin );
	return 1;
}

static int CLuaLocomotion_GetGroundNormal( lua_State *L )
{
	LUA_LOCO_BOT_AND_MOVER
	lua_pushvector( L, pMover ? pMover->GetGroundNormal() : Vector( 0, 0, 1 ) );
	return 1;
}

static int CLuaLocomotion_IsClimbingOrJumping( lua_State *L )
{
	LUA_LOCO_BOT_AND_MOVER
	lua_pushboolean( L, pMover != NULL && pMover->IsClimbingOrJumping() );
	return 1;
}

static int CLuaLocomotion_IsUsingLadder( lua_State *L )
{
	LUA_LOCO_BOT_AND_MOVER
	lua_pushboolean( L, pMover != NULL && pMover->IsUsingLadder() );
	return 1;
}

static int CLuaLocomotion_IsAttemptingToMove( lua_State *L )
{
	LUA_LOCO_BOT_AND_MOVER
	lua_pushboolean( L, pMover != NULL && pMover->IsAttemptingToMove() );
	return 1;
}

static int CLuaLocomotion_IsAreaTraversable( lua_State *L )
{
	LUA_LOCO_BOT_AND_MOVER

	CNavArea **ppArea = (CNavArea **)luaL_checkudata( L, 2, "CNavArea" );
	if ( pMover == NULL || ppArea == NULL || *ppArea == NULL )
	{
		lua_pushboolean( L, false );
		return 1;
	}

	lua_pushboolean( L, pMover->IsAreaTraversable( *ppArea ) );
	return 1;
}

static int CLuaLocomotion_JumpAcrossGap( lua_State *L )
{
	LUA_LOCO_BOT_AND_MOVER
	if ( pMover )
		pMover->JumpAcrossGap( luaL_checkvector( L, 2 ), luaL_checkvector( L, 3 ) );

	return 0;
}

static int CLuaLocomotion_GetNextBot( lua_State *L )
{
	LUA_LOCO_BOT_AND_MOVER
	lua_pushanimating( L, pBot );
	return 1;
}

// Gravity / max yaw rate: ILocomotion only exposes getters here, so the script's
// value is remembered and answered back (the honest half of the contract).
static int CLuaLocomotion_SetGravity( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	if ( pBot )
		pBot->m_flLuaGravity = (float)luaL_checknumber( L, 2 );

	return 0;
}

static int CLuaLocomotion_GetGravity( lua_State *L )
{
	// NextBotGroundLocomotion keeps GetGravity/GetMaxYawRate private, so the
	// script's value is answered when it set one and the wiki's own defaults
	// otherwise (SetGravity default = 1000, SetMaxYawRate default = 250).
	CLuaNextBot *pBot = CheckLocoBot( L );

	lua_pushnumber( L, ( pBot != NULL && pBot->m_flLuaGravity > 0.0f ) ? pBot->m_flLuaGravity : 1000.0f );
	return 1;
}

static int CLuaLocomotion_SetMaxYawRate( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	if ( pBot )
		pBot->m_flLuaMaxYawRate = (float)luaL_checknumber( L, 2 );

	return 0;
}

static int CLuaLocomotion_GetMaxYawRate( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );

	lua_pushnumber( L, ( pBot != NULL && pBot->m_flLuaMaxYawRate > 0.0f ) ? pBot->m_flLuaMaxYawRate : 250.0f );
	return 1;
}

static int CLuaLocomotion_GetCurrentAcceleration( lua_State *L )
{
	// Our locomotion does not expose the instantaneous acceleration vector, and
	// inventing one would be worse than saying zero.
	lua_pushvector( L, vec3_origin );
	return 1;
}

// The three "allowed" switches are the engine's own path-generation convars.
static int CLuaLocomotion_SetAllowedSwitch( lua_State *L, const char *pszConVar )
{
	ConVar *pConVar = cvar->FindVar( pszConVar );

	if ( pConVar != NULL )
		pConVar->SetValue( lua_toboolean( L, 2 ) ? 1 : 0 );

	return 0;
}

static bool CLuaLocomotion_GetAllowedSwitch( lua_State *L, const char *pszConVar )
{
	ConVar *pConVar = cvar->FindVar( pszConVar );
	return ( pConVar != NULL ) ? pConVar->GetBool() : true;
}

static int CLuaLocomotion_SetAvoidAllowed( lua_State *L )   { return CLuaLocomotion_SetAllowedSwitch( L, "nb_allow_avoiding" ); }
static int CLuaLocomotion_GetAvoidAllowed( lua_State *L )   { lua_pushboolean( L, CLuaLocomotion_GetAllowedSwitch( L, "nb_allow_avoiding" ) ); return 1; }
static int CLuaLocomotion_SetClimbAllowed( lua_State *L )   { return CLuaLocomotion_SetAllowedSwitch( L, "nb_allow_climbing" ); }
static int CLuaLocomotion_GetClimbAllowed( lua_State *L )   { lua_pushboolean( L, CLuaLocomotion_GetAllowedSwitch( L, "nb_allow_climbing" ) ); return 1; }
static int CLuaLocomotion_SetJumpGapsAllowed( lua_State *L ){ return CLuaLocomotion_SetAllowedSwitch( L, "nb_allow_gap_jumping" ); }
static int CLuaLocomotion_GetJumpGapsAllowed( lua_State *L ){ lua_pushboolean( L, CLuaLocomotion_GetAllowedSwitch( L, "nb_allow_gap_jumping" ) ); return 1; }

#undef LUA_LOCO_BOT_AND_MOVER

static int CLuaLocomotion_SetStepHeight( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	if ( pBot )
		pBot->m_flLuaStepHeight = (float)luaL_checknumber( L, 2 );

	return 0;
}

static int CLuaLocomotion_GetStepHeight( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetStepHeight() : ( pBot ? pBot->m_flLuaStepHeight : 0.0f ) );
	return 1;
}

static int CLuaLocomotion_SetJumpHeight( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	if ( pBot )
		pBot->m_flLuaJumpHeight = (float)luaL_checknumber( L, 2 );

	return 0;
}

static int CLuaLocomotion_GetJumpHeight( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetMaxJumpHeight() : ( pBot ? pBot->m_flLuaJumpHeight : 0.0f ) );
	return 1;
}

static int CLuaLocomotion_SetDeathDropHeight( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	if ( pBot )
		pBot->m_flLuaDeathDropHeight = (float)luaL_checknumber( L, 2 );

	return 0;
}

static int CLuaLocomotion_GetDeathDropHeight( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetDeathDropHeight() : ( pBot ? pBot->m_flLuaDeathDropHeight : 0.0f ) );
	return 1;
}

static int CLuaLocomotion_SetAcceleration( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	if ( pBot )
		pBot->m_flLuaAcceleration = (float)luaL_checknumber( L, 2 );

	return 0;
}

static int CLuaLocomotion_GetAcceleration( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetMaxAcceleration() : ( pBot ? pBot->m_flLuaAcceleration : 0.0f ) );
	return 1;
}

static int CLuaLocomotion_SetDeceleration( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	if ( pBot )
		pBot->m_flLuaDeceleration = (float)luaL_checknumber( L, 2 );

	return 0;
}

static int CLuaLocomotion_GetDeceleration( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetMaxDeceleration() : ( pBot ? pBot->m_flLuaDeceleration : 0.0f ) );
	return 1;
}

static int CLuaLocomotion_FaceTowards( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->FaceTowards( luaL_checkvector( L, 2 ) );

	return 0;
}

static int CLuaLocomotion_IsStuck( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushboolean( L, pMover ? pMover->IsStuck() : false );
	return 1;
}

static int CLuaLocomotion_GetStuckDuration( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetStuckDuration() : 0.0f );
	return 1;
}

static int CLuaLocomotion_ClearStuck( lua_State *L )
{
	// GMod spells it ClearStuck(); the engine calls the same thing
	// ClearStuckStatus().
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->ClearStuckStatus( "lua" );

	return 0;
}

static int CLuaLocomotion_IsOnGround( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushboolean( L, pMover ? pMover->IsOnGround() : false );
	return 1;
}

static int CLuaLocomotion_GetGround( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushentity( L, pMover ? pMover->GetGround() : NULL );
	return 1;
}

static int CLuaLocomotion_GetVelocity( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		lua_pushvector( L, pMover->GetVelocity() );
	else
		lua_pushvector( L, vec3_origin );

	return 1;
}

static int CLuaLocomotion_SetVelocity( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	NextBotGroundLocomotion *pGround = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pGround )
		pGround->SetVelocity( luaL_checkvector( L, 2 ) );

	return 0;
}

static int CLuaLocomotion_GetSpeed( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetSpeed() : 0.0f );
	return 1;
}

static int CLuaLocomotion_GetGroundSpeed( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetGroundSpeed() : 0.0f );
	return 1;
}

static int CLuaLocomotion_GetRunSpeed( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetRunSpeed() : 0.0f );
	return 1;
}

static int CLuaLocomotion_GetWalkSpeed( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	lua_pushnumber( L, pMover ? pMover->GetWalkSpeed() : 0.0f );
	return 1;
}

static int CLuaLocomotion_Jump( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->Jump();

	return 0;
}

static int CLuaLocomotion_Stop( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->Stop();

	return 0;
}

static int CLuaLocomotion_Run( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->Run();

	return 0;
}

static int CLuaLocomotion_Walk( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->Walk();

	return 0;
}

static int CLuaLocomotion_Approach( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->Approach( luaL_checkvector( L, 2 ), (float)luaL_optnumber( L, 3, 1.0f ) );

	return 0;
}

static int CLuaLocomotion_DriveTo( lua_State *L )
{
	CLuaNextBot *pBot = CheckLocoBot( L );
	ILocomotion *pMover = pBot ? pBot->GetLocomotionInterface() : NULL;

	if ( pMover )
		pMover->DriveTo( luaL_checkvector( L, 2 ) );

	return 0;
}

static const luaL_Reg s_LuaLocomotionMethods[] =
{
	{ "SetDesiredSpeed",	CLuaLocomotion_SetDesiredSpeed },
	{ "GetDesiredSpeed",	CLuaLocomotion_GetDesiredSpeed },
	{ "SetSpeedLimit",		CLuaLocomotion_SetSpeedLimit },
	{ "GetSpeedLimit",		CLuaLocomotion_GetSpeedLimit },
	{ "SetStepHeight",		CLuaLocomotion_SetStepHeight },
	{ "GetStepHeight",		CLuaLocomotion_GetStepHeight },
	// HL2SB GMod compat: the jump / fatal-drop knobs (see the overrides above).
	{ "SetJumpHeight",		CLuaLocomotion_SetJumpHeight },
	{ "GetJumpHeight",		CLuaLocomotion_GetJumpHeight },
	{ "SetDeathDropHeight",	CLuaLocomotion_SetDeathDropHeight },
	{ "GetDeathDropHeight",	CLuaLocomotion_GetDeathDropHeight },
	// HL2SB GMod compat: the rest of the CLuaLocomotion surface the wiki lists.
	{ "GetGroundMotionVector",	CLuaLocomotion_GetGroundMotionVector },
	{ "GetGroundNormal",		CLuaLocomotion_GetGroundNormal },
	{ "IsClimbingOrJumping",	CLuaLocomotion_IsClimbingOrJumping },
	{ "IsUsingLadder",			CLuaLocomotion_IsUsingLadder },
	{ "IsAttemptingToMove",		CLuaLocomotion_IsAttemptingToMove },
	{ "IsAreaTraversable",		CLuaLocomotion_IsAreaTraversable },
	{ "JumpAcrossGap",			CLuaLocomotion_JumpAcrossGap },
	{ "GetNextBot",				CLuaLocomotion_GetNextBot },
	{ "SetGravity",				CLuaLocomotion_SetGravity },
	{ "GetGravity",				CLuaLocomotion_GetGravity },
	{ "SetMaxYawRate",			CLuaLocomotion_SetMaxYawRate },
	{ "GetMaxYawRate",			CLuaLocomotion_GetMaxYawRate },
	{ "GetCurrentAcceleration",	CLuaLocomotion_GetCurrentAcceleration },
	{ "SetAvoidAllowed",		CLuaLocomotion_SetAvoidAllowed },
	{ "GetAvoidAllowed",		CLuaLocomotion_GetAvoidAllowed },
	{ "SetClimbAllowed",		CLuaLocomotion_SetClimbAllowed },
	{ "GetClimbAllowed",		CLuaLocomotion_GetClimbAllowed },
	{ "SetJumpGapsAllowed",		CLuaLocomotion_SetJumpGapsAllowed },
	{ "GetJumpGapsAllowed",		CLuaLocomotion_GetJumpGapsAllowed },
	{ "SetAcceleration",	CLuaLocomotion_SetAcceleration },
	{ "GetAcceleration",	CLuaLocomotion_GetAcceleration },
	{ "SetDeceleration",	CLuaLocomotion_SetDeceleration },
	{ "GetDeceleration",	CLuaLocomotion_GetDeceleration },
	{ "FaceTowards",		CLuaLocomotion_FaceTowards },
	{ "IsStuck",			CLuaLocomotion_IsStuck },
	{ "GetStuckDuration",	CLuaLocomotion_GetStuckDuration },
	{ "ClearStuck",			CLuaLocomotion_ClearStuck },
	{ "IsOnGround",			CLuaLocomotion_IsOnGround },
	{ "GetGround",			CLuaLocomotion_GetGround },
	{ "GetVelocity",		CLuaLocomotion_GetVelocity },
	{ "SetVelocity",		CLuaLocomotion_SetVelocity },
	{ "GetSpeed",			CLuaLocomotion_GetSpeed },
	{ "GetGroundSpeed",		CLuaLocomotion_GetGroundSpeed },
	{ "GetRunSpeed",		CLuaLocomotion_GetRunSpeed },
	{ "GetWalkSpeed",		CLuaLocomotion_GetWalkSpeed },
	{ "Jump",				CLuaLocomotion_Jump },
	{ "Stop",				CLuaLocomotion_Stop },
	{ "Run",				CLuaLocomotion_Run },
	{ "Walk",				CLuaLocomotion_Walk },
	{ "Approach",			CLuaLocomotion_Approach },
	{ "DriveTo",			CLuaLocomotion_DriveTo },
	{ NULL,					NULL }
};

void LuaNextBot_PushLocomotion( lua_State *L, CLuaNextBot *pBot )
{
	LuaLocomotion_t *pLoco = (LuaLocomotion_t *)lua_newuserdata( L, sizeof( LuaLocomotion_t ) );

	pLoco->m_hBot = pBot;

	luaL_getmetatable( L, "CLuaLocomotion" );
	lua_setmetatable( L, -2 );
}

//=============================================================================
// The rest of the entity-level Lua surface a bot needs.
//
// Installed into the "CBaseAnimating" metatable, which is the one
// lua_pushanimating() gives every scripted entity, and whose __index walks on to
// CBaseEntity's (CBaseEntity___index, lbaseentity_shared.cpp:2202: the entity's
// script table first, then the metatable).
//=============================================================================
static int LUA_SDK_CLuaNextBot_BodyMoveXY( lua_State *L )
{
	CLuaNextBot *pBot = dynamic_cast< CLuaNextBot * >( luaL_checkanimating( L, 1 ) );

	if ( pBot )
		pBot->BodyMoveXY();

	return 0;
}

static int LUA_SDK_CBaseEntity_BoundingRadius( lua_State *L )
{
	CBaseEntity *pEntity = luaL_checkentity( L, 1 );

	if ( pEntity == NULL )
	{
		lua_pushnumber( L, 0.0f );
		return 1;
	}

	// GMod's Entity:BoundingRadius() - the radius of a sphere around the entity,
	// which is the quantity GMod's own effects size themselves against
	// (gamemodes/sandbox/entities/effects/entity_remove.lua:12).  The bounding
	// sphere of the AABB is the half-diagonal, not the half-extent.
	Vector vecMins, vecMaxs;
	pEntity->CollisionProp()->WorldSpaceAABB( &vecMins, &vecMaxs );

	Vector vecCenter = ( vecMins + vecMaxs ) * 0.5f;

	lua_pushnumber( L, ( vecMaxs - vecCenter ).Length() );
	return 1;
}

static void InstallNextBotEntityMethods( void )
{
	if ( L == NULL )
		return;

	// HL2SB: keyed on the state, not on the process -- see LuaNavMesh_Install for
	// why.  When the CLuaLocomotion metatable is already present in this state we
	// are done; when it is missing, `self.loco` is handed to the script as a
	// userdata with no __index, and the very first line of a nextbot's
	// Initialize()
	//
	//     self.loco:SetDeathDropHeight(600)
	//
	// raises "attempt to index a userdata value (field 'loco')" and abandons the
	// rest of Initialize().
	luaL_getmetatable( L, "CLuaLocomotion" );

	if ( !lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;					// this state already has it
	}

	lua_pop( L, 1 );

	// The navmesh surface (navmesh.*, CNavArea:*, Path()) is only ever wanted by
	// a nextbot script, so it is installed together with the first one.  It has
	// to be here rather than at lua_State creation: the nav mesh itself is
	// server state loaded at level init (gameinterface.cpp:1234).
	LuaNavMesh_Install();

	// The locomotion userdata's metatable, filled from the table above.
	luaL_newmetatable( L, "CLuaLocomotion" );

	// CLuaLocomotion:method() -> the metatable is its own __index.
	lua_pushvalue( L, -1 );
	lua_setfield( L, -2, "__index" );

	luaL_register( L, NULL, s_LuaLocomotionMethods );

	lua_pop( L, 1 );

	// The two entity methods.
	luaL_getmetatable( L, "CBaseAnimating" );

	if ( lua_istable( L, -1 ) )
	{
		lua_pushcfunction( L, LUA_SDK_CLuaNextBot_BodyMoveXY );
		lua_setfield( L, -2, "BodyMoveXY" );

		lua_pushcfunction( L, LUA_SDK_CBaseEntity_BoundingRadius );
		lua_setfield( L, -2, "BoundingRadius" );
	}

	lua_pop( L, 1 );
}

#endif // LUA_SDK
