//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ==========//
//
// Purpose: HL2SB (2026-09-26): the GMod timer library, engine-side.
//
// This file was REWRITTEN against a full reference of GMod's own timer
// library (client.dll / server.dll).  The registered member set, the object
// layout and every member's semantics below are taken from that analysis:
//
//   * luaL_Reg table (client.dll): Exists, Create, Start, Adjust,
//     Pause, UnPause, IsPaused, Toggle, Stop, Check, Remove, Destroy,
//     TimeLeft, RepsLeft, Simple   (+ the lua_dumptimers_* dump command)
//   * value object fields (relative to the value base):
//       +0x20 next (double)   +0x28 func ref (int)   +0x30 id (string)
//       +0x50 delay (float)   +0x54 reps (int)       +0x58 paused (bool)
//       +0x59 failed (bool)   +0x5a stopped (bool)
//   * Create  (worker ): `if (reps == 0) reps = -1;` -> 0 means
//     INFINITE, stored as -1 (so RepsLeft( infinite ) answers -1, NOT 0).
//   * Pause   (): if (!paused) { paused = 1; next = next - now; }
//                             (while paused, `next` holds the REMAINING time)
//   * UnPause (): if (paused) { paused = 0; stopped = 0; next = now + next; }
//   * Toggle  (): flip `paused` with the same next arithmetic as
//                            Pause/UnPause (pause-span is preserved).
//   * Stop    (): stopped = 1; next = 0;
//   * Start   (): paused = 0; stopped = 0; next = now + delay;
//   * TimeLeft(): 0 when missing / failed / reps==0 / PAUSED;
//                            else next - now   (GMod answers 0 while paused,
//                            it does NOT report a negative time).
//   * RepsLeft(): 0 when missing / failed / stopped; else reps.
//   * Executor(): for each due timer (not paused, not stopped,
//                            next <= now):
//                                next += delay            // catch-up cadence
//                                if (reps > 0) reps--     // -1 never decrements
//                                run func under pcall
//                                if (error) { failed = 1; print
//                                             "Timer Failed! [id][location]" }
//                                if (reps == 0 || failed) REMOVE the timer
//                            ^^^ the old HL2SB comment claimed GMod KEEPS a
//                            failing timer -- the analysis shows it is
//                            REMOVED, which is what this rewrite implements.
//
// Same deal as GMod otherwise: a registry of named timers plus a one-shot
// simple list, pumped once a frame from CHL2MPRules::Think (server) and the
// scripted-viewport paint (client).
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

// HL2SB (2026-09-21): local prototype on purpose (no header churn) -- failed
// timer callbacks go to the process error collector, not just the console.
void HL2SB_CollectLuaError( const char *pszError, const char *pszTraceback );

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// What a script passing repetitions = 0 means (GMod): repeat forever, stored
// as -1 so the executor's `if (reps > 0) reps--` never retires it.
#define HL2SB_TIMER_INFINITE -1

struct HL2SB_LuaTimer_t
{
	int			m_iFuncRef;		// callback id in the library-private table (see below)
	float		m_flDelay;		// seconds between fires
	int			m_iReps;		// fires left; INFINITE (-1) when created with 0
	double		m_flNext;		// absolute fire time; = REMAINING while paused; 0 while stopped
	bool		m_bPaused;
	bool		m_bStopped;
	bool		m_bFailed;		// callback errored last run -> retire on sight
	bool		m_bRemoved;
	CUtlString	m_sLocation;	// where Create() was called, for "Timer Failed!"
};

struct HL2SB_LuaSimpleTimer_t
{
	int			m_iFuncRef;		// callback id in the library-private table (see below)
	const void *m_pFunction;
	double		m_flFireTime;
	CUtlString	m_sLocation;
};

