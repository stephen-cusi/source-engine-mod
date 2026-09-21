//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ==========//
//
// Purpose: HL2SB (2026-09-21): the GMod timer library, engine-side.
//
// GMod implements timer.* in C++ inside its engine and drives it every tick
// (its DoSimpleTimers walks a list of due callbacks per tick).  The pure-Lua
// reimplementation this fork used before (lua/includes/modules/timer.lua) was
// driven by the "Think" gamemode hook AND treated reps = 0 as "fire once":
// a timer.Create(id, delay, 0, fn) -- GMod's infinite-repeat form -- fired a
// single time and deleted itself, which is why every addon using the idiomatic
// timer.Create(..., 0, ...) silently lost its timers.
//
// This is the same deal GMod makes: a registry of named timers plus a simple
// one-shot list, pumped once per frame from CHL2MPRules::Think (server) and
// the scripted-viewport paint (client).  Callbacks run under lua_pcall; a
// failing callback is reported and the timer KEPT (GMod keeps it too).
//
// $NoKeywords: $
//===========================================================================//

#define ltimer_cpp

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "tier1/utlstring.h"
#include "tier1/utlhashtable.h"
#include "tier1/utlvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// What a script passing repetitions = 0 means: repeat forever.
#define HL2SB_TIMER_INFINITE 0

struct HL2SB_LuaTimer_t
{
	int m_iFuncRef;		// luaL_ref into LUA_REGISTRYINDEX
	float m_flDelay;	// seconds between fires
	int m_iReps;		// fires left; 0 as passed in means infinite
	float m_flNext;		// absolute CurTime of the next fire
	bool m_bPaused;
	bool m_bRunning;
};

struct HL2SB_LuaSimpleTimer_t
{
	int m_iFuncRef;
	float m_flFireTime;
};

// Keyed by the identifier's string form (GMod allows any value as an
// identifier; strings cover every real addon -- table keys were impossible in
// the Lua reimplementation too, because it keyed a Lua table by the raw id).
typedef CUtlHashtable<CUtlString, HL2SB_LuaTimer_t> HL2SB_LuaTimerMap;
static HL2SB_LuaTimerMap g_LuaTimers;
static CUtlVector<HL2SB_LuaSimpleTimer_t> g_LuaSimpleTimers;

static float HL2SB_TimerNow( void )
{
	return gpGlobals->curtime;
}

static void HL2SB_TimerUnref( int iRef )
{
	if ( L != NULL && iRef != LUA_NOREF )
		luaL_unref( L, LUA_REGISTRYINDEX, iRef );
}

static CUtlString HL2SB_TimerKey( lua_State *L, int nArg )
{
	// luaL_checkstring accepts numbers too (coerces), matching GMod treating
	// 5 and "5" as the same id only loosely -- strings are the real case.
	return CUtlString( luaL_checkstring( L, nArg ) );
}

// timer.Create( identifier, delay, repetitions, func )
static int timer_Create (lua_State *L) {
	CUtlString sKey = HL2SB_TimerKey( L, 1 );
	float flDelay = (float)luaL_checknumber( L, 2 );
	int iReps = luaL_checkint( L, 3 );
	luaL_checktype( L, 4, LUA_TFUNCTION );

	HL2SB_LuaTimer_t t;
	t.m_iFuncRef = LUA_NOREF;
	t.m_flDelay = flDelay;
	t.m_iReps = iReps;
	t.m_flNext = HL2SB_TimerNow() + flDelay;
	t.m_bPaused = false;
	t.m_bRunning = true;

	// Replace any existing timer under this id (GMod does the same and drops
	// the old callback for the GC).
	UtlHashHandle_t h = g_LuaTimers.Find( sKey );
	if ( h != g_LuaTimers.InvalidHandle() )
		HL2SB_TimerUnref( g_LuaTimers[ h ].m_iFuncRef );

	lua_pushvalue( L, 4 );
	t.m_iFuncRef = luaL_ref( L, LUA_REGISTRYINDEX );

	g_LuaTimers.Insert( sKey, t );
	return 0;
}

// timer.Simple( delay, func ) -- anonymous one-shot.
static int timer_Simple (lua_State *L) {
	float flDelay = (float)luaL_checknumber( L, 1 );
	luaL_checktype( L, 2, LUA_TFUNCTION );

	HL2SB_LuaSimpleTimer_t s;
	lua_pushvalue( L, 2 );
	s.m_iFuncRef = luaL_ref( L, LUA_REGISTRYINDEX );
	s.m_flFireTime = HL2SB_TimerNow() + flDelay;
	g_LuaSimpleTimers.AddToTail( s );
	return 0;
}