// Keyed by the identifier's string form (GMod allows any value as an
// identifier; strings cover every real addon).
typedef CUtlHashtable<CUtlString, HL2SB_LuaTimer_t> HL2SB_LuaTimerMap;
static HL2SB_LuaTimerMap g_LuaTimers;
static CUtlVector<HL2SB_LuaSimpleTimer_t> g_LuaSimpleTimers;
static CUtlVector<HL2SB_LuaSimpleTimer_t> g_LuaPendingSimpleTimers;
static const void *s_pRunningSimpleFunction = NULL;
static bool s_bRunningSimpleTimers = false;

// HL2SB (2026-10-19): the lua_State the records above were created against.
// Every m_iFuncRef is an index into THAT state's registry, so a record may only
// be interpreted while this still names the current state.  GMod never needs
// this bookkeeping because it keeps one state for the whole process; this fork
// builds a fresh one on every level load.
static lua_State *s_pTimerState = NULL;

// A record whose state is not the current one must be dropped WITHOUT touching
// its reference: the index belongs to a registry that is gone, and
// dereferencing or unreferencing it against the new registry would resolve to
// an unrelated object (or corrupt the free list).
static void HL2SB_TimerDropForeignRecords( const char *pszWhy )
{
	if ( g_LuaTimers.Count() == 0 && g_LuaSimpleTimers.Count() == 0 && g_LuaPendingSimpleTimers.Count() == 0 )
		return;

	luasrc_LuaErrorMsgF( "Timer registry reset (%s): dropped %d named / %d simple timer(s) created against Lua state %p, current state is %p\n",
		pszWhy, g_LuaTimers.Count(), g_LuaSimpleTimers.Count() + g_LuaPendingSimpleTimers.Count(),
		(void *)s_pTimerState, (void *)L );
	g_LuaTimers.Purge();
	g_LuaSimpleTimers.Purge();
	g_LuaPendingSimpleTimers.Purge();
	s_pRunningSimpleFunction = NULL;
	s_bRunningSimpleTimers = false;
}

// Adopt the current state, purging whatever belonged to another one.
static void HL2SB_TimerClaimState( const char *pszWhy )
{
	if ( s_pTimerState == L )
		return;
	HL2SB_TimerDropForeignRecords( pszWhy );
	s_pTimerState = L;
}

static double HL2SB_TimerNow( void )
{
	return gpGlobals->curtime;
}


//-----------------------------------------------------------------------------
// HL2SB (2026-10-19): callbacks are NOT luaL_ref() registry indices any more.
//
// The registry index space is shared with every other subsystem in the process
// (net receivers, hooks, panels, scripted entities, ...), so any stray double
// unreference anywhere recycles an index that a live timer still holds.  That is
// exactly what happened: a firing one-shot found a TABLE in its slot, because
// the same integer had been handed out twice.  Timers only need to find their
// own callback again, so they use their own key space instead: a table stored in
// the registry under the ADDRESS of a static (an unforgeable key unique to this
// library), indexed by a monotonically increasing integer.  Nothing outside this
// file can recycle those keys.
//-----------------------------------------------------------------------------
static int s_TimerCallbackKey;			// its address is the registry key
static int s_nNextCallbackId = 1;

static void HL2SB_TimerCallbackTable( lua_State *pL, bool bCreate )
{
	lua_pushlightuserdata( pL, &s_TimerCallbackKey );
	lua_rawget( pL, LUA_REGISTRYINDEX );
	if ( lua_istable( pL, -1 ) )
		return;

	lua_pop( pL, 1 );
	if ( !bCreate )
	{
		lua_pushnil( pL );
		return;
	}

	lua_newtable( pL );
	lua_pushlightuserdata( pL, &s_TimerCallbackKey );
	lua_pushvalue( pL, -2 );
	lua_rawset( pL, LUA_REGISTRYINDEX );
}

// Stores the function at stack index nArg; returns its id, or 0 on failure.
static int HL2SB_TimerStoreCallback( lua_State *pL, int nArg )
{
	if ( pL == NULL || !lua_isfunction( pL, nArg ) )
		return 0;

	HL2SB_TimerCallbackTable( pL, true );		// [tbl]
	const int nId = s_nNextCallbackId++;
	lua_pushinteger( pL, nId );
	lua_pushvalue( pL, nArg );
	lua_rawset( pL, -3 );
	lua_pop( pL, 1 );
	return nId;
}

// Pushes the callback for nId (exactly one value) when it is a function.
static bool HL2SB_TimerPushCallback( lua_State *pL, int nId )
{
	if ( pL == NULL || nId <= 0 )
		return false;

	HL2SB_TimerCallbackTable( pL, false );		// [tbl|nil]
	if ( !lua_istable( pL, -1 ) )
	{
		lua_pop( pL, 1 );
		return false;
	}

	lua_pushinteger( pL, nId );
	lua_rawget( pL, -2 );						// [tbl][fn|nil]
	lua_remove( pL, -2 );						// [fn|nil]
	if ( !lua_isfunction( pL, -1 ) )
	{
		lua_pop( pL, 1 );
		return false;
	}
	return true;
}

// Drops the callback for nId (a no-op when it is already gone).
static void HL2SB_TimerClearCallback( lua_State *pL, int nId )
{
	if ( pL == NULL || nId <= 0 )
		return;

	HL2SB_TimerCallbackTable( pL, false );
	if ( !lua_istable( pL, -1 ) )
	{
		lua_pop( pL, 1 );
		return;
	}

	lua_pushinteger( pL, nId );
	lua_pushnil( pL );
	lua_rawset( pL, -3 );
	lua_pop( pL, 1 );
}

// Clears a callback on the current state (every caller here runs inside it).
static void HL2SB_TimerDropCallback( int nId )
{
	HL2SB_TimerClearCallback( L, nId );
}

static CUtlString HL2SB_TimerKey( lua_State *L, int nArg )
{
	// luaL_checkstring accepts numbers too (coerces); strings are the real case.
	return CUtlString( luaL_checkstring( L, nArg ) );
}

// GMod records the caller's source location at Create/Simple time and prints it
// in the failure line (its worker reads luaL_where of the calling level, falling
// back to the literal "Unknown Location").  luaL_where( L, level ) pushes
// "chunk:line: " (or "" when unavailable).
static CUtlString HL2SB_TimerLocation( lua_State *L, int nLevel )
{
	CUtlString s;
	luaL_where( L, nLevel );
	const char *psz = lua_tostring( L, -1 );
	s = ( psz != NULL && psz[0] != '\0' ) ? psz : "Unknown Location";
	lua_pop( L, 1 );
	return s;
}

// GMod's "repetitions" contract: 0 means infinite and is stored as -1 so the
// executor never decrements it; every other value is the live fire count.
static int HL2SB_TimerRepsArg( lua_State *L, int nArg )
{
	int iReps = luaL_checkint( L, nArg );
	return ( iReps == 0 ) ? HL2SB_TIMER_INFINITE : iReps;
}

// timer.Create( identifier, delay, repetitions, func )
static int timer_Create (lua_State *L) {
	HL2SB_TimerClaimState( "timer.Create from a new state" );
	CUtlString sKey = HL2SB_TimerKey( L, 1 );
	float flDelay = (float)luaL_checknumber( L, 2 );
	int iReps = HL2SB_TimerRepsArg( L, 3 );
	luaL_checktype( L, 4, LUA_TFUNCTION );
	CUtlString sLoc = HL2SB_TimerLocation( L, 1 );

	HL2SB_LuaTimer_t t;
	t.m_iFuncRef = 0;
	t.m_flDelay = flDelay;
	t.m_iReps = iReps;
	t.m_flNext = HL2SB_TimerNow() + flDelay;
	t.m_bPaused = false;
	t.m_bStopped = false;
	t.m_bFailed = false;
	t.m_bRemoved = false;
	t.m_sLocation = sLoc;

	// Replace any existing timer under this id (GMod does the same and drops
	// the old callback for the GC).
	UtlHashHandle_t h = g_LuaTimers.Find( sKey );
	if ( h != g_LuaTimers.InvalidHandle() )
		HL2SB_TimerDropCallback( g_LuaTimers[ h ].m_iFuncRef );

	t.m_iFuncRef = HL2SB_TimerStoreCallback( L, 4 );

	if ( h != g_LuaTimers.InvalidHandle() )
		g_LuaTimers[h] = t;
	else
		g_LuaTimers.Insert( sKey, t );
	return 0;
}