// timer.Remove( identifier ) / timer.Destroy( identifier )
static int timer_Remove (lua_State *L) {
	CUtlString sKey = HL2SB_TimerKey( L, 1 );
	UtlHashHandle_t h = g_LuaTimers.Find( sKey );
	if ( h != g_LuaTimers.InvalidHandle() )
	{
		HL2SB_TimerUnref( g_LuaTimers[ h ].m_iFuncRef );
		g_LuaTimers.Remove( sKey );
	}
	return 0;
}

// timer.Exists( identifier ) -> boolean
static int timer_Exists (lua_State *L) {
	lua_pushboolean( L, g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) ) != g_LuaTimers.InvalidHandle() );
	return 1;
}

// timer.Start( identifier ) -- (re)start from a full delay.
static int timer_Start (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h != g_LuaTimers.InvalidHandle() )
	{
		HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
		t.m_bRunning = true;
		t.m_bPaused = false;
		t.m_flNext = HL2SB_TimerNow() + t.m_flDelay;
	}
	return 0;
}

// timer.Stop( identifier ) -- halt; Exists stays true, Start rewinds it.
static int timer_Stop (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h != g_LuaTimers.InvalidHandle() )
		g_LuaTimers[ h ].m_bRunning = false;
	return 0;
}

// timer.Pause( identifier )
static int timer_Pause (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h != g_LuaTimers.InvalidHandle() )
		g_LuaTimers[ h ].m_bPaused = true;
	return 0;
}

// timer.UnPause( identifier )
static int timer_UnPause (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h != g_LuaTimers.InvalidHandle() )
	{
		HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
		t.m_bPaused = false;
		t.m_flNext = HL2SB_TimerNow() + t.m_flDelay;
	}
	return 0;
}

// timer.Toggle( identifier ) -> boolean (the new paused state)
static int timer_Toggle (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h == g_LuaTimers.InvalidHandle() )
	{
		lua_pushboolean( L, false );
		return 1;
	}
	HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
	t.m_bPaused = !t.m_bPaused;
	if ( !t.m_bPaused )
		t.m_flNext = HL2SB_TimerNow() + t.m_flDelay;
	lua_pushboolean( L, t.m_bPaused );
	return 1;
}

// timer.Adjust( identifier, delay, repetitions = nil, func = nil ) -> boolean
static int timer_Adjust (lua_State *L) {
	CUtlString sKey = HL2SB_TimerKey( L, 1 );
	UtlHashHandle_t h = g_LuaTimers.Find( sKey );
	if ( h == g_LuaTimers.InvalidHandle() )
	{
		lua_pushboolean( L, false );
		return 1;
	}
	HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];

	t.m_flDelay = (float)luaL_checknumber( L, 2 );
	if ( !lua_isnil( L, 3 ) )
		t.m_iReps = luaL_checkint( L, 3 );
	if ( !lua_isnil( L, 4 ) )
	{
		luaL_checktype( L, 4, LUA_TFUNCTION );
		HL2SB_TimerUnref( t.m_iFuncRef );
		lua_pushvalue( L, 4 );
		t.m_iFuncRef = luaL_ref( L, LUA_REGISTRYINDEX );
	}
	t.m_flNext = HL2SB_TimerNow() + t.m_flDelay;

	lua_pushboolean( L, true );
	return 1;
}

// timer.TimeLeft( identifier ) -> number | false (false when it doesn't exist)
static int timer_TimeLeft (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h == g_LuaTimers.InvalidHandle() )
	{
		lua_pushboolean( L, false );
		return 1;
	}
	float flLeft = g_LuaTimers[ h ].m_flNext - HL2SB_TimerNow();
	lua_pushnumber( L, flLeft < 0.0f ? 0.0f : flLeft );
	return 1;
}

// timer.RepsLeft( identifier ) -> number
static int timer_RepsLeft (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	lua_pushinteger( L, h != g_LuaTimers.InvalidHandle() ? g_LuaTimers[ h ].m_iReps : 0 );
	return 1;
}

// timer.IsPaused( identifier ) -> boolean
static int timer_IsPaused (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	lua_pushboolean( L, h != g_LuaTimers.InvalidHandle() && g_LuaTimers[ h ].m_bPaused );
	return 1;
}

// timer.Check() -- deprecated GMod no-op.
static int timer_Check (lua_State *L) {
	return 0;
}

static const luaL_Reg timer_funcs[] = {
	{ "Create",    timer_Create },
	{ "Simple",    timer_Simple },
	{ "Remove",    timer_Remove },
	{ "Destroy",   timer_Remove },
	{ "Exists",    timer_Exists },
	{ "Start",     timer_Start },
	{ "Stop",      timer_Stop },
	{ "Pause",     timer_Pause },
	{ "UnPause",   timer_UnPause },
	{ "Toggle",    timer_Toggle },
	{ "Adjust",    timer_Adjust },
	{ "TimeLeft",  timer_TimeLeft },
	{ "RepsLeft",  timer_RepsLeft },
	{ "IsPaused",  timer_IsPaused },
	{ "Check",     timer_Check },
	{ NULL, NULL }
};