// timer.Simple( delay, func ) -- anonymous one-shot.
static int timer_Simple (lua_State *L) {
	HL2SB_TimerClaimState( "timer.Simple from a new state" );
	float flDelay = (float)luaL_checknumber( L, 1 );
	luaL_checktype( L, 2, LUA_TFUNCTION );
	CUtlString sLoc = HL2SB_TimerLocation( L, 1 );

	HL2SB_LuaSimpleTimer_t s;
	s.m_iFuncRef = HL2SB_TimerStoreCallback( L, 2 );
	s.m_pFunction = lua_topointer( L, 2 );
	s.m_flFireTime = HL2SB_TimerNow() + flDelay;
	s.m_sLocation = sLoc;

	if ( s_bRunningSimpleTimers && s_pRunningSimpleFunction == s.m_pFunction )
		g_LuaPendingSimpleTimers.AddToTail( s );
	else
		g_LuaSimpleTimers.AddToTail( s );
	return 0;
}

// timer.Remove( identifier ) / timer.Destroy( identifier )
static int timer_Remove (lua_State *L) {
	CUtlString sKey = HL2SB_TimerKey( L, 1 );
	UtlHashHandle_t h = g_LuaTimers.Find( sKey );
	if ( h != g_LuaTimers.InvalidHandle() )
		g_LuaTimers[ h ].m_bRemoved = true;
	return 0;
}

// timer.Exists( identifier ) -> boolean
static int timer_Exists (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	lua_pushboolean( L, h != g_LuaTimers.InvalidHandle() && !g_LuaTimers[ h ].m_bRemoved );
	return 1;
}

// timer.Start( identifier ) -- (re)start from a full delay (clears paused+stopped).
static int timer_Start (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h != g_LuaTimers.InvalidHandle() )
	{
		HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
		t.m_bPaused = false;
		t.m_bStopped = false;
		t.m_flNext = HL2SB_TimerNow() + t.m_flDelay;
	}
	return 0;
}

// timer.Stop( identifier ) -- halt; Exists stays true, Start rewinds it.
static int timer_Stop (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h != g_LuaTimers.InvalidHandle() )
	{
		HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
		t.m_bStopped = true;
		t.m_flNext = 0.0;
	}
	return 0;
}

// timer.Pause( identifier ) -- GMod: fold the countdown into `next` as the
// REMAINING time.  While paused, TimeLeft answers 0 (it looks at the paused
// flag, not at `next`).
static int timer_Pause (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h != g_LuaTimers.InvalidHandle() )
	{
		HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
		if ( !t.m_bPaused )
		{
			t.m_bPaused = true;
			t.m_flNext = t.m_flNext - HL2SB_TimerNow();
		}
	}
	return 0;
}

// timer.UnPause( identifier ) -- resume from the stored remaining time.
static int timer_UnPause (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h != g_LuaTimers.InvalidHandle() )
	{
		HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
		if ( t.m_bPaused )
		{
			t.m_bStopped = false;
			t.m_bPaused = false;
			t.m_flNext = HL2SB_TimerNow() + t.m_flNext;
		}
	}
	return 0;
}

// timer.IsPaused( identifier ) -> boolean
static int timer_IsPaused (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	lua_pushboolean( L, h != g_LuaTimers.InvalidHandle() && g_LuaTimers[ h ].m_bPaused );
	return 1;
}

// timer.Toggle( identifier ) -> boolean (the NEW paused state), GMod arithmetic:
// pausing folds the remaining into `next`; resuming re-anchors it to now.
static int timer_Toggle (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h == g_LuaTimers.InvalidHandle() )
	{
		lua_pushboolean( L, false );
		return 1;
	}
	HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
	if ( !t.m_bPaused )
	{
		t.m_bPaused = true;
		t.m_flNext = t.m_flNext - HL2SB_TimerNow();
	}
	else
	{
		t.m_bStopped = false;
		t.m_bPaused = false;
		t.m_flNext = HL2SB_TimerNow() + t.m_flNext;
	}
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
	// wiki: repetitions = nil and func = nil KEEP the previous value - and a
	// simply-omitted argument is LUA_TNONE, not LUA_TNIL, so lua_isnil() alone
	// would treat "Adjust( id, delay )" as "reps was passed" and argerror out.
	if ( !lua_isnoneornil( L, 3 ) )
		t.m_iReps = HL2SB_TimerRepsArg( L, 3 );
	if ( !lua_isnoneornil( L, 4 ) )
	{
		luaL_checktype( L, 4, LUA_TFUNCTION );
		HL2SB_TimerDropCallback( t.m_iFuncRef );
		t.m_iFuncRef = HL2SB_TimerStoreCallback( L, 4 );
	}

	// Adjust restarts the countdown with the new delay.  On a paused timer
	// `next` holds the remaining time, so it becomes the new delay.
	if ( t.m_bPaused )
		t.m_flNext = t.m_flDelay;
	else
		t.m_flNext = HL2SB_TimerNow() + t.m_flDelay;

	lua_pushboolean( L, true );
	return 1;
}

// timer.TimeLeft( identifier ) -> number | false
// GMod (worker ): 0 when missing, failed, exhausted (reps==0) or
// STOPPED; when PAUSED it answers the negated frozen remaining (-next); else
// the plain next - now.  NOTE there is no pause-time field in GMod's timer
// object -- so a paused TimeLeft does NOT drift, it is the frozen negative
// remaining.
static int timer_TimeLeft (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h == g_LuaTimers.InvalidHandle() )
	{
		lua_pushboolean( L, false );
		return 1;
	}
	HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
	if ( t.m_bFailed || t.m_iReps == 0 || t.m_bStopped )
	{
		lua_pushnumber( L, 0 );
		return 1;
	}
	if ( t.m_bPaused )
	{
		lua_pushnumber( L, -t.m_flNext );
		return 1;
	}
	lua_pushnumber( L, t.m_flNext - HL2SB_TimerNow() );
	return 1;
}

// timer.RepsLeft( identifier ) -> number
// GMod: 0 when missing, failed, or stopped; otherwise reps (-1 = infinite).
static int timer_RepsLeft (lua_State *L) {
	UtlHashHandle_t h = g_LuaTimers.Find( HL2SB_TimerKey( L, 1 ) );
	if ( h == g_LuaTimers.InvalidHandle() )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}
	HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
	if ( t.m_bFailed || t.m_bStopped )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}
	lua_pushinteger( L, t.m_iReps );
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
	lua_pop( L, 1 );
	return 1;
}