LUALIB_API int luaopen_timer (lua_State *L) {
	luaL_register( L, "timer", timer_funcs );
	// Marker for lua/includes/modules/timer.lua: when this field is present the
	// engine library exists and the pure-Lua fallback must not load.
	lua_pushboolean( L, true );
	lua_setfield( L, -2, "IsEngine" );
	return 1;
}

//-----------------------------------------------------------------------------
// HL2SB: the per-frame pump.  Server calls it from CHL2MPRules::Think, the
// client from its scripted-viewport paint.  Callbacks fire protected: an error
// is reported and the timer is KEPT (GMod keeps erroring timers alive too).
// Due timers are collected before any callback runs, so a callback may freely
// Create / Remove timers, including the one currently firing.
//-----------------------------------------------------------------------------
LUA_API void HL2SB_TimerTick ( void )
{
	if ( L == NULL )
		return;

	float flNow = HL2SB_TimerNow();

	// Simple timers: fire in queue order.
	for ( int i = 0; i < g_LuaSimpleTimers.Count(); )
	{
		if ( flNow < g_LuaSimpleTimers[ i ].m_flFireTime )
		{
			++i;
			continue;
		}

		int iRef = g_LuaSimpleTimers[ i ].m_iFuncRef;
		g_LuaSimpleTimers.FastRemove( i );

		lua_rawgeti( L, LUA_REGISTRYINDEX, iRef );
		luaL_unref( L, LUA_REGISTRYINDEX, iRef );
		if ( lua_pcall( L, 0, 0, 0 ) != 0 )
		{
			Warning( "[timer] simple timer failed: %s\n", lua_tostring( L, -1 ) );
			lua_pop( L, 1 );
		}
	}

	// Named timers: snapshot who is due first -- callbacks may mutate the
	// table while we are walking it.
	CUtlVector<CUtlString> due;
	FOR_EACH_HASHTABLE( g_LuaTimers, hIter )
	{
		const HL2SB_LuaTimer_t &t = g_LuaTimers[ hIter ];
		if ( t.m_bRunning && !t.m_bPaused && flNow >= t.m_flNext )
			due.AddToTail( g_LuaTimers.Key( hIter ) );
	}

	for ( int i = 0; i < due.Count(); ++i )
	{
		UtlHashHandle_t h = g_LuaTimers.Find( due[ i ] );
		if ( h == g_LuaTimers.InvalidHandle() )
			continue; // an earlier callback removed it already

		HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];

		if ( t.m_iReps > HL2SB_TIMER_INFINITE )
		{
			t.m_iReps--;
			if ( t.m_iReps <= 0 )
			{
				// Finite timer exhausted: drop it before the call so the
				// callback sees Exists() == false, exactly like GMod.
				int iRef = t.m_iFuncRef;
				g_LuaTimers.Remove( due[ i ] );
				lua_rawgeti( L, LUA_REGISTRYINDEX, iRef );
				luaL_unref( L, LUA_REGISTRYINDEX, iRef );
				if ( lua_pcall( L, 0, 0, 0 ) != 0 )
				{
					Warning( "[timer] '%s' failed: %s\n", due[ i ].Get(), lua_tostring( L, -1 ) );
					lua_pop( L, 1 );
				}
				continue;
			}
		}

		t.m_flNext = flNow + t.m_flDelay;

		lua_rawgeti( L, LUA_REGISTRYINDEX, t.m_iFuncRef );
		if ( lua_pcall( L, 0, 0, 0 ) != 0 )
		{
			Warning( "[timer] '%s' failed: %s\n", due[ i ].Get(), lua_tostring( L, -1 ) );
			lua_pop( L, 1 );
		}
	}
}

//-----------------------------------------------------------------------------
// HL2SB: the registry holds luaL_ref() indices into THIS state; drop them
// before the state closes (same reasoning as luasrc_net_reset).
//-----------------------------------------------------------------------------
LUA_API void HL2SB_TimerShutdown ( void )
{
	FOR_EACH_HASHTABLE( g_LuaTimers, hIter )
		HL2SB_TimerUnref( g_LuaTimers[ hIter ].m_iFuncRef );
	g_LuaTimers.Purge();

	FOR_EACH_VEC( g_LuaSimpleTimers, i )
		HL2SB_TimerUnref( g_LuaSimpleTimers[ i ].m_iFuncRef );
	g_LuaSimpleTimers.Purge();
}