//-----------------------------------------------------------------------------
// HL2SB: the per-frame pump.  GMod cadence: due timers advance by exactly
// their delay (`next += delay`) and a callback that errors is retired.
//-----------------------------------------------------------------------------
LUA_API void HL2SB_TimerTick ( void )
{
	if ( L == NULL )
		return;

	// HL2SB (2026-10-19): never interpret a reference against a registry it was
	// not created in.  See s_pTimerState.
	HL2SB_TimerClaimState( "timer pump on a new state" );

	double flNow = HL2SB_TimerNow();

	// --- one-shot simple timers ---
	FOR_EACH_VEC( g_LuaPendingSimpleTimers, pending )
		g_LuaSimpleTimers.AddToTail( g_LuaPendingSimpleTimers[ pending ] );
	g_LuaPendingSimpleTimers.Purge();
	s_bRunningSimpleTimers = true;
	int nSimpleCallbacks = 0;
	for ( int i = 0; i < g_LuaSimpleTimers.Count() && nSimpleCallbacks < 1024; )
	{
		HL2SB_LuaSimpleTimer_t &s = g_LuaSimpleTimers[ i ];
		if ( flNow < s.m_flFireTime )
		{
			++i;
			continue;
		}

		int iRef = s.m_iFuncRef;
		const void *pFunction = s.m_pFunction;
		CUtlString sLoc = s.m_sLocation;
		g_LuaSimpleTimers.Remove( i );

		// HL2SB (2026-10-19): the callback lives in this library's own keyed
		// table, so a missing entry means the timer was retired or its state is
		// gone -- never "some other subsystem recycled our slot".
		if ( !HL2SB_TimerPushCallback( L, iRef ) )
		{
			luasrc_LuaErrorMsgF( "Timer dropped [Simple][%s]: callback id %d is gone\n",
				sLoc.Get(), iRef );
			continue;
		}
		HL2SB_TimerClearCallback( L, iRef );
		++nSimpleCallbacks;
		s_pRunningSimpleFunction = pFunction;
		if ( lua_pcall( L, 0, 0, 0 ) != 0 )
		{
			const char *pszErr = lua_tostring( L, -1 );
			// HL2SB (sbrust): GMod prints the real error next to the "Timer Failed!"
			// head; the raw Warning here showed only the head on the console and
			// carried neither the message nor hl2sb_lua.log.
			luasrc_LuaErrorMsgF( "Timer Failed! [Simple][%s]\n%s", sLoc.Get(),
				pszErr ? pszErr : "(no error message)" );
			luaL_traceback( L, L, pszErr, 0 );
			if ( lua_isstring( L, -1 ) )
				luasrc_LuaErrorMsg( lua_tostring( L, -1 ) );
			HL2SB_CollectLuaError( pszErr, lua_tostring( L, -1 ) );
			lua_pop( L, 2 );
		}
		s_pRunningSimpleFunction = NULL;
	}
	s_bRunningSimpleTimers = false;

	// --- named timers ---
	// Collect the due set first: a callback may Create/Remove timers, and
	// mutating the hashtable while walking it would invalidate the iterator.
	CUtlVector<CUtlString> due;
	CUtlVector<CUtlString> removed;
	FOR_EACH_HASHTABLE( g_LuaTimers, hIter )
	{
		const HL2SB_LuaTimer_t &t = g_LuaTimers[ hIter ];
		if ( t.m_bRemoved )
			removed.AddToTail( g_LuaTimers.Key( hIter ) );
		else if ( !t.m_bPaused && !t.m_bStopped && flNow >= t.m_flNext )
			due.AddToTail( g_LuaTimers.Key( hIter ) );
	}
	for ( int i = 0; i < removed.Count(); ++i )
	{
		UtlHashHandle_t h = g_LuaTimers.Find( removed[ i ] );
		if ( h != g_LuaTimers.InvalidHandle() && g_LuaTimers[ h ].m_bRemoved )
		{
			HL2SB_TimerDropCallback( g_LuaTimers[ h ].m_iFuncRef );
			g_LuaTimers.Remove( removed[ i ] );
		}
	}

	for ( int i = 0; i < due.Count(); ++i )
	{
		UtlHashHandle_t h = g_LuaTimers.Find( due[ i ] );
		if ( h == g_LuaTimers.InvalidHandle() )
			continue; // an earlier callback removed it already

		HL2SB_LuaTimer_t &t = g_LuaTimers[ h ];
		if ( t.m_bRemoved || t.m_bPaused || t.m_bStopped || flNow < t.m_flNext )
			continue;

		// GMod advances by exactly the delay (keeps the original cadence).
		t.m_flNext = t.m_flNext + t.m_flDelay;

		// Decrement only a real count; the infinite sentinel (-1) never retires.
		if ( t.m_iReps > 0 )
			t.m_iReps--;

		int iRepsAfter = t.m_iReps;
		int iRef = t.m_iFuncRef;
		CUtlString sLoc = t.m_sLocation;
		CUtlString sKey = due[ i ];

		bool bFailed = false;
		// HL2SB (2026-10-19): the callback lives in this library's own keyed
		// table; a missing entry means the timer was retired (or belongs to a
		// state that no longer exists), never a recycled registry slot.
		if ( !HL2SB_TimerPushCallback( L, iRef ) )
		{
			luasrc_LuaErrorMsgF( "Timer dropped [%s][%s]: callback id %d is gone\n",
				sKey.Get(), sLoc.Get(), iRef );
			bFailed = true;
		}
		else if ( lua_pcall( L, 0, 0, 0 ) != 0 )
		{
			bFailed = true;
			const char *pszErr = lua_tostring( L, -1 );
			// HL2SB (sbrust): same fix as the simple-timer branch -- the head plus
			// the real error both go to the console and to hl2sb_lua.log.
			luasrc_LuaErrorMsgF( "Timer Failed! [%s][%s]\n%s", sKey.Get(), sLoc.Get(),
				pszErr ? pszErr : "(no error message)" );
			luaL_traceback( L, L, pszErr, 0 );
			if ( lua_isstring( L, -1 ) )
				luasrc_LuaErrorMsg( lua_tostring( L, -1 ) );
			HL2SB_CollectLuaError( pszErr, lua_tostring( L, -1 ) );
			lua_pop( L, 2 );
		}

		// GMod retires a timer when its repetitions are used up OR its callback
		// errored.  Re-resolve + confirm the slot still holds THIS callback: a
		// callback may have replaced the timer under the same id, and that
		// replacement must keep running.
		UtlHashHandle_t h2 = g_LuaTimers.Find( sKey );
		if ( h2 == g_LuaTimers.InvalidHandle() )
			continue;

		HL2SB_LuaTimer_t &t2 = g_LuaTimers[ h2 ];
		if ( t2.m_iFuncRef != iRef )
			continue; // replaced by the callback

		if ( bFailed )
			t2.m_bFailed = true;

		if ( iRepsAfter == 0 || bFailed || t2.m_bRemoved )
		{
			HL2SB_TimerDropCallback( t2.m_iFuncRef );
			g_LuaTimers.Remove( sKey );
		}
	}
}

//-----------------------------------------------------------------------------
// HL2SB: drop the records (and their callbacks) before the state closes.
// A callback id only exists in the state that created it, so clearing against a
// different state is a harmless no-op -- no registry index bookkeeping is shared
// with anything else any more.
//-----------------------------------------------------------------------------
LUA_API void HL2SB_TimerShutdown ( void )
{
	FOR_EACH_HASHTABLE( g_LuaTimers, hIter )
		HL2SB_TimerDropCallback( g_LuaTimers[ hIter ].m_iFuncRef );
	g_LuaTimers.Purge();

	FOR_EACH_VEC( g_LuaSimpleTimers, i )
		HL2SB_TimerDropCallback( g_LuaSimpleTimers[ i ].m_iFuncRef );
	g_LuaSimpleTimers.Purge();
	FOR_EACH_VEC( g_LuaPendingSimpleTimers, i )
		HL2SB_TimerDropCallback( g_LuaPendingSimpleTimers[ i ].m_iFuncRef );
	g_LuaPendingSimpleTimers.Purge();
	s_pRunningSimpleFunction = NULL;
	s_bRunningSimpleTimers = false;

	s_pTimerState = NULL;
}

//-----------------------------------------------------------------------------
// HL2SB (2026-10-19): called from luasrc_init() the moment a new state exists.
//
// This fork builds a fresh Lua state on every level load (luasrc_init does
// L = lua_open()), while GMod -- whose timer is the only implementation (a GMod
// install has no lua/includes/modules/timer.lua; timer lives in the game DLLs
// and is pumped once per frame) -- keeps one state for the whole process.
//
// A record that outlives its state can never be run again: its callback id only
// exists in the dead state's private table.  Take ownership of the new state and
// drop the leftovers, so the pump cannot even try.
//-----------------------------------------------------------------------------
LUA_API void HL2SB_TimerResetForNewState ( lua_State *pL )
{
	g_LuaTimers.Purge();
	g_LuaSimpleTimers.Purge();
	g_LuaPendingSimpleTimers.Purge();
	s_pRunningSimpleFunction = NULL;
	s_bRunningSimpleTimers = false;
	s_pTimerState = pL;
}
