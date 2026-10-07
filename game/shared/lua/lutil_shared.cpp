//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#define lutil_shared_cpp

#include "cbase.h"
#include "luamanager.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "lgametrace.h"
#include "mathlib/lvector.h"
#include "engine/IEngineSound.h"
#include "tier1/keyvaluesjson.h"	// HL2SB GMod compat: util.JSONToTable decode
#include "tier1/checksum_crc.h"		// HL2SB GMod compat: util.CRC
#include "tier1/checksum_md5.h"		// HL2SB GMod compat: util.MD5
#include "tier1/checksum_sha1.h"	// HL2SB GMod compat: util.SHA1
#include "../../../utils/lzma/C/LzmaEnc.h"	// HL2SB GMod compat: util.Compress (SDK encoder linked from tier1)
#include "../../../utils/lzma/C/LzmaDec.h"	// HL2SB GMod compat: util.Decompress (decoder linked from tier1)
#include "vphysics_interface.h"		// HL2SB GMod compat: surfacedata_t / IPhysicsSurfaceProps
#include "activitylist.h"
#include "filesystem.h"
#include "tier1/lzmaDecoder.h"		// HL2SB GMod compat: CLZMA ("LZMA!" blobs)
#include "eventlist.h"				// HL2SB GMod compat: util.GetAnimEvent*By*
#include "ltakedamageinfo.h"		// HL2SB GMod compat: util.BlastDamageInfo
// HL2SB: CSoundEnvelopeController, which owns the engine's CSoundPatch objects.
// CSoundPatch itself is defined only inside game/shared/soundenvelope.cpp, so
// the controller interface is the only public handle on it -- which is exactly
// what the GMod audio channel below needs (see luasrc_CreateSound).
#include "soundenvelope.h"
#include "leffect_dispatch_data.h"
#include <lColor.h>
#include "hl2sb_framestats_cat.h"	// HL2SB frame profiler: per-category buckets

#ifdef CLIENT_DLL
#include "c_te_effect_dispatch.h"
// HL2SB: GMod's lua/effects/*.lua spawner (game/client/lua/lua_effects.cpp).
// Declared here for the same reason c_te_effect_dispatch.cpp declares it: this
// file is not compiled with game/client/lua on its include path.
bool HL2SB_CreateLuaEffect( const char *pszName, const CEffectData &data );
#else
#include "te_effect_dispatch.h"
#include "SpriteTrail.h"
#include "util.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
#include <stdarg.h>
#include "utldict.h"

/*
** HL2SB: "ask the engine for this resource once, then remember it" cache, plus a
** bounded one-shot diagnostic printer.
**
** The Nyan Gun only ever hands resource names to the engine at run time (the
** trail's .vmt inside util.SpriteTrail, the waves inside CreateSound, the
** particle system inside ParticleSystems.Precache), so the disk/decode/upload
** cost of each one lands on a *live* throw -- which is the first-second stutter
** the frame analysis saw.  There is no way to move that cost into the map's
** precache phase (the name is not known then), but it can at least be made to
** happen exactly once per name per session instead of on every call: the old
** bindings re-asked the engine every time, and the log shows it
** ("Late precache of nyan/rainbow.vmt", "Direct precache of <wave>").
**
** Both dictionaries are created lazily: a file-scope CUtlDict would run its
** constructor while the DLL is still loading, i.e. before the engine has
** installed its allocator.
*/
static CUtlDict<int, int> *s_pHL2SBPrecached = NULL;
static CUtlDict<int, int> *s_pHL2SBWarned = NULL;

// Returns true the first time this exact name is passed, false afterwards.
// Non-static: game/shared/lua/lparticle_system.cpp shares the same cache.
bool HL2SB_PrecacheOnce (const char *pszName) {
  if ( pszName == NULL || pszName[0] == '\0' )
    return false;

  if ( s_pHL2SBPrecached == NULL )
    s_pHL2SBPrecached = new CUtlDict<int, int>();

  // Bounded: a runaway script must not be able to grow this without limit.
  if ( s_pHL2SBPrecached->Count() >= 512 )
    return false;

  if ( s_pHL2SBPrecached->Find( pszName ) != s_pHL2SBPrecached->InvalidIndex() )
    return false;

  s_pHL2SBPrecached->Insert( pszName, 1 );
  return true;
}

/*
** One log line per distinct key, at most 32 keys per DLL load.  Used on the
** branches that can leave an engine object in a state Lua cannot see (a
** CSoundPatch whose owner entity is gone, a NULL attacker in BlastDamage, a
** colliding entity with no physics object), so that the *next* crash log names
** the path that was live instead of leaving a bare access violation.
** Non-static: game/shared/lua/basescripted.cpp uses it too.
*/
void HL2SB_WarnOnce (const char *pszKey, const char *pszFormat, ...) {
  if ( s_pHL2SBWarned == NULL )
    s_pHL2SBWarned = new CUtlDict<int, int>();

  // HL2SB: this was 32 -- a single session with a few addons burns through
  // that in the first minute, and EVERY later diagnostic (tracer chain,
  // effect templates, ...) is then silently swallowed.  That cost a whole
  // debugging round: "no line in the log" was read as "the code never ran"
  // when it only meant "the one-shot budget was spent".
  if ( s_pHL2SBWarned->Count() >= 256 )
    return;

  if ( s_pHL2SBWarned->Find( pszKey ) != s_pHL2SBWarned->InvalidIndex() )
    return;

  s_pHL2SBWarned->Insert( pszKey, 1 );

  char szBuf[ 320 ];
  va_list args;
  va_start( args, pszFormat );
  Q_vsnprintf( szBuf, sizeof( szBuf ), pszFormat, args );
  va_end( args );

  luasrc_LuaWarnMsgF( "[HL2SB] %s\n", szBuf );
}

/*
** HL2SB: undo a HL2SB_PrecacheOnce() entry.
**
** The one-shot cache is process-lifetime, but a precache can legitimately FAIL
** the first time it is asked: GMod scripts call Sound("HealthKit.Touch") at
** weapon-file load time, which on the server happens before the sound emitter
** system has necessarily read scripts/game_sounds_manifest.txt.  In that window
** CBaseEntity::PrecacheScriptSound() answers 0, the caller falls back to
** treating the sound-script NAME as a raw .wav path (which cannot work), and --
** before this function existed -- the name stayed in the cache forever, so the
** real wave was never registered for the rest of the session and every later
** EmitSound of it was refused by the engine with "SV_StartSound: <wave> not
** precached".  Forgetting the entry lets the next call retry.
*/
void HL2SB_PrecacheForget (const char *pszName) {
  if ( s_pHL2SBPrecached == NULL || pszName == NULL )
    return;

  const int i = s_pHL2SBPrecached->Find( pszName );

  if ( i != s_pHL2SBPrecached->InvalidIndex() )
    s_pHL2SBPrecached->RemoveAt( i );
}


static int luasrc_UTIL_VecToYaw (lua_State *L) {
  lua_pushnumber(L, UTIL_VecToYaw(luaL_checkvector(L, 1)));
  return 1;
}

static int luasrc_UTIL_VecToPitch (lua_State *L) {
  lua_pushnumber(L, UTIL_VecToPitch(luaL_checkvector(L, 1)));
  return 1;
}

static int luasrc_UTIL_YawToVector (lua_State *L) {
  Vector v = UTIL_YawToVector(luaL_checknumber(L, 1));
  lua_pushvector(L, v);
  return 1;
}

static int luasrc_SharedRandomFloat (lua_State *L) {
  lua_pushnumber(L, SharedRandomFloat(luaL_checkstring(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optint(L, 4, 0)));
  return 1;
}

static int luasrc_SharedRandomInt (lua_State *L) {
  lua_pushinteger(L, SharedRandomInt(luaL_checkstring(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_optint(L, 4, 0)));
  return 1;
}

static int luasrc_SharedRandomVector (lua_State *L) {
  lua_pushvector(L, SharedRandomVector(luaL_checkstring(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optint(L, 4, 0)));
  return 1;
}

static int luasrc_SharedRandomAngle (lua_State *L) {
  lua_pushangle(L, SharedRandomAngle(luaL_checkstring(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optint(L, 4, 0)));
  return 1;
}

//-----------------------------------------------------------------------------
// HL2SB: GMod's `filter` field, in all three of its forms.
//
//   filter = entity   -> that entity is skipped (what this fork always did)
//   filter = { ... }  -> the entities in the table are skipped
//   filter = function( ent ) -> called for every candidate entity; true means
//                               the trace HITS it, false means skip
//                               (GMod wiki, Structures/Trace: "Return true to hit
//                               the entity, false to skip it")
//
//   The function form is not optional decoration: SCP-096's SeeMe() decides "a
//   player is looking at me" by running two eye traces whose *filter* is where
//   the hit is recorded --
//
//       local tr = util.TraceLine( { start = self:EyePos(), endpos = ..., 
//           filter = function( ent )
//               if ( ent:IsPlayer() && ent:Alive() ) then ok1 = 1; return true end
//               return false
//           end } )
//
//   The old parser read `filter` with lua_toentity(), which answers NULL for a
//   function, so the filter was dropped, the callback never ran, ok1/ok2 stayed
//   0 and SeeMe() could never return true -- silently, because an ignored filter
//   is indistinguishable from a trace that simply missed.
//-----------------------------------------------------------------------------
class CLuaTraceFilter : public ITraceFilter
{
public:
	CLuaTraceFilter( void ) { m_L = NULL; m_nRef = LUA_NOREF; m_nType = LUA_TNIL; m_pPassEnt = NULL; m_nCollisionGroup = COLLISION_GROUP_NONE; m_bIgnoreWorld = false; }

	void Setup( lua_State *pL, int nIndex, IHandleEntity *pPassEnt, int nCollisionGroup )
	{
		Release();

		m_L = pL;
		m_pPassEnt = pPassEnt;
		m_nCollisionGroup = nCollisionGroup;
		m_nType = lua_type( pL, nIndex );

		if ( m_nType == LUA_TFUNCTION )
		{
			lua_pushvalue( pL, nIndex );
			m_nRef = luaL_ref( pL, LUA_REGISTRYINDEX );
		}
		else if ( m_nType == LUA_TTABLE )
		{
			// Resolved to handles once, so identity is a pointer compare (each push
			// of an entity makes a fresh userdata, so Lua-side equality would not
			// work here).  Both the keys and the values are collected: GMod scripts
			// write both `filter = { ent }` and `filter = { [ent] = true }`.
			lua_pushvalue( pL, nIndex );

			lua_pushnil( pL );
			while ( lua_next( pL, -2 ) != 0 )
			{
				CBaseEntity *pKey = lua_toentity( pL, -2 );
				if ( pKey != NULL )
					m_ignoreList.AddToTail( pKey );

				CBaseEntity *pValue = lua_toentity( pL, -1 );
				if ( pValue != NULL )
					m_ignoreList.AddToTail( pValue );

				lua_pop( pL, 1 );
			}

			lua_pop( pL, 1 );
		}
		else
		{
			// entity (or nil / the NULL sentinel): on the plain engine path an
			// entity filter went to CTraceFilterSimple; when the trace is routed
			// through this filter (because of `ignoreworld`) it still has to be
			// honoured, so keep it as the pass entity.
			CBaseEntity *pEnt = lua_toentity( pL, nIndex );
			if ( pEnt != NULL )
				m_pPassEnt = pEnt;
		}
	}

	// HL2SB GMod compat: GMod's util.TraceLine family takes `ignoreworld = true`
	// (the parser stores it next to the filter fields, and scp173's
	// util.TraceLineEx sets it after the first world hit so later passes reach
	// entities the world would shadow).
	void SetIgnoreWorld( bool bIgnoreWorld ) { m_bIgnoreWorld = bIgnoreWorld; }

	void Release( void )
	{
		if ( m_L != NULL && m_nRef != LUA_NOREF )
			luaL_unref( m_L, LUA_REGISTRYINDEX, m_nRef );

		m_L = NULL;
		m_nRef = LUA_NOREF;
		m_nType = LUA_TNIL;
		m_ignoreList.RemoveAll();
		m_bIgnoreWorld = false;
	}

	virtual TraceType_t GetTraceType( void ) const { return TRACE_EVERYTHING; }

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		if ( m_bIgnoreWorld )
		{
			CBaseEntity *pEnt = EntityFromEntityHandle( pHandleEntity );
			if ( pEnt != NULL && pEnt->IsWorld() )
				return false;
		}

		if ( !StandardFilterRules( pHandleEntity, contentsMask ) )
			return false;

		if ( !PassServerEntityFilter( pHandleEntity, m_pPassEnt ) )
			return false;

		if ( m_L == NULL )
			return true;

		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( pEntity == NULL )
			return false;

		if ( !pEntity->ShouldCollide( m_nCollisionGroup, contentsMask ) )
			return false;

		if ( m_ignoreList.Count() > 0 )
		{
			for ( int i = 0; i < m_ignoreList.Count(); ++i )
			{
				if ( m_ignoreList[ i ] == pEntity )
					return false;
			}
		}

		if ( m_nType != LUA_TFUNCTION )
			return true;

		const int nTop = lua_gettop( m_L );

		lua_getref( m_L, m_nRef );					// [function]
		lua_pushvalue( m_L, -1 );					// [function][function]
		CBaseEntity::PushLuaInstanceSafe( m_L, pEntity );

		bool bHit = false;

		if ( luasrc_pcall( m_L, 1, 1, 0 ) == 0 && lua_isboolean( m_L, -1 ) )
			bHit = lua_toboolean( m_L, -1 ) != 0;

		lua_settop( m_L, nTop );
		return bHit;
	}

private:
	lua_State				*m_L;
	int						 m_nRef;
	int						 m_nType;
	IHandleEntity			*m_pPassEnt;
	int						 m_nCollisionGroup;
	bool					 m_bIgnoreWorld;
	CUtlVector< CBaseEntity * > m_ignoreList;
};

// The parser hands the filter to the trace callers through these two; a trace is
// built and used inside a single binding call, so one instance is enough.
static CLuaTraceFilter s_LuaTraceFilter;
static bool s_bLuaTraceFilterActive = false;

//-----------------------------------------------------------------------------
// HL2SB GMod compat: GMod's util.TraceLine / util.TraceHull take a *table*
//
//     local tr = util.TraceLine( { start = a, endpos = b, filter = ply,
//                                  mask = MASK_SHOT } )
//
// and return the trace, while this fork's binding takes positional arguments with
// an out-parameter trace (#6 / #8).  Both spellings are accepted now: the table
// form builds the trace itself and returns it (GMod's TraceResult).
//
// The ported flechette gun does exactly the GMod spelling and threw
//     weapon_flechettegun/shared.lua:72: bad argument #6 to 'TraceLine'
//     (CGameTrace expected, got no value)
// so it never reached ents.Create() below it.
//
// `filter` accepts an entity, a table of entities, or a function (see
// CLuaTraceFilter above).
//-----------------------------------------------------------------------------
static bool luasrc_TraceArgsFromTable (lua_State *L, Vector *pStart, Vector *pEnd, Vector *pMins, Vector *pMaxs,
                                        int *pMask, CBaseEntity **ppFilter, int *pCollisionGroup)
{
  *pStart = vec3_origin;
  *pEnd = vec3_origin;
  *pMins = vec3_origin;
  *pMaxs = vec3_origin;
  // HL2SB GMod compat: GMod's util.TraceLine/TraceHull default mask when the
  // data table carries no "mask" key is MASK_SOLID (0x0200400B): the literal
  // loaded into r8 as the fallback of the "mask" key read in GMod x64
  // server.dll's shared trace argument parser.  MASK_SHOT additionally matches
  // CONTENTS_DEBRIS/CONTENTS_HITBOX, so the old default stopped eye traces on
  // gibs that GMod traces pass straight through.
  *pMask = MASK_SOLID;
  *ppFilter = NULL;
  *pCollisionGroup = COLLISION_GROUP_NONE;

  lua_getfield( L, 1, "start" );
  if ( !lua_isnoneornil( L, -1 ) ) *pStart = luaL_checkvector( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "endpos" );
  if ( !lua_isnoneornil( L, -1 ) ) *pEnd = luaL_checkvector( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "mins" );
  if ( !lua_isnoneornil( L, -1 ) ) *pMins = luaL_checkvector( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "maxs" );
  if ( !lua_isnoneornil( L, -1 ) ) *pMaxs = luaL_checkvector( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "mask" );
  if ( lua_isnumber( L, -1 ) ) *pMask = (int)lua_tointeger( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "collisiongroup" );
  if ( lua_isnumber( L, -1 ) ) *pCollisionGroup = (int)lua_tointeger( L, -1 );
  lua_pop( L, 1 );

  // HL2SB GMod compat: `ignoreworld = true` -- GMod's parser reads it right
  // after collisiongroup and before filter (x64 server.dll trace argument
  // parser), and keeps it beside the filter fields.  With it set the world
  // stops participating in the trace, which util.TraceLineEx relies on: after
  // the first world hit it turns this on so subsequent passes see past the
  // world instead of stopping on it again.
  bool bIgnoreWorld = false;
  lua_getfield( L, 1, "ignoreworld" );
  if ( !lua_isnil( L, -1 ) ) bIgnoreWorld = lua_toboolean( L, -1 ) != 0;
  lua_pop( L, 1 );

  // `filter` is read LAST: the Lua filter below wants the collision group.
  s_bLuaTraceFilterActive = false;
  s_LuaTraceFilter.Release();

  lua_getfield( L, 1, "filter" );

  if ( lua_isfunction( L, -1 ) || lua_istable( L, -1 ) || bIgnoreWorld )
  {
    // Routing through the Lua filter either because the filter IS a Lua value
    // or because ignoreworld needs a ShouldHitEntity hook.  Entity and nil
    // filters are captured inside Setup.
    s_LuaTraceFilter.Setup( L, -1, NULL, *pCollisionGroup );
    s_LuaTraceFilter.SetIgnoreWorld( bIgnoreWorld );
    s_bLuaTraceFilterActive = true;
  }
  else
  {
    *ppFilter = lua_toentity( L, -1 );      // NULL for nil, GMod's NULL sentinel or a table
  }

  lua_pop( L, 1 );

  return true;
}

static int luasrc_UTIL_TraceLine (lua_State *L) {
  HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_UTIL_TRACE );
  if ( lua_istable( L, 1 ) ) {
    Vector vecStart, vecEnd, vecMins, vecMaxs;
    CBaseEntity *pFilter = NULL;
    int nMask = MASK_SHOT, nCollisionGroup = COLLISION_GROUP_NONE;
    CGameTrace trace;

    luasrc_TraceArgsFromTable( L, &vecStart, &vecEnd, &vecMins, &vecMaxs, &nMask, &pFilter, &nCollisionGroup );

    if ( s_bLuaTraceFilterActive )
      UTIL_TraceLine( vecStart, vecEnd, nMask, &s_LuaTraceFilter, &trace );
    else
      UTIL_TraceLine( vecStart, vecEnd, nMask, pFilter, nCollisionGroup, &trace );

    s_LuaTraceFilter.Release();
    s_bLuaTraceFilterActive = false;

    lua_pushtrace( L, trace );
    return 1;
  }

  UTIL_TraceLine(luaL_checkvector(L, 1), luaL_checkvector(L, 2), luaL_checkint(L, 3), lua_toentity(L, 4), luaL_checkint(L, 5), &luaL_checktrace(L, 6));
  return 0;
}

static int luasrc_UTIL_TraceHull (lua_State *L) {
  if ( lua_istable( L, 1 ) ) {
    Vector vecStart, vecEnd, vecMins, vecMaxs;
    CBaseEntity *pFilter = NULL;
    int nMask = MASK_SHOT, nCollisionGroup = COLLISION_GROUP_NONE;
    CGameTrace trace;

    luasrc_TraceArgsFromTable( L, &vecStart, &vecEnd, &vecMins, &vecMaxs, &nMask, &pFilter, &nCollisionGroup );

    if ( s_bLuaTraceFilterActive )
      UTIL_TraceHull( vecStart, vecEnd, vecMins, vecMaxs, nMask, &s_LuaTraceFilter, &trace );
    else
      UTIL_TraceHull( vecStart, vecEnd, vecMins, vecMaxs, nMask, pFilter, nCollisionGroup, &trace );

    s_LuaTraceFilter.Release();
    s_bLuaTraceFilterActive = false;

    lua_pushtrace( L, trace );
    return 1;
  }

  UTIL_TraceHull(luaL_checkvector(L, 1), luaL_checkvector(L, 2), luaL_checkvector(L, 3), luaL_checkvector(L, 4), luaL_checkint(L, 5), luaL_checkentity(L, 6), luaL_checkint(L, 7), &luaL_checktrace(L, 8));
  return 0;
}

static int luasrc_UTIL_TraceEntity (lua_State *L) {
  // HL2SB GMod compat: util.TraceEntity( tracedata, ent ).
  //
  // Wiki: "Runs a trace using the entity's collisionmodel between two points.
  // This does not take the entity's angles into account and will trace its
  // unrotated collisionmodel." -- so the ENTITY supplies the hull, and the
  // standard Trace structure supplies start/endpos/filter/mask, and the result is
  // a TraceResult.
  //
  // The old positional Team Sandbox form (entity, start, end, mask, startEnt,
  // collisionGroup, trace) is kept below for in-tree callers.  windgrin_npc calls
  // the GMod form every frame (util.TraceEntity( data, self.target )); with only
  // the positional binding it raised
  //
  //     bad argument #5 to 'TraceEntity' (number expected, got no value)
  //
  // once per tick and its targeting never ran.
  if ( lua_istable( L, 1 ) )
  {
    CBaseEntity *pEntity = luaL_checkentity( L, 2 );
    Vector vecStart, vecEnd, vecMins, vecMaxs;
    CBaseEntity *pFilter = NULL;
    int nMask = MASK_SHOT, nCollisionGroup = COLLISION_GROUP_NONE;
    CGameTrace trace;

    luasrc_TraceArgsFromTable( L, &vecStart, &vecEnd, &vecMins, &vecMaxs, &nMask, &pFilter, &nCollisionGroup );

    Vector vecHullMin = pEntity->CollisionProp()->OBBMins();
    Vector vecHullMax = pEntity->CollisionProp()->OBBMaxs();

    if ( s_bLuaTraceFilterActive )
      UTIL_TraceHull( vecStart, vecEnd, vecHullMin, vecHullMax, nMask, &s_LuaTraceFilter, &trace );
    else
      UTIL_TraceHull( vecStart, vecEnd, vecHullMin, vecHullMax, nMask, pFilter, nCollisionGroup, &trace );

    s_LuaTraceFilter.Release();
    s_bLuaTraceFilterActive = false;

    lua_pushtrace( L, trace );
    return 1;
  }

  UTIL_TraceEntity(luaL_checkentity(L, 1), luaL_checkvector(L, 2), luaL_checkvector(L, 3), luaL_checkint(L, 4), luaL_checkentity(L, 5), luaL_checkint(L, 5), &luaL_checktrace(L, 6));
  return 0;
}

static int luasrc_UTIL_EntityHasMatchingRootParent (lua_State *L) {
  lua_pushboolean(L, UTIL_EntityHasMatchingRootParent(luaL_checkentity(L, 1), luaL_checkentity(L, 2)));
  return 1;
}

static int luasrc_UTIL_PointContents (lua_State *L) {
  lua_pushinteger(L, UTIL_PointContents(luaL_checkvector(L, 1)));
  return 1;
}

static int luasrc_UTIL_TraceModel (lua_State *L) {
  UTIL_TraceModel(luaL_checkvector(L, 1), luaL_checkvector(L, 2), luaL_checkvector(L, 3), luaL_checkvector(L, 4), luaL_checkentity(L, 5), luaL_checkint(L, 6), &luaL_checktrace(L, 7));
  return 0;
}

/*
** HL2SB GMod compat: util.ParticleTracer( name, startPos, endPos, doWhiz ).
** GMod's 4-arg signature -- entity/attachment routing lives in the Ex variant
** below.  (The first cut exposed the C++ UTIL_ParticleTracer's 6-arg order
** here, so a script's `doWhiz = true` landed in the entIndex slot.)
** The 4-arg form never uses an attachment: pass TRACER_DONT_USE_ATTACHMENT so
** the dispatch does not carry a bogus TRACER_FLAG_USEATTACHMENT.
*/
static int luasrc_UTIL_ParticleTracer (lua_State *L) {
  UTIL_ParticleTracer(luaL_checkstring(L, 1), luaL_checkvector(L, 2), luaL_checkvector(L, 3), 0, TRACER_DONT_USE_ATTACHMENT, luaL_optboolean(L, 4, 0));
  return 0;
}

/*
** HL2SB GMod compat: util.ParticleTracerEx( name, startPos, endPos, doWhiz,
** entityIndex, attachmentIndex ) -- GMod's expanded variant, the signature the
** old binding above used to answer under the plain ParticleTracer name.
*/
static int luasrc_UTIL_ParticleTracerEx (lua_State *L) {
  UTIL_ParticleTracer(luaL_checkstring(L, 1), luaL_checkvector(L, 2), luaL_checkvector(L, 3), luaL_optint(L, 5, 0), luaL_optint(L, 6, 0), luaL_optboolean(L, 4, 0));
  return 0;
}

static int luasrc_UTIL_Tracer (lua_State *L) {
  UTIL_Tracer(luaL_checkvector(L, 1), luaL_checkvector(L, 2), luaL_optint(L, 3, 0), luaL_optint(L, 4, -1), luaL_optnumber(L, 5, 0), luaL_optboolean(L, 6, 0), luaL_optstring(L, 7, 0), luaL_optint(L, 8, 0));
  return 0;
}

static int luasrc_UTIL_BloodDrips (lua_State *L) {
  UTIL_BloodDrips(luaL_checkvector(L, 1), luaL_checkvector(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int luasrc_UTIL_IsLowViolence (lua_State *L) {
  lua_pushboolean(L, UTIL_IsLowViolence());
  return 1;
}

static int luasrc_UTIL_ShouldShowBlood (lua_State *L) {
  lua_pushboolean(L, UTIL_ShouldShowBlood(luaL_checkint(L, 1)));
  return 1;
}

static int luasrc_UTIL_BloodImpact (lua_State *L) {
  UTIL_BloodImpact(luaL_checkvector(L, 1), luaL_checkvector(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int luasrc_UTIL_BloodDecalTrace (lua_State *L) {
  UTIL_BloodDecalTrace(&luaL_checktrace(L, 1), luaL_checkint(L, 2));
  return 0;
}

static int luasrc_UTIL_DecalTrace (lua_State *L) {
  UTIL_DecalTrace(&luaL_checktrace(L, 1), luaL_checkstring(L, 2));
  return 0;
}

// HL2SB GMod compat (2026-09-23): GMod's util.Decal( name, start, end,
// filter = NULL ) -- "Performs a trace and paints a decal to the surface hit"
// (wiki; built-in names include ManhackCut, Impact.Concrete, Scorch, ...).
// This fork only had util.DecalTrace( trace, name ), so the CF beast pack's
// melee (weapon_cf_base.lua:65, util.Decal("ManhackCut", sp, ran)) died on
// "attempt to call a nil value (field 'Decal')" on every wall hit.
static int luasrc_UTIL_Decal (lua_State *L) {
  const char *pszDecal = luaL_checkstring( L, 1 );
  Vector vecStart = luaL_checkvector( L, 2 );
  Vector vecEnd = luaL_checkvector( L, 3 );
  CBaseEntity *pFilter = ( lua_gettop( L ) >= 4 && !lua_isnoneornil( L, 4 ) ) ? lua_toentity( L, 4 ) : NULL;

  CGameTrace trace;
  UTIL_TraceLine( vecStart, vecEnd, MASK_SHOT, pFilter, COLLISION_GROUP_NONE, &trace );

  // Same guard as the engine's own UTIL_DecalTrace callers: nothing hit ->
  // nothing to paint (UTIL_DecalTrace early-outs on fraction == 1.0 anyway,
  // but it also dereferences m_pEnt, so don't even call it into the void).
  if ( trace.fraction < 1.0f && trace.m_pEnt != NULL )
    UTIL_DecalTrace( &trace, pszDecal );

  return 0;
}

static int luasrc_UTIL_IsSpaceEmpty (lua_State *L) {
  lua_pushboolean(L, UTIL_IsSpaceEmpty(luaL_checkentity(L, 1), luaL_checkvector(L, 2), luaL_checkvector(L, 3)));
  return 1;
}

static int luasrc_UTIL_PlayerByIndex (lua_State *L) {
  lua_pushplayer(L, UTIL_PlayerByIndex(luaL_checkint(L, 1)));
  return 1;
}


// HL2SB GMod SWEP compat: stock SWEP Initialize calls util.PrecacheSound.
//
// HL2SB: script-sound names must go through PrecacheScriptSound(), because that
// is the table the server's SV_StartSound validation checks, and the raw-wave
// path has to be idempotent: CBaseEntity::PrecacheSound() warns
// ("Direct precache of %s", SoundEmitterSystem.cpp:1494) every time it is called
// outside the precache phase, and a weapon that is re-created (every pickup,
// every map reload) ran this on each spawn.
static int luasrc_util_PrecacheSound (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);

  // HL2SB: ask once per name per session (see HL2SB_PrecacheOnce).
  if ( !HL2SB_PrecacheOnce( pszName ) )
    return 0;

  if ( CBaseEntity::PrecacheScriptSound( pszName ) <= 0
       && !enginesound->IsSoundPrecached( pszName ) ) {
    // Neither the sound script nor the raw wave registered.  Most often the
    // script tables are not loaded yet (a weapon script runs this at load time);
    // forget the entry so a later map load can retry instead of leaving the
    // sound permanently unregistered for the whole process.
    HL2SB_PrecacheForget( pszName );
    CBaseEntity::PrecacheSound( pszName );
  }
  return 0;
}

// HL2SB GMod compat: util.PrecacheModel(mdl).  GMod scripts (gmod_camera, many
// workshop SWEPs and addons) wrap model paths in Model(...) or call
// util.PrecacheModel directly.  Precaching is a server-side operation; on the
// client the argument is validated and dropped, matching GMod's behaviour.
static int luasrc_util_PrecacheModel (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);

  // HL2SB: an SWEP is recreated on every pickup and every map reload, and each
  // recreation used to re-run the whole precache of its model/sound list.
  if ( !HL2SB_PrecacheOnce( pszName ) )
    return 0;

#ifndef CLIENT_DLL
  CBaseEntity::PrecacheModel( pszName );
#endif
  return 0;
}

/*
** HL2SB GMod compat: util.GetModelInfo( modelname ) -> table | nil.
**
** GMod answers a table (at least { SkinCount = n }) for any loadable model and
** the minecraft SWEP's block menu builds its skin variants from it
** (cl_init.lua:322, `test["SkinCount"] > 1`).  With no binding at all the whole
** createBlockMenu() aborted there, leaving mc_blockCounter and every later
** control nil -- the "menu opens basically empty" symptom.
**
** Client realm: the block models are never precached at map load, so force the
** same on-demand load ClientsideModel uses (engine->LoadModel +
** RegisterDynamicModel, see lbaseflex_shared.cpp).  Server realm: only models
** already in the precache table are answerable.
*/
static int luasrc_util_GetModelInfo (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);

  // wiki: "This function will silently fail if used on models with following
  // strings in them" -- animation/library models carry no useful info.
  static const char *const s_pGetModelInfoBlock[] = {
    "_shared", "_anims", "_gestures", "_anim", "_postures", "_gst",
    "_pst", "_shd", "_ss", "_anm", "_include"
  };
  for ( int i = 0; i < ARRAYSIZE( s_pGetModelInfoBlock ); i++ ) {
    if ( strstr( pszName, s_pGetModelInfoBlock[ i ] ) != NULL ) {
      lua_pushnil( L );
      return 1;
    }
  }

  model_t *pModel = NULL;

#ifdef CLIENT_DLL
  int nModelIndex = modelinfo->GetModelIndex( pszName );
  if ( nModelIndex != -1 )
  {
    pModel = ( model_t * )modelinfo->GetModel( nModelIndex );
    if ( modelinfo->GetStudiomodel( pModel ) == NULL )
      pModel = NULL;    // stale pointer after a map change
  }
  if ( pModel == NULL )
  {
    pModel = ( model_t * )engine->LoadModel( pszName, true );
    if ( pModel != NULL )
      modelinfo->RegisterDynamicModel( pszName, true );
  }
#else
  int nModelIndex = modelinfo->GetModelIndex( pszName );
  if ( nModelIndex != -1 )
    pModel = ( model_t * )modelinfo->GetModel( nModelIndex );
#endif

  if ( pModel == NULL )
  {
    lua_pushnil( L );
    return 1;
  }

  const studiohdr_t *pHdr = modelinfo->GetStudiomodel( pModel );
  int nSkins = ( pHdr != NULL ) ? pHdr->numskinfamilies : 1;
  if ( nSkins < 1 )
    nSkins = 1;

  lua_newtable( L );
  lua_pushstring( L, "SkinCount" );
  lua_pushinteger( L, nSkins );
  lua_rawset( L, -3 );
  lua_pushstring( L, "ModelName" );
  // wiki: "as embedded in the model file itself"
  lua_pushstring( L, ( pHdr != NULL && pHdr->name[ 0 ] ) ? pHdr->name : pszName );
  lua_rawset( L, -3 );

  // vcollide: the .phy keyvalues text (ModelInfo.KeyValues).
  vcollide_t *pCollide = modelinfo->GetVCollide( modelinfo->GetModelIndex( pszName ) );
  lua_pushstring( L, "KeyValues" );
  lua_pushstring( L, ( pCollide != NULL && pCollide->pKeyValues != NULL ) ? pCollide->pKeyValues : "" );
  lua_rawset( L, -3 );
  lua_pushstring( L, "ModelKeyValues" );
  lua_pushstring( L, ( pHdr != NULL && pHdr->KeyValueText() != NULL ) ? pHdr->KeyValueText() : "" );
  lua_rawset( L, -3 );

  if ( pHdr != NULL ) {
    lua_pushstring( L, "MeshCount" );         lua_pushinteger( L, pHdr->numbodyparts );   lua_rawset( L, -3 );
    lua_pushstring( L, "BoneCount" );         lua_pushinteger( L, pHdr->numbones );       lua_rawset( L, -3 );
    lua_pushstring( L, "MaterialCount" );     lua_pushinteger( L, pHdr->numtextures );    lua_rawset( L, -3 );
    lua_pushstring( L, "SequenceCount" );     lua_pushinteger( L, pHdr->numlocalseq );         lua_rawset( L, -3 );
    lua_pushstring( L, "AttachmentCount" );   lua_pushinteger( L, pHdr->numlocalattachments ); lua_rawset( L, -3 );
    lua_pushstring( L, "Flags" );             lua_pushinteger( L, pHdr->flags );          lua_rawset( L, -3 );
    lua_pushstring( L, "StaticProp" );        lua_pushboolean( L, ( pHdr->flags & STUDIOHDR_FLAGS_STATIC_PROP ) != 0 ); lua_rawset( L, -3 );
    lua_pushstring( L, "Version" );           lua_pushinteger( L, pHdr->version );        lua_rawset( L, -3 );
    lua_pushstring( L, "Checksum" );          lua_pushinteger( L, pHdr->checksum );       lua_rawset( L, -3 );
    lua_pushstring( L, "SurfacePropName" );   lua_pushstring( L, pHdr->pszSurfaceProp() ); lua_rawset( L, -3 );
    lua_pushstring( L, "IncludeModelCount" ); lua_pushinteger( L, pHdr->numincludemodels ); lua_rawset( L, -3 );

    lua_pushstring( L, "EyePosition" );    lua_pushvector( L, pHdr->eyeposition );    lua_rawset( L, -3 );
    lua_pushstring( L, "IllumPosition" );  lua_pushvector( L, pHdr->illumposition );  lua_rawset( L, -3 );
    lua_pushstring( L, "HullMin" );        lua_pushvector( L, pHdr->hull_min );       lua_rawset( L, -3 );
    lua_pushstring( L, "HullMax" );        lua_pushvector( L, pHdr->hull_max );       lua_rawset( L, -3 );

    // attachments: Name / Bone / Offset
    lua_pushstring( L, "Attachments" );
    lua_newtable( L );
    for ( int i = 0; i < pHdr->numlocalattachments; i++ ) {
      const mstudioattachment_t *pAtt = pHdr->pLocalAttachment( i );
      lua_newtable( L );
      lua_pushstring( L, "Name" );   lua_pushstring( L, pAtt->pszName() ); lua_rawset( L, -3 );
      lua_pushstring( L, "Bone" );   lua_pushinteger( L, pAtt->localbone ); lua_rawset( L, -3 );
      Vector vecAttPos( pAtt->local.m_flMatVal[0][3], pAtt->local.m_flMatVal[1][3], pAtt->local.m_flMatVal[2][3] );
      lua_pushstring( L, "Offset" ); lua_pushvector( L, vecAttPos );        lua_rawset( L, -3 );
      lua_rawseti( L, -2, i + 1 );
    }
    lua_rawset( L, -3 );

    // bones: Name / Parent / Flags / Position / SurfacePropName
    lua_pushstring( L, "Bones" );
    lua_newtable( L );
    for ( int i = 0; i < pHdr->numbones; i++ ) {
      const mstudiobone_t *pBone = pHdr->pBone( i );
      lua_newtable( L );
      lua_pushstring( L, "Name" );            lua_pushstring( L, pBone->pszName() );          lua_rawset( L, -3 );
      lua_pushstring( L, "Parent" );          lua_pushinteger( L, pBone->parent );            lua_rawset( L, -3 );
      lua_pushstring( L, "Flags" );           lua_pushinteger( L, pBone->flags );             lua_rawset( L, -3 );
      lua_pushstring( L, "Position" );        lua_pushvector( L, pBone->pos );                lua_rawset( L, -3 );
      lua_pushstring( L, "SurfacePropName" ); lua_pushstring( L, pBone->pszSurfaceProp() );   lua_rawset( L, -3 );
      lua_rawseti( L, -2, i + 1 );
    }
    lua_rawset( L, -3 );

    // sequences: Name / Activity / ActivityID
    lua_pushstring( L, "Sequences" );
    lua_newtable( L );
    for ( int i = 0; i < pHdr->numlocalseq; i++ ) {
      const mstudioseqdesc_t *pSeq = pHdr->pLocalSeqdesc( i );
      lua_newtable( L );
      lua_pushstring( L, "Name" );       lua_pushstring( L, pSeq->pszLabel() ); lua_rawset( L, -3 );
      lua_pushstring( L, "ActivityID" ); lua_pushinteger( L, pSeq->activity ); lua_rawset( L, -3 );
      const char *pActivity = ActivityList_NameForIndex( pSeq->activity );
      lua_pushstring( L, "Activity" );   lua_pushstring( L, pActivity ? pActivity : "" ); lua_rawset( L, -3 );
      lua_rawseti( L, -2, i + 1 );
    }
    lua_rawset( L, -3 );

    // materials across the model's textures
    lua_pushstring( L, "Materials" );
    lua_newtable( L );
    for ( int i = 0; i < pHdr->numtextures; i++ ) {
      lua_pushstring( L, pHdr->pTexture( i )->pszName() );
      lua_rawseti( L, -2, i + 1 );
    }
    lua_rawset( L, -3 );
  }
  return 1;
}

/*
** HL2SB GMod compat: util.Effect( name, effectData [, allowOverride] ).
**
** GMod's util.Effect is the script-facing way to run an effect by name:
**
**   * on the CLIENT it runs a scripted effect (lua/effects/<name>.lua) right
**     there.  DispatchEffect() is NOT the way to get there from a predicted
**     weapon: CTempEnts::SuppressTE() removes the predicting player from the
**     filter and drops the whole temp entity, so nothing would render (see
**     AGENTS.md 5.3.1 -- the same trap the env_explosion fireball hit).
**   * on the SERVER it dispatches the effect as a temp entity, which is how a
**     non-predicted shooter (an NPC, a scripted entity) gets the effect onto
**     every client's screen.
**
** allowOverride (wiki: default TRUE) -- when false, a Lua effect registered
** under the same name does NOT shadow the engine callback for this call.
** The 4th GMod argument (ignorePrediction / CRecipientFilter) is accepted and
** ignored: this fork dispatches with the default filter either way.
*/
static int luasrc_UTIL_Effect (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);
  CEffectData data = luaL_checkeffect(L, 2);
  int bAllowOverride = luaL_optboolean(L, 3, 1);

#ifdef CLIENT_DLL
  if ( bAllowOverride && HL2SB_CreateLuaEffect( pszName, data ) ) {
    return 0;
  }
#endif

  DispatchEffect( pszName, data );
  return 0;
}

/*
** HL2SB GMod compat: util.SpriteTrail( entity, attachment, color, additive,
**                                     startWidth, endWidth, lifetime,
**                                     textureResolution, texture ).
**
** GMod returns the env_spritetrail entity it attaches to `entity`.  The engine
** side of that entity (CSpriteTrail, game/shared/SpriteTrail.cpp) is a real
** networked entity on both realms, but only the server can create one, so this
** is the server implementation; the client returns nil exactly like it would
** for an entity the server owns (the client's copy arrives over the network).
**
** Anchoring: the first cut used CBaseEntity::SetParent() alone.  That only sets
** the *move parent*; CSpriteTrail's client-side GetRenderOrigin() -- which is
** what actually samples every trail point -- reads CSprite::m_hAttachedToEntity
** / m_nAttachment, which only CSprite::SetAttachment() sets.  Every trail the
** engine itself creates is anchored with SetAttachment (env_effectsscript.cpp
** :261, prop_combine_ball.cpp:402, grenade_frag.cpp:178-179,
** env_headcrabcanister.cpp:494), so do the same: the ribbon then follows the
** model's attachment point instead of the entity's bare origin, and it is
** sampled from the same place on both realms.
*/
static int luasrc_UTIL_SpriteTrail (lua_State *L) {
#ifndef CLIENT_DLL
  CBaseEntity *pEntity = lua_toentity( L, 1 );
  int iAttachment = luaL_checkint( L, 2 );
  lua_Color clr = luaL_checkcolor( L, 3 );
  bool bAdditive = luaL_checkboolean( L, 4 );
  float flStartWidth = (float)luaL_checknumber( L, 5 );
  float flEndWidth = (float)luaL_checknumber( L, 6 );
  float flLifetime = (float)luaL_optnumber( L, 7, 1.0f );
  float flTextureResolution = (float)luaL_optnumber( L, 8, 0.0f );
  const char *pszTexture = luaL_checkstring( L, 9 );

  // Scripts hand over 0 / -1 / NaN freely (GMod's own effects do); a zero or
  // negative lifetime would make every point expire on the frame it is recorded
  // (nothing renders, and UpdateTrail() then re-adds a point every frame), and a
  // zero texture resolution would stretch the texture over the whole ribbon.
  if ( !IsFinite( flStartWidth ) || flStartWidth < 0.0f )
    flStartWidth = 0.0f;
  if ( !IsFinite( flEndWidth ) )
    flEndWidth = 0.0f;
  if ( !IsFinite( flLifetime ) || flLifetime <= 0.0f )
    flLifetime = 1.0f;
  if ( !IsFinite( flTextureResolution ) || flTextureResolution <= 0.0f )
    flTextureResolution = ( flStartWidth + flEndWidth ) > 0.0f
                          ? ( 1.0f / ( flStartWidth + flEndWidth ) * 0.5f )
                          : 0.03125f;
  if ( iAttachment < 0 )
    iAttachment = 0;

  Vector vecOrigin = vec3_origin;
  if ( pEntity != NULL ) {
    vecOrigin = pEntity->GetAbsOrigin();
  }

  // HL2SB: precache the trail texture once per session, before the entity is
  // created, so a weapon that is re-created (every pickup / every map reload)
  // does not re-run CSpriteTrail::Precache()'s "Late precache of <vmt>" and its
  // synchronous material + texture load on a live throw.
  if ( HL2SB_PrecacheOnce( pszTexture ) ) {
    CBaseEntity::PrecacheModel( pszTexture );
  }

  CSpriteTrail *pTrail = CSpriteTrail::SpriteTrailCreate( pszTexture, vecOrigin, true );
  if ( pTrail == NULL ) {
    lua_pushnil( L );
    return 1;
  }

  pTrail->SetStartWidth( flStartWidth );
  pTrail->SetEndWidth( flEndWidth );
  pTrail->SetLifeTime( flLifetime );
  pTrail->SetTextureResolution( flTextureResolution );
  pTrail->SetRenderColor( clr.r(), clr.g(), clr.b() );
  pTrail->SetBrightness( clr.a() );
  pTrail->SetRenderMode( bAdditive ? kRenderGlow : kRenderTransAlpha );

  if ( pEntity != NULL ) {
    pTrail->SetParent( pEntity, iAttachment );
    pTrail->SetLocalOrigin( vec3_origin );
    // ... and the attachment, the way the engine's own trails do it.  If the
    // model has no such attachment, CSpriteTrail::GetRenderOrigin() falls back
    // to the entity's origin on its own (SpriteTrail.cpp:537-553), so a bad
    // index degrades to the old behaviour instead of moving the ribbon.
    pTrail->SetAttachment( pEntity, iAttachment );
  }

  lua_pushentity( L, pTrail );
  return 1;
#else
  lua_pushnil( L );
  return 1;
#endif
}

/*
** HL2SB GMod compat: util.BlastDamage( inflictor, attacker, origin, radius, damage ).
** Server-side in GMod too (the client has no authoritative damage model).
**
** The Nyan bomb calls this from ENT:PhysicsCollide with self:GetOwner() as the
** attacker, and the owner is already gone whenever the bomb outlives its owner
** (dropped, disconnected, or the weapon was removed) -- so a NULL attacker and a
** NULL inflictor are both reachable here, as are zero/negative/absurd radii from
** a script.  g_pGameRules itself is NULL outside a live server (level shutdown,
** a menu-state Lua call), and dereferencing it is an outright null-pointer
** crash, so guard before dispatching.
*/
static int luasrc_UTIL_BlastDamage (lua_State *L) {
#ifndef CLIENT_DLL
  CBaseEntity *pInflictor = lua_toentity( L, 1 );
  CBaseEntity *pAttacker = lua_toentity( L, 2 );
  Vector vecOrigin = luaL_checkvector( L, 3 );
  float flRadius = (float)luaL_checknumber( L, 4 );
  float flDamage = (float)luaL_checknumber( L, 5 );

  if ( g_pGameRules == NULL ) {
    HL2SB_WarnOnce( "blastdamage-norules",
      "util.BlastDamage called with g_pGameRules == NULL (level shutting down?); ignored" );
    return 0;
  }

  if ( !IsFinite( flRadius ) || flRadius <= 0.0f ) {
    HL2SB_WarnOnce( "blastdamage-radius",
      "util.BlastDamage got a non-positive radius (%.3f); ignored", flRadius );
    return 0;
  }
  if ( !IsFinite( flDamage ) )
    flDamage = 0.0f;

  if ( pInflictor == NULL ) {
    pInflictor = pAttacker;
  }

  // A blast with no inflictor AND no attacker is legitimate (a map object
  // exploding), but it is also what an addon ends up with when its owner entity
  // died first -- which is worth one line in the log, because RadiusDamage()
  // attributes the damage to the world and nothing else records it.
  if ( pAttacker == NULL ) {
    HL2SB_WarnOnce( "blastdamage-noattacker",
      "util.BlastDamage with a NULL attacker (owner entity already gone); damage is attributed to the world" );
  }

  CTakeDamageInfo info( pInflictor, pAttacker, flDamage, DMG_BLAST );
  g_pGameRules->RadiusDamage( info, vecOrigin, flRadius, CLASS_NONE, NULL );
#else
  luaL_checkvector( L, 3 );
  luaL_checknumber( L, 4 );
  luaL_checknumber( L, 5 );
#endif
  return 0;
}

// HL2SB GMod compat (2026-09-24): util.ScreenShake( pos, amplitude, frequency,
// duration, radius ) -- wiki util.ScreenShake, server realm (client gets the
// shake via the usermessage).  combustible_lemon's Detonate calls this right
// after BlastDamage; with the name missing the call raised and every error
// after it in the detonation path died silently.
static int luasrc_UTIL_ScreenShake (lua_State *L) {
  Vector vecCenter = luaL_checkvector( L, 1 );
  float flAmplitude = (float)luaL_checknumber( L, 2 );
  float flFrequency = (float)luaL_checknumber( L, 3 );
  float flDuration  = (float)luaL_checknumber( L, 4 );
  float flRadius    = (float)luaL_checknumber( L, 5 );

#ifndef CLIENT_DLL
  // GMod's default command is SHAKE_START; the 6th argument is the wiki's
  // airshake boolean (Source's bAirshake), not a ShakeCommand_t.
  bool bAirshake = ( lua_isnone( L, 6 ) ) ? false : ( lua_toboolean( L, 6 ) != 0 );
  UTIL_ScreenShake( vecCenter, flAmplitude, flFrequency, flDuration, flRadius, SHAKE_START, bAirshake );
#else
  (void)flAmplitude; (void)flFrequency; (void)flDuration; (void)flRadius;
#endif
  return 0;
}

// HL2SB GMod compat: util.IsInWorld( position ).
//
// Wiki: "Returns whether the given position is in the world."  The fork only had
// Entity:IsInWorld, and the windgrin_npc nextbot calls the util form to validate a
// trace hit position every time it recomputes its path:
//
//     if d.Hit && util.IsInWorld( d.HitPos ) then ...      -- npc_windgrinbot.lua
//
// util.IsInWorld answered nil, so its RecomputeTargetPath raised on every tick and
// no path was ever built.  The test is the engine's own coordinate bounds check
// (CBaseEntity::IsInWorld), applied to the point.
static int luasrc_UTIL_IsInWorld (lua_State *L) {
  Vector vecPos = luaL_checkvector( L, 1 );

  bool bInside = vecPos.x > MIN_COORD_FLOAT && vecPos.x < MAX_COORD_FLOAT &&
                 vecPos.y > MIN_COORD_FLOAT && vecPos.y < MAX_COORD_FLOAT &&
                 vecPos.z > MIN_COORD_FLOAT && vecPos.z < MAX_COORD_FLOAT;

  lua_pushboolean( L, bInside );
  return 1;
}

// -----------------------------------------------------------------------------
// HL2SB GMod compat (2026-09-25): util.TableToJSON / util.JSONToTable.
//
// GMod implements both engine-side (RapidJSON).  Here decode goes through
// Valve's KeyValuesJSONParser (tier1/keyvaluesjson.cpp - battle-tested, and
// already in every link that pulls tier1), encode is a direct Lua-table
// emitter.
//
// Wiki semantics reproduced:
// - TableToJSON( table, prettyPrint=false ) -> string.  Non-serializable
//   values (functions, userdata, nil) are dropped "as if it wasn't in the
//   table"; all keys become strings.
// - JSONToTable( json ) -> table|nil.  nil on invalid input; JSON null -> nil;
//   numeric-looking keys convert back to numbers "wherever possible";
//   sequential numeric keys (JSON arrays) come back as 1-based Lua arrays.
//
// Known deviations (documented, none hit stock addons): GMod's parser also
// tolerates comments/trailing commas and tracks array-vs-object identity for
// numeric keys; Valve's parser is strict JSON and both shapes collapse to the
// same KeyValues tree.
// -----------------------------------------------------------------------------

static void JSON_EmitEscaped( CUtlString &out, const char *psz )
{
	for ( const char *p = psz; *p != '\0'; ++p )
	{
		unsigned char c = (unsigned char)*p;
		switch ( c )
		{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if ( c < 0x20 )
				{
					char szTmp[ 8 ];
					V_sprintf_safe( szTmp, "\\u%04x", c );
					out += szTmp;
				}
				else
				{
					char szCh[ 2 ] = { (char)c, '\0' };
					out += szCh;
				}
		}
	}
}

static void JSON_EmitNumber( CUtlString &out, lua_Number num )
{
	char szNum[ 40 ];
	if ( num == (double)(long long)num && num > -9.0e15 && num < 9.0e15 )
		V_sprintf_safe( szNum, "%lld", (long long)num );
	else
		V_sprintf_safe( szNum, "%.14g", num );
	out += szNum;
}

static void JSON_EmitValue( lua_State *L, int idx, CUtlString &out, bool pretty, int depth, int indent );

static bool JSON_IsArray( lua_State *L, int idx, lua_Integer *pnOut )
{
	lua_Integer n = luaL_len( L, idx );
	if ( n <= 0 )
		return false;

	// keys 1..n must all be present...
	for ( lua_Integer k = 1; k <= n; k++ )
	{
		lua_rawgeti( L, idx, k );
		bool bNil = lua_isnil( L, -1 );
		lua_pop( L, 1 );
		if ( bNil )
			return false;
	}

	// ...and there must be no other keys at all
	lua_Integer count = 0;
	lua_pushnil( L );
	while ( lua_next( L, idx ) != 0 )
	{
		count++;
		lua_pop( L, 1 );
	}
	if ( count != n )
		return false;

	*pnOut = n;
	return true;
}

static void JSON_EmitTable( lua_State *L, int idx, CUtlString &out, bool pretty, int depth, int indent )
{
	if ( depth > 128 )
	{
		// cycle / pathological nesting - drop the value rather than overflowing
		out += "null";
		return;
	}

	// work on an absolute stack index: everything below pushes/pops around it
	lua_pushvalue( L, idx );
	int i = lua_gettop( L );

	lua_Integer n = 0;
	bool isArray = JSON_IsArray( L, i, &n );

	if ( isArray )
	{
		out += "[";
		for ( lua_Integer k = 1; k <= n; k++ )
		{
			if ( pretty ) { out += "\n"; for ( int s = 0; s <= indent; s++ ) out += "    "; }
			lua_rawgeti( L, i, k );
			JSON_EmitValue( L, -1, out, pretty, depth + 1, indent + 1 );
			lua_pop( L, 1 );
			if ( k < n ) out += ",";
		}
		if ( pretty ) { out += "\n"; for ( int s = 0; s < indent; s++ ) out += "    "; }
		out += "]";
		lua_pop( L, 1 );
		return;
	}

	out += "{";
	bool bFirst = true;
	lua_pushnil( L );
	while ( lua_next( L, i ) != 0 )
	{
		// value at -1, key at -2.  GMod: non-serializable values are dropped
		// "as if it wasn't in the table"; all keys become strings.
		int keyType = lua_type( L, -2 );
		int valueType = lua_type( L, -1 );

		bool bSkip = ( valueType == LUA_TNIL || valueType == LUA_TFUNCTION ||
					   valueType == LUA_TUSERDATA ||
					   ( keyType != LUA_TSTRING && keyType != LUA_TNUMBER ) );
		if ( bSkip )
		{
			lua_pop( L, 1 );
			continue;
		}

		if ( !bFirst ) out += ",";
		bFirst = false;
		if ( pretty ) { out += "\n"; for ( int s = 0; s <= indent; s++ ) out += "    "; }

		out += "\"";
		if ( keyType == LUA_TNUMBER )
			JSON_EmitNumber( out, lua_tonumber( L, -2 ) );	// number keys stringify
		else
			JSON_EmitEscaped( out, lua_tostring( L, -2 ) );
		out += "\": ";

		JSON_EmitValue( L, -1, out, pretty, depth + 1, indent + 1 );
		lua_pop( L, 1 );
	}
	if ( pretty && !bFirst ) { out += "\n"; for ( int s = 0; s < indent; s++ ) out += "    "; }
	out += "}";
	lua_pop( L, 1 );
}

static void JSON_EmitValue( lua_State *L, int idx, CUtlString &out, bool pretty, int depth, int indent )
{
	// copy to an absolute index - idx may be relative (lua_rawgeti results etc.)
	lua_pushvalue( L, idx );
	int i = lua_gettop( L );

	switch ( lua_type( L, i ) )
	{
		case LUA_TNIL:		out += "null"; break;
		case LUA_TBOOLEAN:	out += lua_toboolean( L, i ) ? "true" : "false"; break;
		case LUA_TNUMBER:	JSON_EmitNumber( out, lua_tonumber( L, i ) ); break;
		case LUA_TSTRING:	JSON_EmitEscaped( out, lua_tostring( L, i ) ); break;
		case LUA_TTABLE:	JSON_EmitTable( L, i, out, pretty, depth, indent ); break;
		case LUA_TUSERDATA:
		{
			// HL2SB (2026-10-05): GMod's encoder serializes Vector / Angle as
			// plain x/y/z (p/y/r) objects instead of dropping them - without
			// this a duplicator save loses every position and angle (the first
			// gm_save produced {"Player":{"MoveType":2}} with Origin silently
			// gone).  Metatable identity, not luaL_checkudata: the checker
			// raises on mismatch and the encoder must skip non-serializable
			// values, not abort.
			luaL_getmetatable( L, "Vector" );
			const bool bIsVector = lua_rawequal( L, -1, i ) != 0;
			lua_pop( L, 1 );
			luaL_getmetatable( L, "QAngle" );
			const bool bIsAngle = lua_rawequal( L, -1, i ) != 0;
			lua_pop( L, 1 );

			if ( bIsVector )
			{
				const Vector &v = luaL_checkvector( L, i );
				out += "{\"x\":";
				JSON_EmitNumber( out, v.x );
				out += ",\"y\":";
				JSON_EmitNumber( out, v.y );
				out += ",\"z\":";
				JSON_EmitNumber( out, v.z );
				out += "}";
			}
			else if ( bIsAngle )
			{
				const QAngle &a = luaL_checkangle( L, i );
				out += "{\"p\":";
				JSON_EmitNumber( out, a.x );
				out += ",\"y\":";
				JSON_EmitNumber( out, a.y );
				out += ",\"r\":";
				JSON_EmitNumber( out, a.z );
				out += "}";
			}
			else
			{
				out += "null";
			}
			break;
		}
		default:			out += "null"; break;
	}

	lua_pop( L, 1 );
}

// util.TableToJSON( table, prettyPrint=false )
// (non-static: net_WriteTable reuses the emitter, lnet.cpp)
LUA_API int luasrc_UTIL_TableToJSON( lua_State *L )
{
	if ( lua_type( L, 1 ) != LUA_TTABLE )
	{
		lua_pushnil( L );
		return 1;
	}

	bool pretty = ( lua_toboolean( L, 2 ) != 0 );

	CUtlString out;
	JSON_EmitTable( L, 1, out, pretty, 0, 0 );

	lua_pushstring( L, out.Get() );
	return 1;
}

// walk a KeyValues tree (from the JSON parser) back into a Lua value/table
static void JSON_KVToLua( lua_State *L, KeyValues *pNode )
{
	KeyValues *pChild = pNode->GetFirstSubKey();

	if ( pChild == NULL )
	{
		// leaf.  NOTE: "{}" parses to a childless TYPE_NONE node - that is an
		// empty table, not null (null comes through as TYPE_PTR).
		if ( pNode->GetDataType() == KeyValues::TYPE_NONE )
		{
			lua_newtable( L );
		}
		else if ( pNode->GetDataType() == KeyValues::TYPE_PTR )
		{
			lua_pushnil( L );	// JSON null
		}
		else
		{
			switch ( pNode->GetDataType() )
			{
				case KeyValues::TYPE_INT:     lua_pushinteger( L, pNode->GetInt() ); break;
				case KeyValues::TYPE_UINT64:  lua_pushnumber( L, (lua_Number)pNode->GetUint64() ); break;
				case KeyValues::TYPE_FLOAT:   lua_pushnumber( L, pNode->GetFloat() ); break;
				default:                      lua_pushstring( L, pNode->GetString() ); break;
			}
		}
		return;
	}

	// container.  ParseArray names children "0","1",... - sequential numeric
	// names mean this was a JSON array and comes back as a 1-based Lua array;
	// otherwise it is a map (GMod converts numeric-looking keys to numbers
	// "wherever possible").
	bool isArray = true;
	int expected = 0;
	for ( KeyValues *p = pChild; p != NULL; p = p->GetNextKey() )
	{
		if ( V_atoi( p->GetName() ) != expected ) { isArray = false; break; }
		++expected;
	}

	lua_newtable( L );

	int arrayIdx = 0;
	for ( KeyValues *p = pChild; p != NULL; p = p->GetNextKey() )
	{
		if ( isArray )
		{
			JSON_KVToLua( L, p );
			lua_rawseti( L, -2, ++arrayIdx );
		}
		else
		{
			const char *pszKey = p->GetName();
			char *pszEnd = NULL;
			long lKey = strtol( pszKey, &pszEnd, 10 );
			if ( pszEnd && *pszEnd == '\0' && pszEnd != pszKey )
				lua_pushinteger( L, (lua_Integer)lKey );
			else
				lua_pushstring( L, pszKey );
			JSON_KVToLua( L, p );
			lua_settable( L, -3 );
		}
	}

	// HL2SB (2026-10-05): GMod's decoder hands {"x":..,"y":..,"z":..} objects
	// back as real Vector userdata (the duplicator round-trip feeds them
	// straight into SetPos / MakeProp - plain tables would error there).  The
	// p/y/r shape comes back as a QAngle.  A table that merely happens to
	// carry those three numeric keys is rare enough that GMod accepts the same
	// ambiguity.
	if ( !isArray )
	{
		bool bX = false, bY = false, bZ = false, bP = false, bR = false;
		lua_Integer nY = 0;

		lua_pushnil( L );
		while ( lua_next( L, -2 ) != 0 )
		{
			if ( lua_isnumber( L, -1 ) && lua_type( L, -2 ) == LUA_TSTRING )
			{
				const char *pszKey = lua_tostring( L, -2 );
				if ( !V_stricmp( pszKey, "x" ) ) { bX = true; }
				else if ( !V_stricmp( pszKey, "y" ) ) { bY = true; nY = lua_tointeger( L, -1 ); }
				else if ( !V_stricmp( pszKey, "z" ) ) { bZ = true; }
				else if ( !V_stricmp( pszKey, "p" ) ) { bP = true; }
				else if ( !V_stricmp( pszKey, "r" ) ) { bR = true; }
			}
			lua_pop( L, 1 );
		}

		if ( bX && bY && bZ && !bP && !bR )
		{
			lua_getfield( L, -1, "x" );
			float x = (float)lua_tonumber( L, -1 );
			lua_getfield( L, -1, "y" );
			float y = (float)lua_tonumber( L, -1 );
			lua_getfield( L, -1, "z" );
			float z = (float)lua_tonumber( L, -1 );
			lua_pop( L, 3 );
			lua_pop( L, 1 );
			lua_pushvector( L, Vector( x, y, z ) );
			return;
		}

		if ( bP && bY && bR && !bX && !bZ )
		{
			lua_getfield( L, -1, "p" );
			float p = (float)lua_tonumber( L, -1 );
			lua_getfield( L, -1, "y" );
			float y = (float)lua_tonumber( L, -1 );
			lua_getfield( L, -1, "r" );
			float r = (float)lua_tonumber( L, -1 );
			lua_pop( L, 3 );
			lua_pop( L, 1 );
			lua_pushangle( L, QAngle( p, y, r ) );
			return;
		}
	}
}

// util.JSONToTable( json )
// (non-static: net_ReadTable reuses the decoder, lnet.cpp)
LUA_API int luasrc_UTIL_JSONToTable( lua_State *L )
{
	const char *pszJson = luaL_checkstring( L, 1 );

	KeyValuesJSONParser parser( pszJson );
	KeyValues *pRoot = parser.ParseFile();
	if ( pRoot == NULL )
	{
		lua_pushnil( L );
		return 1;
	}

	JSON_KVToLua( L, pRoot );
	pRoot->deleteThis();
	return 1;
}

// The shared physprops global (game/shared/physics_shared.h:29 in the game
// trees); declared here so the surface-property bindings do not need to drag
// all of physics_shared.h into this file.
extern IPhysicsSurfaceProps *physprops;

// The shared block's filesystem handle (lfilesystem.cpp has its own copy).
static IFileSystem *HL2SB_UtilFS( void )
{
	return filesystem;
}

// util.IsValidModel's "model file doesn't exist on disk" test.
static bool HL2SB_UtilModelExistsOnDisk( const char *pName )
{
	return HL2SB_UtilFS()->FileExists( pName, "GAME" ) ||
	       HL2SB_UtilFS()->FileExists( pName, "MOD" );
}

//=============================================================================
// HL2SB: GMod's util library, second batch (2026-09-25, wiki-checked).
//
// Covers the members addons reach for that the Source-era UTIL_* bindings
// above did not: codecs (Base64/CRC/MD5/SHA1/SHA256/Compress/Decompress),
// SteamID conversion, the geometry/intersection family, surface property
// data, model validity, activity/anim-event name lookups, BlastDamageInfo,
// GMod-shape TraceEntityHull, FilterText and the menu path helpers.  The wiki
// page text backing each contract is in D:\project\wiki\UTIL_*.txt.
//=============================================================================

//-----------------------------------------------------------------------------
// Base64 (util.Base64Encode / util.Base64Decode).  Encode is RFC 2045: a line
// break after every 76th character unless `inline` asks for the raw form.
// Decode tolerates line breaks and other whitespace.
//-----------------------------------------------------------------------------
static const char s_szBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int luasrc_util_Base64Encode (lua_State *L) {
  size_t nLen = 0;
  const unsigned char *pIn = (const unsigned char *)luaL_checklstring( L, 1, &nLen );
  bool bInline = ( lua_toboolean( L, 2 ) != 0 );

  if ( nLen == 0 ) {
    lua_pushstring( L, "" );
    return 1;
  }

  size_t nB64 = ( ( nLen + 2 ) / 3 ) * 4;
  size_t nBreaks = bInline ? 0 : ( nB64 / 76 ) + 1;
  char *pOut = (char *)malloc( nB64 + nBreaks + 1 );
  if ( !pOut ) {
    lua_pushnil( L );
    return 1;
  }

  size_t nOut = 0;
  size_t nLine = 0;
  size_t i = 0;
  while ( i < nLen ) {
    size_t nRemain = ( nLen - i >= 3 ) ? 3 : ( nLen - i );
    unsigned int nBits = (unsigned int)pIn[ i ] << 16;
    if ( nRemain > 1 ) nBits |= (unsigned int)pIn[ i + 1 ] << 8;
    if ( nRemain > 2 ) nBits |= (unsigned int)pIn[ i + 2 ];
    i += nRemain;

    pOut[ nOut++ ] = s_szBase64Alphabet[ ( nBits >> 18 ) & 0x3F ];
    pOut[ nOut++ ] = s_szBase64Alphabet[ ( nBits >> 12 ) & 0x3F ];
    pOut[ nOut++ ] = ( nRemain >= 2 ) ? s_szBase64Alphabet[ ( nBits >> 6 ) & 0x3F ] : '=';
    pOut[ nOut++ ] = ( nRemain >= 3 ) ? s_szBase64Alphabet[ nBits & 0x3F ] : '=';

    if ( !bInline ) {
      nLine += 4;
      if ( nLine >= 76 && i < nLen ) {
        pOut[ nOut++ ] = '\n';
        nLine = 0;
      }
    }
  }

  lua_pushlstring( L, pOut, nOut );
  free( pOut );
  return 1;
}

static int luasrc_util_Base64Decode (lua_State *L) {
  size_t nLen = 0;
  const char *pIn = luaL_checklstring( L, 1, &nLen );

  if ( nLen == 0 ) {
    lua_pushstring( L, "" );
    return 1;
  }

  char *pOut = (char *)malloc( nLen + 3 );
  if ( !pOut ) {
    lua_pushnil( L );
    return 1;
  }

  static signed char s_Reverse[ 256 ];
  static bool s_bReverseInit = false;
  if ( !s_bReverseInit ) {
    memset( s_Reverse, -1, sizeof( s_Reverse ) );
    for ( int i = 0; i < 64; i++ )
      s_Reverse[ (unsigned char)s_szBase64Alphabet[ i ] ] = (signed char)i;
    s_Reverse[ (unsigned char)'-' ] = 62;	// URL-safe tolerance
    s_Reverse[ (unsigned char)'_' ] = 63;
    s_bReverseInit = true;
  }

  unsigned int nBits = 0;
  int nBitsCollected = 0;
  size_t nOut = 0;
  for ( size_t i = 0; i < nLen; i++ ) {
    unsigned char c = (unsigned char)pIn[ i ];
    if ( c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '=' )
      continue;

    int nValue = s_Reverse[ c ];
    if ( nValue < 0 ) {
      free( pOut );
      lua_pushnil( L );
      return 1;
    }

    nBits = ( nBits << 6 ) | (unsigned int)nValue;
    nBitsCollected += 6;

    if ( nBitsCollected >= 8 ) {
      nBitsCollected -= 8;
      pOut[ nOut++ ] = (char)( ( nBits >> nBitsCollected ) & 0xFF );
    }
  }

  lua_pushlstring( L, pOut, nOut );
  free( pOut );
  return 1;
}

//-----------------------------------------------------------------------------
// Checksums: CRC32, MD5, SHA1, SHA256.  CRC returns the unsigned decimal
// spelling (its wiki example prints "3904355907"); the hashes are lower-case
// hex.
//-----------------------------------------------------------------------------
static int luasrc_util_CRC (lua_State *L) {
  size_t nLen = 0;
  const char *pIn = luaL_checklstring( L, 1, &nLen );
  CRC32_t nCrc = CRC32_ProcessSingleBuffer( pIn, (int)nLen );
  char szBuf[ 16 ];
  Q_snprintf( szBuf, sizeof( szBuf ), "%u", (unsigned int)nCrc );
  lua_pushstring( L, szBuf );
  return 1;
}

static int luasrc_util_MD5 (lua_State *L) {
  size_t nLen = 0;
  const char *pIn = luaL_checklstring( L, 1, &nLen );
  MD5Context_t ctx;
  MD5Init( &ctx );
  MD5Update( &ctx, (unsigned char const *)pIn, (unsigned int)nLen );
  unsigned char digest[ MD5_DIGEST_LENGTH ];
  MD5Final( digest, &ctx );

  char szBuf[ MD5_DIGEST_LENGTH * 2 + 1 ];
  for ( int i = 0; i < MD5_DIGEST_LENGTH; i++ )
    Q_snprintf( szBuf + i * 2, 3, "%02x", digest[ i ] );
  lua_pushstring( L, szBuf );
  return 1;
}

static int luasrc_util_SHA1 (lua_State *L) {
  size_t nLen = 0;
  const char *pIn = luaL_checklstring( L, 1, &nLen );
  CSHA1 sha1;
  sha1.Update( (unsigned char *)pIn, (unsigned int)nLen );
  sha1.Final();
  unsigned char digest[ 20 ];
  sha1.GetHash( digest );

  char szBuf[ 41 ];
  for ( int i = 0; i < 20; i++ )
    Q_snprintf( szBuf + i * 2, 3, "%02x", digest[ i ] );
  lua_pushstring( L, szBuf );
  return 1;
}

// util.SHA256.  The fork's crypto set stops at SHA1, so the FIPS 180-4
// compression function is inlined here.
static const unsigned int s_Sh256K[ 64 ] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

#define SH256_ROTR( x, n ) ( ( ( x ) >> ( n ) ) | ( ( x ) << ( 32 - ( n ) ) ) )
#define SH256_CH( x, y, z ) ( ( ( x ) & ( y ) ) ^ ( ~( x ) & ( z ) ) )
#define SH256_MAJ( x, y, z ) ( ( ( x ) & ( y ) ) ^ ( ( x ) & ( z ) ) ^ ( ( y ) & ( z ) ) )
#define SH256_EP0( x ) ( SH256_ROTR( x, 2 ) ^ SH256_ROTR( x, 13 ) ^ SH256_ROTR( x, 22 ) )
#define SH256_EP1( x ) ( SH256_ROTR( x, 6 ) ^ SH256_ROTR( x, 11 ) ^ SH256_ROTR( x, 25 ) )
#define SH256_SIG0( x ) ( SH256_ROTR( x, 7 ) ^ SH256_ROTR( x, 18 ) ^ ( ( x ) >> 3 ) )
#define SH256_SIG1( x ) ( SH256_ROTR( x, 17 ) ^ SH256_ROTR( x, 19 ) ^ ( ( x ) >> 10 ) )

static int luasrc_util_SHA256 (lua_State *L) {
  size_t nLen = 0;
  const char *pIn = luaL_checklstring( L, 1, &nLen );

  unsigned int h[ 8 ] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
  };

  size_t nBlocks = ( nLen + 1 + 8 + 63 ) / 64;
  unsigned char *pMsg = (unsigned char *)malloc( nBlocks * 64 );
  if ( !pMsg ) {
    lua_pushnil( L );
    return 1;
  }
  memset( pMsg, 0, nBlocks * 64 );
  memcpy( pMsg, pIn, nLen );
  pMsg[ nLen ] = 0x80;
  unsigned long long nBits = (unsigned long long)nLen * 8;
  for ( int i = 0; i < 8; i++ )
    pMsg[ nBlocks * 64 - 1 - i ] = (unsigned char)( ( nBits >> ( 8 * i ) ) & 0xFF );

  for ( size_t nBlock = 0; nBlock < nBlocks; nBlock++ ) {
    unsigned int w[ 64 ];
    for ( int i = 0; i < 16; i++ ) {
      const unsigned char *p = pMsg + nBlock * 64 + i * 4;
      w[ i ] = ( (unsigned int)p[ 0 ] << 24 ) | ( (unsigned int)p[ 1 ] << 16 ) |
               ( (unsigned int)p[ 2 ] << 8 ) | (unsigned int)p[ 3 ];
    }
    for ( int i = 16; i < 64; i++ )
      w[ i ] = SH256_SIG1( w[ i - 2 ] ) + w[ i - 7 ] + SH256_SIG0( w[ i - 15 ] ) + w[ i - 16 ];

    unsigned int a = h[ 0 ], b = h[ 1 ], c = h[ 2 ], d = h[ 3 ];
    unsigned int e = h[ 4 ], f = h[ 5 ], g = h[ 6 ], hh = h[ 7 ];
    for ( int i = 0; i < 64; i++ ) {
      unsigned int t1 = hh + SH256_EP1( e ) + SH256_CH( e, f, g ) + s_Sh256K[ i ] + w[ i ];
      unsigned int t2 = SH256_EP0( a ) + SH256_MAJ( a, b, c );
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h[ 0 ] += a; h[ 1 ] += b; h[ 2 ] += c; h[ 3 ] += d;
    h[ 4 ] += e; h[ 5 ] += f; h[ 6 ] += g; h[ 7 ] += hh;
  }
  free( pMsg );

  char szBuf[ 65 ];
  for ( int i = 0; i < 8; i++ )
    Q_snprintf( szBuf + i * 8, 9, "%08x", h[ i ] );
  lua_pushstring( L, szBuf );
  return 1;
}

//-----------------------------------------------------------------------------
// util.Compress / util.Decompress -- LZMA in GMod's envelope: an 8-byte
// little-endian uncompressed-size prefix followed by the raw LZMA stream
// (5 property bytes + 8-byte size + data).  The decoder objects come from
// tier1 (LzmaDec.c); the encoder (LzmaEnc.c/LzFind.c) joined tier1's build
// for this.  Decompress also accepts the engine's own "LZMA!"-wrapped blobs
// and honours the wiki's maxSize guard against decompression bombs.
//-----------------------------------------------------------------------------
static void *HL2SB_LzmaAlloc( void *p, size_t size ) { (void)p; return malloc( size ); }
static void HL2SB_LzmaFree( void *p, void *address ) { (void)p; free( address ); }

static int luasrc_util_Compress (lua_State *L) {
  size_t nLen = 0;
  const char *pIn = luaL_checklstring( L, 1, &nLen );

  if ( nLen == 0 ) {
    lua_pushstring( L, "" );
    return 1;
  }

  size_t nDestCap = nLen + nLen / 20 * 21 + ( 1 << 16 );
  Byte *pDest = (Byte *)malloc( nDestCap );
  if ( !pDest ) {
    lua_pushnil( L );
    return 1;
  }

  CLzmaEncProps props;
  LzmaEncProps_Init( &props );
  props.level = 9;

  SizeT nDestLen = nDestCap;
  Byte propsEncoded[ LZMA_PROPS_SIZE ];
  SizeT nPropsSize = LZMA_PROPS_SIZE;

  ISzAlloc alloc = { HL2SB_LzmaAlloc, HL2SB_LzmaFree };
  SRes res = LzmaEncode( pDest, &nDestLen, (const Byte *)pIn, nLen,
                         &props, propsEncoded, &nPropsSize, 0, NULL, &alloc, &alloc );
  if ( res != SZ_OK || nPropsSize != LZMA_PROPS_SIZE ) {
    free( pDest );
    lua_pushnil( L );
    return 1;
  }

  // GMod envelope: 8-byte LE uncompressed size, then the raw stream (props
  // (5) + size (8) + data).  The SDK's one-shot LzmaEncode delegates to
  // LzmaEnc_MemEncode, which emits RAW compressed data with NO 13-byte
  // header -- the header-writing helper is Valve's own 5-arg LzmaEncode in
  // utils/lzma/lzma.cpp (its "strip theirs" comment refers to that one).
  // Stripping 13 bytes from the raw output was chopping real compressed
  // data, and the decoder answered SZ_ERROR_DATA on our own output.
  size_t nRawData = nDestLen;
  size_t nTotal = 8 + LZMA_PROPS_SIZE + 8 + nRawData;
  char *pOut = (char *)malloc( nTotal );
  if ( !pOut ) {
    free( pDest );
    lua_pushnil( L );
    return 1;
  }

  size_t nAt = 0;
  for ( int i = 0; i < 8; i++ )
    pOut[ nAt++ ] = (char)( ( (unsigned long long)nLen >> ( 8 * i ) ) & 0xFF );
  memcpy( pOut + nAt, propsEncoded, LZMA_PROPS_SIZE );
  nAt += LZMA_PROPS_SIZE;
  for ( int i = 0; i < 8; i++ )
    pOut[ nAt++ ] = (char)( ( (unsigned long long)nLen >> ( 8 * i ) ) & 0xFF );
  memcpy( pOut + nAt, pDest, nRawData );
  nAt += nRawData;
  free( pDest );

  lua_pushlstring( L, pOut, nAt );
  free( pOut );
  return 1;
}

static int luasrc_util_Decompress (lua_State *L) {
  size_t nLen = 0;
  const char *pIn = luaL_checklstring( L, 1, &nLen );
  int nMaxSize = luaL_optint( L, 2, -1 );

  if ( nLen == 0 ) {
    lua_pushnil( L );
    return 1;
  }

  // The engine's own "LZMA!"-wrapped blobs (BSP lumps, older data) go through
  // tier1's CLZMA directly (the utils/lzma glue is not linked into the game).
  if ( CLZMA::IsCompressed( (unsigned char *)pIn ) ) {
    unsigned int nActual = CLZMA::GetActualSize( (unsigned char *)pIn );
    if ( nActual == 0 || ( nMaxSize > 0 && (int)nActual > nMaxSize ) ) {
      lua_pushnil( L );
      return 1;
    }
    unsigned char *pOut = (unsigned char *)malloc( nActual );
    if ( !pOut ) {
      lua_pushnil( L );
      return 1;
    }
    if ( CLZMA::Uncompress( (unsigned char *)pIn, pOut ) == 0 ) {
      free( pOut );
      lua_pushnil( L );
      return 1;
    }
    lua_pushlstring( L, (const char *)pOut, nActual );
    free( pOut );
    return 1;
  }

  if ( nLen < 8 + LZMA_PROPS_SIZE + 8 ) {
    lua_pushnil( L );
    return 1;
  }

  unsigned long long nActual = 0;
  for ( int i = 7; i >= 0; i-- )
    nActual = ( nActual << 8 ) | (unsigned char)pIn[ i ];
  if ( nActual == 0 || nActual > 0x7FFFFFFFull || ( nMaxSize > 0 && (unsigned long long)nMaxSize < nActual ) ) {
    lua_pushnil( L );
    return 1;
  }

  // GMod envelope: [8-byte LE size][props(5)][size(8)][data].  Wrap the raw
  // stream in the engine's "LZMA!" header and let tier1's CLZMA decode it --
  // the same battle-tested path the engine uses for BSP lumps (a hand-rolled
  // LzmaDec loop here answered nil for this fork's own output).
  size_t nDataLen = nLen - 8 - LZMA_PROPS_SIZE - 8;
  size_t nWrapSize = sizeof( lzma_header_t ) + nDataLen;
  unsigned char *pWrap = (unsigned char *)malloc( nWrapSize );
  if ( !pWrap ) {
    lua_pushnil( L );
    return 1;
  }
  lzma_header_t *pHdr = (lzma_header_t *)pWrap;
  pHdr->id = LZMA_ID;
  pHdr->actualSize = (unsigned int)nActual;
  pHdr->lzmaSize = (unsigned int)nDataLen;
  memcpy( pHdr->properties, pIn + 8, LZMA_PROPS_SIZE );
  memcpy( pWrap + sizeof( lzma_header_t ), pIn + 8 + LZMA_PROPS_SIZE + 8, nDataLen );

  unsigned char *pOut = (unsigned char *)malloc( (size_t)nActual );
  if ( !pOut ) {
    free( pWrap );
    lua_pushnil( L );
    return 1;
  }

  unsigned int nUncompressed = CLZMA::Uncompress( pWrap, pOut );
  free( pWrap );
  if ( nUncompressed == 0 || nUncompressed != (unsigned int)nActual ) {
    free( pOut );
    lua_pushnil( L );
    return 1;
  }

  lua_pushlstring( L, (const char *)pOut, nUncompressed );
  free( pOut );
  return 1;
}

//-----------------------------------------------------------------------------
// util.SteamIDTo64 / util.SteamIDFrom64.  64-bit arithmetic -- Lua numbers
// lose precision above 2^53, so this is C++ even though the math is simple.
// From64 loses universe/account-type/instance the way GMod's does.
//-----------------------------------------------------------------------------
static int luasrc_util_SteamIDTo64 (lua_State *L) {
  const char *pIn = luaL_checkstring( L, 1 );
  int nUniverse = 0, nAuthServer = 0;
  unsigned int nAccountId = 0;
  if ( sscanf( pIn, "STEAM_%d:%d:%u", &nUniverse, &nAuthServer, &nAccountId ) != 3 ) {
    lua_pushnil( L );
    return 1;
  }

  unsigned long long nId64 = 76561197960265728ull +
                             ( (unsigned long long)( nUniverse > 0 ? nUniverse - 1 : 0 ) << 32 ) +
                             (unsigned long long)nAuthServer +
                             (unsigned long long)nAccountId * 2ull;
  char szBuf[ 32 ];
  Q_snprintf( szBuf, sizeof( szBuf ), "%llu", nId64 );
  lua_pushstring( L, szBuf );
  return 1;
}

static int luasrc_util_SteamIDFrom64 (lua_State *L) {
  const char *pIn = luaL_checkstring( L, 1 );
  unsigned long long nId64 = V_strtoui64( pIn, NULL, 10 );
  unsigned long long nAccount = nId64 - 76561197960265728ull;
  if ( nAccount > 0xFFFFFFFFull ) {
    lua_pushnil( L );
    return 1;
  }

  int nAuthServer = (int)( nAccount % 2 );
  unsigned int nAccountId = (unsigned int)( nAccount / 2 );
  char szBuf[ 32 ];
  Q_snprintf( szBuf, sizeof( szBuf ), "STEAM_0:%d:%u", nAuthServer, nAccountId );
  lua_pushstring( L, szBuf );
  return 1;
}

//-----------------------------------------------------------------------------
// util.SharedRandom -- GMod's name for the engine's SharedRandomFloat.
//-----------------------------------------------------------------------------
static int luasrc_util_SharedRandom (lua_State *L) {
  return luasrc_SharedRandomFloat( L );
}

//-----------------------------------------------------------------------------
// util.AimVector( ang, fov, x, y, scrW, scrH ).  The view basis scaled by the
// screen position in tangent space, matching gui.ScreenToVector's inverse.
//-----------------------------------------------------------------------------
static int luasrc_util_AimVector (lua_State *L) {
  QAngle viewAng = luaL_checkangle( L, 1 );
  float flFov = (float)luaL_checknumber( L, 2 );
  float flX = (float)luaL_checknumber( L, 3 );
  float flY = (float)luaL_checknumber( L, 4 );
  float flScrW = (float)luaL_checknumber( L, 5 );
  float flScrH = (float)luaL_checknumber( L, 6 );

  float flAspect = ( flScrH != 0.0f ) ? ( flScrW / flScrH ) : 1.0f;
  float flTanHalf = tanf( DEG2RAD( flFov ) * 0.5f );
  float flXN = ( 2.0f * flX / flScrW - 1.0f );
  float flYN = ( 1.0f - 2.0f * flY / flScrH );

  Vector vecForward, vecRight, vecUp;
  AngleVectors( viewAng, &vecForward, &vecRight, &vecUp );

  Vector vecDir = vecForward + vecRight * ( flXN * flTanHalf * flAspect ) + vecUp * ( flYN * flTanHalf );
  VectorNormalize( vecDir );
  lua_pushvector( L, vecDir );
  return 1;
}

//-----------------------------------------------------------------------------
// util.DistanceToLine( lineStart, lineEnd, pointPos ) -> distance, closest
// point, distance along the line (clamped to the segment).
//-----------------------------------------------------------------------------
static int luasrc_util_DistanceToLine (lua_State *L) {
  Vector vecStart = luaL_checkvector( L, 1 );
  Vector vecEnd = luaL_checkvector( L, 2 );
  Vector vecPoint = luaL_checkvector( L, 3 );

  Vector vecClosest;
  float flT = 0.0f;
  CalcClosestPointOnLineSegment( vecPoint, vecStart, vecEnd, vecClosest, &flT );

  lua_pushnumber( L, vecPoint.DistTo( vecClosest ) );
  lua_pushvector( L, vecClosest );
  lua_pushnumber( L, flT * vecStart.DistTo( vecEnd ) );
  return 3;
}

//-----------------------------------------------------------------------------
// util.IntersectRayWithPlane( rayOrigin, rayDirection, planePosition,
// planeNormal ) -> hit position, distance -- or nothing.
//-----------------------------------------------------------------------------
static int luasrc_util_IntersectRayWithPlane (lua_State *L) {
  Vector vecOrigin = luaL_checkvector( L, 1 );
  Vector vecDir = luaL_checkvector( L, 2 );
  Vector vecPlanePos = luaL_checkvector( L, 3 );
  Vector vecPlaneNormal = luaL_checkvector( L, 4 );

  float flDenom = vecPlaneNormal.Dot( vecDir );
  if ( fabsf( flDenom ) < 1e-6f )
    return 0;

  float flT = ( vecPlanePos - vecOrigin ).Dot( vecPlaneNormal ) / flDenom;
  if ( flT < 0.0f )
    return 0;

  lua_pushvector( L, vecOrigin + vecDir * flT );
  lua_pushnumber( L, flT * vecDir.Length() );
  return 2;
}

// Ray vs axis-aligned slabs in the box's local space.  Returns the entry
// fraction and the local-space entry normal, or false for a miss.
static bool HL2SB_RayAABBLocal ( const Vector &vecLocalStart, const Vector &vecLocalDelta,
                                 const Vector &vecMins, const Vector &vecMaxs,
                                 float &flFraction, Vector &vecLocalNormal )
{
  float flTMin = 0.0f, flTMax = 1.0f;
  int nAxis = -1;
  float flSign = 0.0f;

  for ( int i = 0; i < 3; i++ ) {
    float flStart = vecLocalStart[ i ];
    float flDelta = vecLocalDelta[ i ];
    float flLo = vecMins[ i ], flHi = vecMaxs[ i ];

    if ( fabsf( flDelta ) < 1e-8f ) {
      if ( flStart < flLo || flStart > flHi )
        return false;
      continue;
    }

    float flInv = 1.0f / flDelta;
    float flT1 = ( flLo - flStart ) * flInv;
    float flT2 = ( flHi - flStart ) * flInv;
    float flThisSign = -1.0f;
    if ( flT1 > flT2 ) {
      float flSwap = flT1; flT1 = flT2; flT2 = flSwap;
      flThisSign = 1.0f;
    }
    if ( flT1 > flTMin ) { flTMin = flT1; nAxis = i; flSign = flThisSign; }
    if ( flT2 < flTMax ) flTMax = flT2;
    if ( flTMin > flTMax )
      return false;
  }

  if ( nAxis < 0 || flTMax < 0.0f || flTMin > 1.0f )
    return false;

  flFraction = flTMin;
  vecLocalNormal.Init( 0, 0, 0 );
  vecLocalNormal[ nAxis ] = flSign;
  return true;
}

//-----------------------------------------------------------------------------
// util.IntersectRayWithOBB( rayStart, rayDelta, boxOrigin, boxAngles, mins,
// maxs ) -> hit position, world normal, fraction -- or nothing.
//-----------------------------------------------------------------------------
static int luasrc_util_IntersectRayWithOBB (lua_State *L) {
  Vector vecStart = luaL_checkvector( L, 1 );
  Vector vecDelta = luaL_checkvector( L, 2 );
  Vector vecBoxOrigin = luaL_checkvector( L, 3 );
  QAngle angBox = luaL_checkangle( L, 4 );
  Vector vecMins = luaL_checkvector( L, 5 );
  Vector vecMaxs = luaL_checkvector( L, 6 );

  matrix3x4_t matBox;
  AngleMatrix( angBox, vecBoxOrigin, matBox );

  matrix3x4_t matInv;
  MatrixInvert( matBox, matInv );

  Vector vecLocalStart, vecLocalDelta;
  VectorTransform( vecStart, matInv, vecLocalStart );
  VectorRotate( vecDelta, matInv, vecLocalDelta );

  float flFraction = 0.0f;
  Vector vecLocalNormal;
  if ( !HL2SB_RayAABBLocal( vecLocalStart, vecLocalDelta, vecMins, vecMaxs, flFraction, vecLocalNormal ) )
    return 0;

  Vector vecWorldNormal;
  VectorRotate( vecLocalNormal, matBox, vecWorldNormal );

  lua_pushvector( L, vecStart + vecDelta * flFraction );
  lua_pushvector( L, vecWorldNormal );
  lua_pushnumber( L, flFraction );
  return 3;
}

//-----------------------------------------------------------------------------
// util.IntersectRayWithSphere( rayOrigin, rayDelta, spherePosition, radius )
// -> entry fraction, exit fraction -- or nothing.
//-----------------------------------------------------------------------------
static int luasrc_util_IntersectRayWithSphere (lua_State *L) {
  Vector vecOrigin = luaL_checkvector( L, 1 );
  Vector vecDelta = luaL_checkvector( L, 2 );
  Vector vecCenter = luaL_checkvector( L, 3 );
  float flRadius = (float)luaL_checknumber( L, 4 );

  Vector vecM = vecOrigin - vecCenter;
  float flA = vecDelta.Dot( vecDelta );
  if ( flA < 1e-12f )
    return 0;
  float flB = 2.0f * vecM.Dot( vecDelta );
  float flC = vecM.Dot( vecM ) - flRadius * flRadius;
  float flDisc = flB * flB - 4.0f * flA * flC;
  if ( flDisc < 0.0f )
    return 0;

  float flSqrt = sqrtf( flDisc );
  float flT1 = ( -flB - flSqrt ) / ( 2.0f * flA );
  float flT2 = ( -flB + flSqrt ) / ( 2.0f * flA );
  if ( flT2 < 0.0f || flT1 > 1.0f )
    return 0;

  lua_pushnumber( L, flT1 );
  lua_pushnumber( L, flT2 );
  return 2;
}

//-----------------------------------------------------------------------------
// util.IntersectRayWithTriangle( rayOrigin, rayEnd, triA, triB, triC,
// oneSided ) -> hit position, fraction -- or nothing.  Moller-Trumbore.
//-----------------------------------------------------------------------------
static int luasrc_util_IntersectRayWithTriangle (lua_State *L) {
  Vector vecOrigin = luaL_checkvector( L, 1 );
  Vector vecEnd = luaL_checkvector( L, 2 );
  Vector vecA = luaL_checkvector( L, 3 );
  Vector vecB = luaL_checkvector( L, 4 );
  Vector vecC = luaL_checkvector( L, 5 );
  bool bOneSided = ( lua_toboolean( L, 6 ) != 0 );

  Vector vecDir = vecEnd - vecOrigin;
  Vector vecE1 = vecB - vecA;
  Vector vecE2 = vecC - vecA;
  Vector vecP = vecDir.Cross( vecE2 );

  float flDet = vecE1.Dot( vecP );
  if ( bOneSided && flDet < 1e-9f )
    return 0;
  if ( fabsf( flDet ) < 1e-9f )
    return 0;

  float flInvDet = 1.0f / flDet;
  Vector vecT = vecOrigin - vecA;
  float flU = vecT.Dot( vecP ) * flInvDet;
  if ( flU < 0.0f || flU > 1.0f )
    return 0;

  Vector vecQ = vecT.Cross( vecE1 );
  float flV = vecDir.Dot( vecQ ) * flInvDet;
  if ( flV < 0.0f || flU + flV > 1.0f )
    return 0;

  float flT = vecE2.Dot( vecQ ) * flInvDet;
  if ( flT < 0.0f || flT > 1.0f )
    return 0;

  lua_pushvector( L, vecOrigin + vecDir * flT );
  lua_pushnumber( L, flT );
  return 2;
}

//-----------------------------------------------------------------------------
// The Is*Intersecting* geometry family.  This fork's mathlib does not publish
// the box/cone tests, so they are implemented here from first principles.
//-----------------------------------------------------------------------------
static int luasrc_util_IsBoxIntersectingBox (lua_State *L) {
  Vector a1 = luaL_checkvector( L, 1 ), a2 = luaL_checkvector( L, 2 );
  Vector b1 = luaL_checkvector( L, 3 ), b2 = luaL_checkvector( L, 4 );

  lua_pushboolean( L, a1.x <= b2.x && a2.x >= b1.x &&
                       a1.y <= b2.y && a2.y >= b1.y &&
                       a1.z <= b2.z && a2.z >= b1.z );
  return 1;
}

static int luasrc_util_IsBoxIntersectingSphere (lua_State *L) {
  Vector vecMins = luaL_checkvector( L, 1 ), vecMaxs = luaL_checkvector( L, 2 );
  Vector vecCenter = luaL_checkvector( L, 3 );
  float flRadius = (float)luaL_checknumber( L, 4 );

  Vector vecClamped(
    clamp( vecCenter.x, vecMins.x, vecMaxs.x ),
    clamp( vecCenter.y, vecMins.y, vecMaxs.y ),
    clamp( vecCenter.z, vecMins.z, vecMaxs.z ) );

  lua_pushboolean( L, vecClamped.DistToSqr( vecCenter ) <= flRadius * flRadius );
  return 1;
}

static int luasrc_util_IsSphereIntersectingSphere (lua_State *L) {
  Vector vecCenter1 = luaL_checkvector( L, 1 );
  float flRadius1 = (float)luaL_checknumber( L, 2 );
  Vector vecCenter2 = luaL_checkvector( L, 3 );
  float flRadius2 = (float)luaL_checknumber( L, 4 );

  lua_pushboolean( L, vecCenter1.DistToSqr( vecCenter2 ) <= ( flRadius1 + flRadius2 ) * ( flRadius1 + flRadius2 ) );
  return 1;
}

// Separating-axis test over the 3+3 face normals (edge-cross axes covered by
// the 6-axis variant are redundant for these box representations, so the two
// boxes are tested in each other's frames through both axis sets).
static int luasrc_util_IsOBBIntersectingOBB (lua_State *L) {
  Vector vecOrigin1 = luaL_checkvector( L, 1 );
  QAngle ang1 = luaL_checkangle( L, 2 );
  Vector vecMins1 = luaL_checkvector( L, 3 ), vecMaxs1 = luaL_checkvector( L, 4 );
  Vector vecOrigin2 = luaL_checkvector( L, 5 );
  QAngle ang2 = luaL_checkangle( L, 6 );
  Vector vecMins2 = luaL_checkvector( L, 7 ), vecMaxs2 = luaL_checkvector( L, 8 );
  float flTolerance = (float)luaL_optnumber( L, 9, 0.0 );

  matrix3x4_t mat1, mat2;
  AngleMatrix( ang1, vecOrigin1, mat1 );
  AngleMatrix( ang2, vecOrigin2, mat2 );

  Vector axes1[ 3 ], axes2[ 3 ];
  MatrixGetColumn( mat1, 0, axes1[ 0 ] );
  MatrixGetColumn( mat1, 1, axes1[ 1 ] );
  MatrixGetColumn( mat1, 2, axes1[ 2 ] );
  MatrixGetColumn( mat2, 0, axes2[ 0 ] );
  MatrixGetColumn( mat2, 1, axes2[ 1 ] );
  MatrixGetColumn( mat2, 2, axes2[ 2 ] );

  Vector vecHalf1 = ( vecMaxs1 - vecMins1 ) * 0.5f;
  Vector vecHalf2 = ( vecMaxs2 - vecMins2 ) * 0.5f;
  Vector vecCenter1 = vecOrigin1 + ( vecMins1 + vecMaxs1 ) * 0.5f;
  Vector vecCenter2 = vecOrigin2 + ( vecMins2 + vecMaxs2 ) * 0.5f;
  Vector vecD = vecCenter2 - vecCenter1;

  Vector vecTest[ 6 ];
  for ( int i = 0; i < 3; i++ ) vecTest[ i ] = axes1[ i ];
  for ( int i = 0; i < 3; i++ ) vecTest[ 3 + i ] = axes2[ i ];

  bool bIntersects = true;
  for ( int nAxis = 0; nAxis < 6 && bIntersects; nAxis++ ) {
    Vector vecL = vecTest[ nAxis ];
    float flLen = vecL.Length();
    if ( flLen < 1e-6f )
      continue;
    vecL /= flLen;

    float flRadiusA = 0.0f, flRadiusB = 0.0f;
    for ( int i = 0; i < 3; i++ ) {
      flRadiusA += fabsf( axes1[ i ].Dot( vecL ) ) * vecHalf1[ i ];
      flRadiusB += fabsf( axes2[ i ].Dot( vecL ) ) * vecHalf2[ i ];
    }

    float flDist = fabsf( vecD.Dot( vecL ) );
    if ( flDist > flRadiusA + flRadiusB + flTolerance )
      bIntersects = false;
  }

  lua_pushboolean( L, bIntersects );
  return 1;
}

static int luasrc_util_IsPointInCone (lua_State *L) {
  Vector vecPoint = luaL_checkvector( L, 1 );
  Vector vecOrigin = luaL_checkvector( L, 2 );
  Vector vecAxis = luaL_checkvector( L, 3 );
  float flSine = (float)luaL_checknumber( L, 4 );
  float flLength = (float)luaL_checknumber( L, 5 );

  Vector vecD = vecPoint - vecOrigin;
  float flH = vecD.Dot( vecAxis );
  if ( flH < 0.0f || flH > flLength ) {
    lua_pushboolean( L, false );
    return 1;
  }

  Vector vecPerp = vecD - vecAxis * flH;
  float flHalfAngleSlope = flSine / sqrtf( 1.0f - flSine * flSine );
  float flConeRadius = flH * flHalfAngleSlope;
  lua_pushboolean( L, vecPerp.LengthSqr() <= flConeRadius * flConeRadius );
  return 1;
}

static int luasrc_util_IsRayIntersectingRay (lua_State *L) {
  Vector s1 = luaL_checkvector( L, 1 ), e1 = luaL_checkvector( L, 2 );
  Vector s2 = luaL_checkvector( L, 3 ), e2 = luaL_checkvector( L, 4 );

  // Closest points of two segments (clamped), Ericson ch. 5.
  Vector d1 = e1 - s1, d2 = e2 - s2, r = s1 - s2;
  float a = d1.Dot( d1 ), e = d2.Dot( d2 ), f = d2.Dot( r );
  float c = d1.Dot( r ), b = d1.Dot( d2 );
  float flDenom = a * e - b * b;

  float flS = 0.0f, flT = 0.0f;
  if ( flDenom > 1e-8f )
    flS = clamp( ( b * f - c * e ) / flDenom, 0.0f, 1.0f );
  float flTNom = b * flS + f;
  flT = ( e > 1e-8f ) ? clamp( flTNom / e, 0.0f, 1.0f ) : 0.0f;

  Vector p1 = s1 + d1 * flS;
  Vector p2 = s2 + d2 * flT;

  lua_pushboolean( L, p1.DistToSqr( p2 ) < 0.01f );
  lua_pushnumber( L, flS );
  lua_pushnumber( L, flT );
  return 3;
}

static int luasrc_util_IsSphereIntersectingCone (lua_State *L) {
  Vector vecCenter = luaL_checkvector( L, 1 );
  float flRadius = (float)luaL_checknumber( L, 2 );
  Vector vecOrigin = luaL_checkvector( L, 3 );
  Vector vecAxis = luaL_checkvector( L, 4 );
  float flSine = (float)luaL_checknumber( L, 5 );
  float flCosine = (float)luaL_checknumber( L, 6 );

  Vector vecD = vecCenter - vecOrigin;
  float flH = vecD.Dot( vecAxis );
  float flPerp = ( vecD - vecAxis * flH ).Length();

  // Signed distance from the sphere center to the cone's lateral surface.
  float flLatDist = flPerp * flCosine - flH * flSine;
  if ( flLatDist > flRadius ) {
    lua_pushboolean( L, false );
    return 1;
  }
  if ( flH >= 0.0f ) {
    lua_pushboolean( L, true );
    return 1;
  }
  // Behind the apex: the sphere must still reach it.
  lua_pushboolean( L, vecD.Length() <= flRadius );
  return 1;
}

//-----------------------------------------------------------------------------
// Model membership: IsModelLoaded / IsValidModel / IsValidProp /
// IsValidRagdoll.  IsValidModel follows the wiki's name rules (leading space,
// wrong root, animation-only name fragments, .bsp), then precaches.
//-----------------------------------------------------------------------------
static bool HL2SB_UtilModelNameValid ( const char *pName )
{
  static const char *const s_pBadFragments[] = {
    "_gestures", "_animations", "_postures", "_gst", "_pst", "_shd", "_ss", "_anm", ".bsp", "cs_fix"
  };

  if ( !pName || pName[ 0 ] == '\0' || pName[ 0 ] == ' ' )
    return false;
  if ( !V_strnicmp( pName, "maps", 4 ) )
    return false;
  if ( V_strnicmp( pName, "models", 6 ) != 0 )
    return false;

  char szLower[ MAX_PATH ];
  V_strncpy( szLower, pName, sizeof( szLower ) );
  V_strlower( szLower );
  for ( int i = 0; i < ARRAYSIZE( s_pBadFragments ); i++ ) {
    if ( strstr( szLower, s_pBadFragments[ i ] ) )
      return false;
  }
  return true;
}

// Engine PrecacheModel spelling differs per realm; the shared name lives on
// the model preview side.  Declared locally so this file keeps one registry.
static int luasrc_util_IsModelLoaded (lua_State *L) {
  const char *pName = luaL_checkstring( L, 1 );
  int nIndex = modelinfo->GetModelIndex( pName );
  lua_pushboolean( L, nIndex >= 0 && modelinfo->GetModel( nIndex ) != NULL );
  return 1;
}

static int luasrc_util_IsValidModel (lua_State *L) {
  const char *pName = luaL_checkstring( L, 1 );
  if ( !HL2SB_UtilModelNameValid( pName ) ) {
    lua_pushboolean( L, false );
    return 1;
  }

  // "If the model isn't precached on the server, AND if the model file
  // doesn't exist on disk" -> invalid; otherwise running this precaches it.
  if ( modelinfo->GetModelIndex( pName ) < 0 &&
       !HL2SB_UtilModelExistsOnDisk( pName ) ) {
    lua_pushboolean( L, false );
    return 1;
  }

  CBaseEntity::PrecacheModel( pName );
  lua_pushboolean( L, true );
  return 1;
}

static int luasrc_util_IsValidProp (lua_State *L) {
  const char *pName = luaL_checkstring( L, 1 );
  int nIndex = modelinfo->GetModelIndex( pName );
  if ( nIndex < 0 || !HL2SB_UtilModelNameValid( pName ) ) {
    lua_pushboolean( L, false );
    return 1;
  }

  vcollide_t *pCollide = modelinfo->GetVCollide( nIndex );
  lua_pushboolean( L, pCollide != NULL && pCollide->solidCount > 0 );
  return 1;
}

static int luasrc_util_IsValidRagdoll (lua_State *L) {
  const char *pName = luaL_checkstring( L, 1 );
  int nIndex = modelinfo->GetModelIndex( pName );
  if ( nIndex < 0 ) {
    lua_pushboolean( L, false );
    return 1;
  }

  // A ragdoll setup is a vcollide with more than one solid (bone solids).
  vcollide_t *pCollide = modelinfo->GetVCollide( nIndex );
  lua_pushboolean( L, pCollide != NULL && pCollide->solidCount > 1 );
  return 1;
}

//-----------------------------------------------------------------------------
// Surface properties (IPhysicsSurfaceProps, shared): GetSurfaceIndex /
// GetSurfacePropName / GetSurfaceData.  The sound fields are string-table
// handles resolved through GetString(); unmapped wiki fields get defaults.
//-----------------------------------------------------------------------------
static int luasrc_util_GetSurfaceIndex (lua_State *L) {
  const char *pName = luaL_checkstring( L, 1 );
  int nIndex = physprops->GetSurfaceIndex( pName );
  if ( nIndex < 0 ) {
    lua_pushnil( L );
    return 1;
  }
  lua_pushinteger( L, nIndex );
  return 1;
}

static int luasrc_util_GetSurfacePropName (lua_State *L) {
  int nIndex = luaL_checkint( L, 1 );
  const char *pName = physprops->GetPropName( nIndex );
  if ( !pName ) {
    lua_pushnil( L );
    return 1;
  }
  lua_pushstring( L, pName );
  return 1;
}

static void HL2SB_PushSurfaceSoundField (lua_State *L, const char *pKey, unsigned short nHandle )
{
  const char *pName = ( nHandle != 0xFFFF ) ? physprops->GetString( nHandle ) : NULL;
  lua_pushstring( L, pKey );
  lua_pushstring( L, pName ? pName : "" );
  lua_rawset( L, -3 );
}

static int luasrc_util_GetSurfaceData (lua_State *L) {
  int nIndex = luaL_checkint( L, 1 );
  surfacedata_t *pData = physprops->GetSurfaceData( nIndex );
  if ( !pData ) {
    return 0;
  }

  lua_newtable( L );
  const char *pPropName = physprops->GetPropName( nIndex );
  lua_pushstring( L, "name" );
  lua_pushstring( L, pPropName ? pPropName : "" );
  lua_rawset( L, -3 );

  lua_pushstring( L, "friction" );       lua_pushnumber( L, pData->physics.friction );       lua_rawset( L, -3 );
  lua_pushstring( L, "elasticity" );     lua_pushnumber( L, pData->physics.elasticity );   lua_rawset( L, -3 );
  lua_pushstring( L, "density" );        lua_pushnumber( L, pData->physics.density );      lua_rawset( L, -3 );
  lua_pushstring( L, "thickness" );      lua_pushnumber( L, pData->physics.thickness );    lua_rawset( L, -3 );
  lua_pushstring( L, "dampening" );      lua_pushnumber( L, pData->physics.dampening );    lua_rawset( L, -3 );

  lua_pushstring( L, "reflectivity" );            lua_pushnumber( L, pData->audio.reflectivity );           lua_rawset( L, -3 );
  lua_pushstring( L, "hardnessFactor" );          lua_pushnumber( L, pData->audio.hardnessFactor );         lua_rawset( L, -3 );
  lua_pushstring( L, "roughnessFactor" );         lua_pushnumber( L, pData->audio.roughnessFactor );        lua_rawset( L, -3 );
  lua_pushstring( L, "roughThreshold" );          lua_pushnumber( L, pData->audio.roughThreshold );         lua_rawset( L, -3 );
  lua_pushstring( L, "hardThreshold" );           lua_pushnumber( L, pData->audio.hardThreshold );          lua_rawset( L, -3 );
  lua_pushstring( L, "hardVelocityThreshold" );   lua_pushnumber( L, pData->audio.hardVelocityThreshold );  lua_rawset( L, -3 );

  lua_pushstring( L, "material" );        lua_pushinteger( L, pData->game.material );        lua_rawset( L, -3 );
  lua_pushstring( L, "climbable" );       lua_pushinteger( L, pData->game.climbable );       lua_rawset( L, -3 );
  lua_pushstring( L, "maxSpeedFactor" );  lua_pushnumber( L, pData->game.maxSpeedFactor );   lua_rawset( L, -3 );
  lua_pushstring( L, "jumpFactor" );      lua_pushnumber( L, pData->game.jumpFactor );       lua_rawset( L, -3 );

  HL2SB_PushSurfaceSoundField( L, "stepLeftSound",      pData->sounds.stepleft );
  HL2SB_PushSurfaceSoundField( L, "stepRightSound",     pData->sounds.stepright );
  HL2SB_PushSurfaceSoundField( L, "impactSoftSound",    pData->sounds.impactSoft );
  HL2SB_PushSurfaceSoundField( L, "impactHardSound",    pData->sounds.impactHard );
  HL2SB_PushSurfaceSoundField( L, "scrapeSmoothSound",  pData->sounds.scrapeSmooth );
  HL2SB_PushSurfaceSoundField( L, "scrapeRoughSound",   pData->sounds.scrapeRough );
  HL2SB_PushSurfaceSoundField( L, "bulletImpactSound",  pData->sounds.bulletImpact );
  HL2SB_PushSurfaceSoundField( L, "rollingSound",       pData->sounds.rolling );
  HL2SB_PushSurfaceSoundField( L, "breakSound",         pData->sounds.breakSound );
  HL2SB_PushSurfaceSoundField( L, "strainSound",        pData->sounds.strainSound );
  return 1;
}

//-----------------------------------------------------------------------------
// Activity / anim-event name lookups (shared lists).  Unknown names answer
// nil, the way GMod answers nil for names the lists do not carry.
//-----------------------------------------------------------------------------
static int luasrc_util_GetActivityIDByName (lua_State *L) {
  const char *pName = luaL_checkstring( L, 1 );
  int nIndex = ActivityList_IndexForName( pName );
  if ( nIndex < 0 ) {
    lua_pushnil( L );
    return 1;
  }
  lua_pushinteger( L, nIndex );
  return 1;
}

static int luasrc_util_GetActivityNameByID (lua_State *L) {
  int nIndex = luaL_checkint( L, 1 );
  const char *pName = ActivityList_NameForIndex( nIndex );
  if ( !pName || !pName[ 0 ] ) {
    lua_pushnil( L );
    return 1;
  }
  lua_pushstring( L, pName );
  return 1;
}

static int luasrc_util_GetAnimEventIDByName (lua_State *L) {
  const char *pName = luaL_checkstring( L, 1 );
  int nIndex = EventList_IndexForName( pName );
  if ( nIndex < 0 ) {
    lua_pushnil( L );
    return 1;
  }
  lua_pushinteger( L, nIndex );
  return 1;
}

static int luasrc_util_GetAnimEventNameByID (lua_State *L) {
  int nIndex = luaL_checkint( L, 1 );
  const char *pName = EventList_NameForIndex( nIndex );
  if ( !pName || !pName[ 0 ] ) {
    lua_pushnil( L );
    return 1;
  }
  lua_pushstring( L, pName );
  return 1;
}

//-----------------------------------------------------------------------------
// util.BlastDamageInfo( dmginfo, origin, radius ) -- spherical RadiusDamage.
//-----------------------------------------------------------------------------
static int luasrc_UTIL_BlastDamageInfo (lua_State *L) {
#ifndef CLIENT_DLL
  CTakeDamageInfo dmgInfo( luaL_checkdamageinfo( L, 1 ) );
  Vector vecOrigin = luaL_checkvector( L, 2 );
  float flRadius = (float)luaL_checknumber( L, 3 );

  if ( g_pGameRules == NULL ) {
    HL2SB_WarnOnce( "blastdamageinfo-norules",
      "util.BlastDamageInfo called with g_pGameRules == NULL (level shutting down?); ignored" );
    return 0;
  }
  if ( !IsFinite( flRadius ) || flRadius <= 0.0f ) {
    luaL_error( L, "util.BlastDamageInfo: damageRadius must be > 0" );
    return 0;
  }

  RadiusDamage( dmgInfo, vecOrigin, flRadius, CLASS_NONE, NULL );
#endif
  return 0;
}

//-----------------------------------------------------------------------------
// util.TraceEntityHull( tracedata, ent ) -- GMod-shape: like TraceHull but the
// hull comes from the entity's AABB, and the table's mins/maxs are ignored.
//-----------------------------------------------------------------------------
static int luasrc_UTIL_TraceEntityHull (lua_State *L) {
  if ( lua_istable( L, 1 ) ) {
    Vector vecStart, vecEnd, vecMins, vecMaxs;
    CBaseEntity *pFilter = NULL;
    int nMask = MASK_SHOT, nCollisionGroup = COLLISION_GROUP_NONE;
    CGameTrace trace;

    luasrc_TraceArgsFromTable( L, &vecStart, &vecEnd, &vecMins, &vecMaxs, &nMask, &pFilter, &nCollisionGroup );

    CBaseEntity *pEnt = lua_toentity( L, 2 );
    if ( !pEnt ) {
      luaL_argerror( L, 2, "Entity expected" );
      return 0;
    }

    Vector vecHalf = pEnt->CollisionProp()->OBBSize() * 0.5f;
    vecMins = vecHalf * -1.0f;
    vecMaxs = vecHalf;

    if ( s_bLuaTraceFilterActive )
      UTIL_TraceHull( vecStart, vecEnd, vecMins, vecMaxs, nMask, &s_LuaTraceFilter, &trace );
    else
      UTIL_TraceHull( vecStart, vecEnd, vecMins, vecMaxs, nMask, pEnt, nCollisionGroup, &trace );

    s_LuaTraceFilter.Release();
    s_bLuaTraceFilterActive = false;
    lua_pushtrace( L, trace );
    return 1;
  }

  // Source-shape passthrough: hull supplied by the caller.
  UTIL_TraceHull( luaL_checkvector( L, 1 ), luaL_checkvector( L, 2 ), luaL_checkvector( L, 3 ),
                  luaL_checkvector( L, 4 ), luaL_checkint( L, 5 ), lua_toentity( L, 6 ),
                  luaL_checkint( L, 7 ), &luaL_checktrace( L, 8 ) );
  return 0;
}

//-----------------------------------------------------------------------------
// util.FilterText -- no Steam text filter on this fork, so the input passes
// through unchanged (GMod filters only specific blocked phrases anyway).
//-----------------------------------------------------------------------------
static int luasrc_util_FilterText (lua_State *L) {
  const char *pIn = luaL_checkstring( L, 1 );
  lua_pushstring( L, pIn );
  return 1;
}

//-----------------------------------------------------------------------------
// Menu-path helpers (they work in the game realms too; GMod only names them
// "_Menu").  RelativePathToGMA_Menu has no equivalent here (no workshop tree).
//-----------------------------------------------------------------------------
static int luasrc_util_RelativePathToFull (lua_State *L) {
  const char *pPath = luaL_checkstring( L, 1 );
  const char *pMount = luaL_optstring( L, 2, "MOD" );

  char szFull[ MAX_PATH ];
  const char *pResult = HL2SB_UtilFS()->RelativePathToFullPath( pPath, pMount, szFull, sizeof( szFull ) );
  if ( !pResult || !pResult[ 0 ] ) {
    lua_pushnil( L );
    return 1;
  }
  lua_pushstring( L, pResult );
  return 1;
}

static int luasrc_util_FullPathToRelative (lua_State *L) {
  const char *pFull = luaL_checkstring( L, 1 );

  char szRel[ MAX_PATH ];
  if ( !HL2SB_UtilFS()->FullPathToRelativePath( pFull, szRel, sizeof( szRel ) ) || !szRel[ 0 ] ) {
    lua_pushnil( L );
    return 1;
  }
  lua_pushstring( L, szRel );
  return 1;
}
static const luaL_Reg util_funcs[] = {
  // HL2SB GMod compat (2026-09-25): engine-side JSON (see the block above).
  {"TableToJSON",  luasrc_UTIL_TableToJSON},
  {"JSONToTable",  luasrc_UTIL_JSONToTable},
  // {"UTIL_VecToYaw",  luasrc_UTIL_VecToYaw},
  {"VecToYaw",  luasrc_UTIL_VecToYaw},
  // {"UTIL_VecToPitch",  luasrc_UTIL_VecToPitch},
  {"VecToPitch",  luasrc_UTIL_VecToPitch},
  // {"UTIL_YawToVector",  luasrc_UTIL_YawToVector},
  {"YawToVector",  luasrc_UTIL_YawToVector},
  {"SharedRandomFloat",  luasrc_SharedRandomFloat},
  {"SharedRandomInt",  luasrc_SharedRandomInt},
  {"SharedRandomVector",  luasrc_SharedRandomVector},
  {"SharedRandomAngle",  luasrc_SharedRandomAngle},
  // {"UTIL_TraceLine",  luasrc_UTIL_TraceLine},
  {"TraceLine",  luasrc_UTIL_TraceLine},
  // HL2SB GMod compat: util.IsInWorld( position ) (see the definition above).
  {"IsInWorld",  luasrc_UTIL_IsInWorld},
  // {"UTIL_TraceHull",  luasrc_UTIL_TraceHull},
  {"TraceHull",  luasrc_UTIL_TraceHull},
  // {"UTIL_TraceEntity",  luasrc_UTIL_TraceEntity},
  {"TraceEntity",  luasrc_UTIL_TraceEntity},
  // {"UTIL_EntityHasMatchingRootParent",  luasrc_UTIL_EntityHasMatchingRootParent},
  {"EntityHasMatchingRootParent",  luasrc_UTIL_EntityHasMatchingRootParent},
  // {"UTIL_PointContents",  luasrc_UTIL_PointContents},
  {"PointContents",  luasrc_UTIL_PointContents},
  // {"UTIL_TraceModel",  luasrc_UTIL_TraceModel},
  {"TraceModel",  luasrc_UTIL_TraceModel},
  // {"UTIL_ParticleTracer",  luasrc_UTIL_ParticleTracer},
  {"ParticleTracer",  luasrc_UTIL_ParticleTracer},
  {"ParticleTracerEx",  luasrc_UTIL_ParticleTracerEx},
  // {"UTIL_Tracer",  luasrc_UTIL_Tracer},
  {"Tracer",  luasrc_UTIL_Tracer},
  // {"UTIL_IsLowViolence",  luasrc_UTIL_IsLowViolence},
  {"IsLowViolence",  luasrc_UTIL_IsLowViolence},
  // {"UTIL_ShouldShowBlood",  luasrc_UTIL_ShouldShowBlood},
  {"ShouldShowBlood",  luasrc_UTIL_ShouldShowBlood},
  // {"UTIL_BloodDrips",  luasrc_UTIL_BloodDrips},
  {"BloodDrips",  luasrc_UTIL_BloodDrips},
  // {"UTIL_BloodImpact",  luasrc_UTIL_BloodImpact},
  {"BloodImpact",  luasrc_UTIL_BloodImpact},
  // {"UTIL_BloodDecalTrace",  luasrc_UTIL_BloodDecalTrace},
  {"BloodDecalTrace",  luasrc_UTIL_BloodDecalTrace},
  // {"UTIL_DecalTrace",  luasrc_UTIL_DecalTrace},
  {"DecalTrace",  luasrc_UTIL_DecalTrace},
  {"Decal",  luasrc_UTIL_Decal},
  // {"UTIL_IsSpaceEmpty",  luasrc_UTIL_IsSpaceEmpty},
  {"IsSpaceEmpty",  luasrc_UTIL_IsSpaceEmpty},
  // {"UTIL_PlayerByIndex",  luasrc_UTIL_PlayerByIndex},
  {"PlayerByIndex",  luasrc_UTIL_PlayerByIndex},
  // HL2SB GMod SWEP compat
  {"PrecacheSound",  luasrc_util_PrecacheSound},
  {"PrecacheModel",  luasrc_util_PrecacheModel},
  {"GetModelInfo",   luasrc_util_GetModelInfo},
  // HL2SB GMod effect compat (lua/effects/*.lua, sprite trails, radius damage)
  {"Effect",  luasrc_UTIL_Effect},
  {"SpriteTrail",  luasrc_UTIL_SpriteTrail},
  {"BlastDamage",  luasrc_UTIL_BlastDamage},
  {"ScreenShake",  luasrc_UTIL_ScreenShake},
  // HL2SB: GMod util, second batch (2026-09-25) -- codecs, SteamIDs,
  // geometry, surface props, model membership, damage, traces, misc.
  {"Base64Encode",           luasrc_util_Base64Encode},
  {"Base64Decode",           luasrc_util_Base64Decode},
  {"CRC",                    luasrc_util_CRC},
  {"MD5",                    luasrc_util_MD5},
  {"SHA1",                   luasrc_util_SHA1},
  {"SHA256",                 luasrc_util_SHA256},
  {"Compress",               luasrc_util_Compress},
  {"Decompress",             luasrc_util_Decompress},
  {"SteamIDTo64",            luasrc_util_SteamIDTo64},
  {"SteamIDFrom64",          luasrc_util_SteamIDFrom64},
  {"SharedRandom",           luasrc_util_SharedRandom},
  {"AimVector",              luasrc_util_AimVector},
  {"DistanceToLine",         luasrc_util_DistanceToLine},
  {"IntersectRayWithPlane",  luasrc_util_IntersectRayWithPlane},
  {"IntersectRayWithOBB",    luasrc_util_IntersectRayWithOBB},
  {"IntersectRayWithSphere", luasrc_util_IntersectRayWithSphere},
  {"IntersectRayWithTriangle", luasrc_util_IntersectRayWithTriangle},
  {"IsBoxIntersectingBox",   luasrc_util_IsBoxIntersectingBox},
  {"IsBoxIntersectingSphere", luasrc_util_IsBoxIntersectingSphere},
  {"IsSphereIntersectingSphere", luasrc_util_IsSphereIntersectingSphere},
  {"IsOBBIntersectingOBB",   luasrc_util_IsOBBIntersectingOBB},
  {"IsPointInCone",          luasrc_util_IsPointInCone},
  {"IsRayIntersectingRay",   luasrc_util_IsRayIntersectingRay},
  {"IsSphereIntersectingCone", luasrc_util_IsSphereIntersectingCone},
  {"IsModelLoaded",          luasrc_util_IsModelLoaded},
  {"IsValidModel",           luasrc_util_IsValidModel},
  {"IsValidProp",            luasrc_util_IsValidProp},
  {"IsValidRagdoll",         luasrc_util_IsValidRagdoll},
  {"GetSurfaceIndex",        luasrc_util_GetSurfaceIndex},
  {"GetSurfacePropName",     luasrc_util_GetSurfacePropName},
  {"GetSurfaceData",         luasrc_util_GetSurfaceData},
  {"GetActivityIDByName",    luasrc_util_GetActivityIDByName},
  {"GetActivityNameByID",    luasrc_util_GetActivityNameByID},
  {"GetAnimEventIDByName",   luasrc_util_GetAnimEventIDByName},
  {"GetAnimEventNameByID",   luasrc_util_GetAnimEventNameByID},
  {"BlastDamageInfo",        luasrc_UTIL_BlastDamageInfo},
  {"TraceEntityHull",        luasrc_UTIL_TraceEntityHull},
  {"FilterText",             luasrc_util_FilterText},
  {"RelativePathToFull_Menu", luasrc_util_RelativePathToFull},
  {"FullPathToRelative_Menu", luasrc_util_FullPathToRelative},
  {NULL, NULL}
};


/*
** HL2SB: GMod's SysTime() as an ENGINE global, on BOTH realms.
**
** GMod's SysTime() is a high precision monotonic clock in seconds.  HL2SB only
** had the client-side Lua alias in lua/includes/extensions/gmod_globals.lua
** (SysTime = SysTime or RealTime), so on the SERVER SysTime was nil -- which
** silently disabled the undo de-duplication in lua/includes/modules/undo.lua
** ("fail open" then let the twice-dispatched undo run twice, deleting entities
** the first pass had already removed, and crash in server.dll with an execute
** access violation).
**
** Plat_FloatTime() is exactly that clock and exists on both realms.
*/
static int luasrc_SysTime (lua_State *L) {
  lua_pushnumber(L, Plat_FloatTime());
  return 1;
}

/*
** HL2SB GMod compat: CreateSound( entity, sound ) -> IGModAudioChannel.
**
** GMod returns a BASS audio stream.  This fork has no BASS and no AudioChannel
** library at all (see the note in lsrcinit.cpp), so the addon's music --
**
**     self.LoopSound = CreateSound( self.Owner, Sound( "weapons/nyan/nyan_loop.wav" ) )
**     if ( self.LoopSound ) then self.LoopSound:Play() end
**     ... self.LoopSound:ChangeVolume( 0, 0.1 ) ...
**
** -- was "attempt to call a nil value (global 'CreateSound')" on the first shot.
**
** The first cut of this binding was backed by EmitSound(): a fire-and-forget
** sound that can be re-emitted with SND_CHANGE_VOL / SND_STOP but has no engine
** object behind it.  That is why the addon's loop could never be stopped or
** cross-faded -- and the log shows exactly what it did instead:
**
**     Direct precache of weapons/nyan/nyan_loop.wav
**     Late precache of weapons/nyan/nyan_loop.wav
**
** (one EmitSound per press, no persistent voice).
**
** The channel is now a real voice: a CSoundPatch, created and driven through
** CSoundEnvelopeController.  That is the engine's own looping-sound object (the
** one C_BaseEntity::EmitSound( filter, entindex, EmitSound_t& ) and every
** engine ambient loop is built on), so:
**
**     Play()                  -> controller.Play( patch, volume, pitch )
**     ChangeVolume( v, t )    -> controller.SoundChangeVolume( patch, v, t )
**                                ... a real ramp, i.e. the cross-fade
**     Stop()/Shutdown         -> controller.SoundDestroy( patch )
**
** The channel stays an ordinary Lua table (the addon stores it on the SWEP and
** only ever calls methods on it), with the CSoundPatch kept in a light-userdata
** field.  Lifetime: the patch is created lazily on the first Play(), destroyed
** by Stop()/Pause()/holster, and forgotten if the owning entity goes away --
** the engine's controller drops a patch whose entity is gone, so touching the
** pointer afterwards would be a use-after-free.
*/
#define HL2SB_CHANNEL_FIELD_ENT      "__hl2sb_ent"
#define HL2SB_CHANNEL_FIELD_SOUND    "__hl2sb_sound"
#define HL2SB_CHANNEL_FIELD_VOLUME   "__hl2sb_volume"
#define HL2SB_CHANNEL_FIELD_CHANNEL  "__hl2sb_channel"
#define HL2SB_CHANNEL_FIELD_PLAYING  "__hl2sb_playing"
#define HL2SB_CHANNEL_FIELD_PATCH    "__hl2sb_patch"
#define HL2SB_CHANNEL_FIELD_PAUSED   "__hl2sb_paused"

static CBaseEntity *HL2SB_ChannelEntity( lua_State *L, int nIndex ) {
  lua_getfield( L, nIndex, HL2SB_CHANNEL_FIELD_ENT );
  CBaseEntity *pEntity = lua_toentity( L, -1 );
  lua_pop( L, 1 );
  return pEntity;
}

static CSoundPatch *HL2SB_ChannelPatch( lua_State *L, int nIndex ) {
  lua_getfield( L, nIndex, HL2SB_CHANNEL_FIELD_PATCH );
  CSoundPatch *pPatch = (CSoundPatch *)lua_touserdata( L, -1 );
  lua_pop( L, 1 );
  return pPatch;
}

static void HL2SB_ChannelSetPatch( lua_State *L, int nIndex, CSoundPatch *pPatch ) {
  if ( pPatch != NULL )
    lua_pushlightuserdata( L, pPatch );
  else
    lua_pushnil( L );
  lua_setfield( L, nIndex, HL2SB_CHANNEL_FIELD_PATCH );
}

static bool HL2SB_ChannelBool( lua_State *L, int nIndex, const char *pszField ) {
  lua_getfield( L, nIndex, pszField );
  const bool bValue = lua_toboolean( L, -1 ) != 0;
  lua_pop( L, 1 );
  return bValue;
}

static void HL2SB_ChannelSetBool( lua_State *L, int nIndex, const char *pszField, bool bValue ) {
  lua_pushboolean( L, bValue );
  lua_setfield( L, nIndex, pszField );
}

static float HL2SB_ChannelVolume( lua_State *L, int nIndex ) {
  lua_getfield( L, nIndex, HL2SB_CHANNEL_FIELD_VOLUME );
  const float flVolume = lua_isnumber( L, -1 ) ? (float)lua_tonumber( L, -1 ) : 1.0f;
  lua_pop( L, 1 );
  return flVolume;
}

static void HL2SB_ChannelSetVolume( lua_State *L, int nIndex, float flVolume ) {
  lua_pushnumber( L, flVolume );
  lua_setfield( L, nIndex, HL2SB_CHANNEL_FIELD_VOLUME );
}

static int HL2SB_ChannelAudioChannel( lua_State *L, int nIndex ) {
  lua_getfield( L, nIndex, HL2SB_CHANNEL_FIELD_CHANNEL );
  const int nChannel = lua_isnumber( L, -1 ) ? (int)lua_tointeger( L, -1 ) : CHAN_STATIC;
  lua_pop( L, 1 );
  return nChannel;
}

/*
** The engine's own looping voice for this channel, created on demand.  Returns
** NULL when the owning entity or the sound name is gone -- every caller treats
** that as "nothing to hear".
*/
static CSoundPatch *HL2SB_ChannelEnsurePatch( lua_State *L, int nIndex ) {
  // The entity is checked FIRST: the engine's controller drops a CSoundPatch
  // whose entity is gone (CSoundPatch::Update -> "Removing CSoundPatch with NULL
  // EHandle"), so a stored pointer can already be freed and must be forgotten
  // without touching it.
  CBaseEntity *pEntity = HL2SB_ChannelEntity( L, nIndex );
  if ( pEntity == NULL ) {
    // The risk branch the crash hunt cares about: the owner entity died (or the
    // weapon was dropped) while this channel still owned a live CSoundPatch.
    // CSoundControllerImp::SystemUpdate() removes such a patch from its update
    // list WITHOUT deleting it (soundenvelope.cpp:500-513, 920-928), so the
    // pointer is stale-but-allocated; touching it would be a latent
    // use-after-free the moment anything else frees or reuses that block.  Drop
    // it, never dereference it, and say so once.
    if ( HL2SB_ChannelPatch( L, nIndex ) != NULL ) {
      HL2SB_WarnOnce( "channel-owner-gone",
        "CreateSound channel: owner entity went away with a live CSoundPatch; the pointer is dropped unused (the engine leaves such patches allocated)" );
    }
    HL2SB_ChannelSetPatch( L, nIndex, NULL );
    HL2SB_ChannelSetBool( L, nIndex, HL2SB_CHANNEL_FIELD_PLAYING, false );
    return NULL;
  }

  CSoundPatch *pPatch = HL2SB_ChannelPatch( L, nIndex );
  if ( pPatch != NULL )
    return pPatch;

  lua_getfield( L, nIndex, HL2SB_CHANNEL_FIELD_SOUND );
  const char *pszSound = lua_isstring( L, -1 ) ? lua_tostring( L, -1 ) : NULL;
  lua_pop( L, 1 );

  if ( pszSound == NULL || pszSound[0] == '\0' )
    return NULL;

  // The server refuses to start a wave SV_StartSound has not been told about
  // (the log is full of "SV_StartSound: weapons/nyan/nya2.wav not precached"),
  // so a raw wave name has to be registered before the patch can start.  This
  // is the same call EmitSound makes, hence the same once-per-channel
  // "Direct precache of ..." line the first cut produced.
#ifndef CLIENT_DLL
  // HL2SB: same "ask once per name" cache as util.PrecacheSound -- the addon
  // creates this channel on every deploy and every first shot, and each create
  // used to re-run the engine's precache query.
  if ( !enginesound->IsSoundPrecached( pszSound ) && HL2SB_PrecacheOnce( pszSound ) )
    CBaseEntity::PrecacheSound( pszSound );
#endif

  CPASAttenuationFilter soundFilter( pEntity, pszSound );
#ifdef CLIENT_DLL
  soundFilter.UsePredictionRules();
#endif

  pPatch = CSoundEnvelopeController::GetController().SoundCreate(
             soundFilter, pEntity->entindex(), HL2SB_ChannelAudioChannel( L, nIndex ),
             pszSound, SNDLVL_NORM );

  if ( pPatch != NULL )
    HL2SB_ChannelSetPatch( L, nIndex, pPatch );

  return pPatch;
}

/*
** Stops the voice and forgets it.  Destroying the patch is what actually stops
** the loop (the first cut's SND_STOP re-emit did not), so the next Play()
** creates a fresh one.  When the entity is gone the engine has already taken the
** patch away and it must NOT be touched again.
*/
static void HL2SB_ChannelShutdown( lua_State *L, int nIndex ) {
  CSoundPatch *pPatch = HL2SB_ChannelPatch( L, nIndex );

  if ( pPatch != NULL ) {
    if ( HL2SB_ChannelEntity( L, nIndex ) != NULL ) {
      CSoundEnvelopeController::GetController().SoundDestroy( pPatch );
    } else {
      // Never touch a patch whose owner is gone (see HL2SB_ChannelEnsurePatch):
      // the controller has already dropped it, and SoundDestroy() would be the
      // use-after-free.  Forget it instead -- the engine's own comment
      // (soundenvelope.cpp:511) admits these leak.
      HL2SB_WarnOnce( "channel-shutdown-owner-gone",
        "CreateSound channel Stop(): owner entity already gone; the CSoundPatch handle was dropped instead of destroyed" );
    }

    HL2SB_ChannelSetPatch( L, nIndex, NULL );
  }

  HL2SB_ChannelSetBool( L, nIndex, HL2SB_CHANNEL_FIELD_PLAYING, false );
}

static int luasrc_Channel_Play (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );

  CSoundPatch *pPatch = HL2SB_ChannelEnsurePatch( L, 1 );
  if ( pPatch == NULL ) {
    lua_pushboolean( L, false );
    return 1;
  }

  CSoundEnvelopeController::GetController().Play( pPatch, HL2SB_ChannelVolume( L, 1 ), PITCH_NORM );
  HL2SB_ChannelSetBool( L, 1, HL2SB_CHANNEL_FIELD_PLAYING, true );
  HL2SB_ChannelSetBool( L, 1, HL2SB_CHANNEL_FIELD_PAUSED, false );

  lua_pushboolean( L, true );
  return 1;
}

/*
** HL2SB GMod compat: CSoundPatch:PlayEx( volume, pitch ) -- GMod's one-shot
** "play at this volume and pitch" (wiki).  hl1sweps' tau cannon charges its
** loop with PlayEx( 0.7, 110 ).  The channel keeps the volume for later
** ChangeVolume ramps; the pitch goes straight at the envelope controller so
** the first frame already sounds charged instead of snapping afterwards.
*/
static int luasrc_Channel_PlayEx (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  const float flVolume = (float)luaL_optnumber( L, 2, 1.0f );
  const float flPitch = (float)luaL_optnumber( L, 3, (double)PITCH_NORM );

  CSoundPatch *pPatch = HL2SB_ChannelEnsurePatch( L, 1 );
  if ( pPatch == NULL ) {
    lua_pushboolean( L, false );
    return 1;
  }

  HL2SB_ChannelSetVolume( L, 1, flVolume );
  CSoundEnvelopeController::GetController().Play( pPatch, flVolume, flPitch );
  HL2SB_ChannelSetBool( L, 1, HL2SB_CHANNEL_FIELD_PLAYING, true );
  HL2SB_ChannelSetBool( L, 1, HL2SB_CHANNEL_FIELD_PAUSED, false );

  lua_pushboolean( L, true );
  return 1;
}

static int luasrc_Channel_Stop (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  HL2SB_ChannelShutdown( L, 1 );
  HL2SB_ChannelSetBool( L, 1, HL2SB_CHANNEL_FIELD_PAUSED, false );
  return 0;
}

static int luasrc_Channel_Pause (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  HL2SB_ChannelShutdown( L, 1 );
  HL2SB_ChannelSetBool( L, 1, HL2SB_CHANNEL_FIELD_PAUSED, true );
  return 0;
}

static int luasrc_Channel_SetVolume (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  const float flVolume = (float)luaL_checknumber( L, 2 );
  HL2SB_ChannelSetVolume( L, 1, flVolume );

  CSoundPatch *pPatch = HL2SB_ChannelPatch( L, 1 );
  if ( pPatch != NULL && HL2SB_ChannelEntity( L, 1 ) != NULL )
    CSoundEnvelopeController::GetController().SoundChangeVolume( pPatch, flVolume, 0.0f );

  return 0;
}

/*
** GMod's cross-fade: ChangeVolume( volume, time ) ramps over `time` seconds.
** CSoundPatch::ChangeVolume is exactly that ramp, so the nyan gun's
** "loop up on fire, loop down on release" finally works.
*/
static int luasrc_Channel_ChangeVolume (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  const float flVolume = (float)luaL_checknumber( L, 2 );
  const float flDeltaTime = (float)luaL_optnumber( L, 3, 0.0f );
  HL2SB_ChannelSetVolume( L, 1, flVolume );

  CSoundPatch *pPatch = HL2SB_ChannelPatch( L, 1 );
  if ( pPatch != NULL && HL2SB_ChannelEntity( L, 1 ) != NULL )
    CSoundEnvelopeController::GetController().SoundChangeVolume( pPatch, flVolume, flDeltaTime );

  return 0;
}

static int luasrc_Channel_SetPitch (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  const float flPitch = (float)luaL_checknumber( L, 2 );

  CSoundPatch *pPatch = HL2SB_ChannelPatch( L, 1 );
  if ( pPatch != NULL && HL2SB_ChannelEntity( L, 1 ) != NULL )
    CSoundEnvelopeController::GetController().SoundChangePitch( pPatch, flPitch, 0.0f );

  return 0;
}

static int luasrc_Channel_IsPlaying (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );

  if ( HL2SB_ChannelEntity( L, 1 ) == NULL ) {
    lua_pushboolean( L, false );
    return 1;
  }

  lua_pushboolean( L, HL2SB_ChannelBool( L, 1, HL2SB_CHANNEL_FIELD_PLAYING ) );
  return 1;
}

// GMod CSoundPatch:ChangePitch( pitch, time )
static int luasrc_Channel_ChangePitch (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  const float flPitch = (float)luaL_checknumber( L, 2 );
  const float flDeltaTime = (float)luaL_optnumber( L, 3, 0.0f );

  CSoundPatch *pPatch = HL2SB_ChannelPatch( L, 1 );
  if ( pPatch != NULL && HL2SB_ChannelEntity( L, 1 ) != NULL )
    CSoundEnvelopeController::GetController().SoundChangePitch( pPatch, flPitch, flDeltaTime );

  return 0;
}

// GMod CSoundPatch:FadeOut( seconds ) -- ramp to 0 then Stop.
static int luasrc_Channel_FadeOut (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  const float flSeconds = (float)luaL_optnumber( L, 2, 0.5f );

  CSoundPatch *pPatch = HL2SB_ChannelPatch( L, 1 );
  if ( pPatch != NULL && HL2SB_ChannelEntity( L, 1 ) != NULL ) {
    float flDur = flSeconds;
    if ( flDur < 0.01f ) flDur = 0.01f;
    CSoundEnvelopeController::GetController().SoundChangeVolume( pPatch, 0.0f, flDur );
  }

  HL2SB_ChannelSetVolume( L, 1, 0.0f );
  return 0;
}

// GMod CSoundPatch:IsFinished() -- not playing and not paused = finished.
static int luasrc_Channel_IsFinished (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  bool bPlaying = HL2SB_ChannelBool( L, 1, HL2SB_CHANNEL_FIELD_PLAYING );
  bool bPaused  = HL2SB_ChannelBool( L, 1, HL2SB_CHANNEL_FIELD_PAUSED );
  bool bValid   = ( HL2SB_ChannelEntity( L, 1 ) != NULL );
  lua_pushboolean( L, bValid && !bPlaying && !bPaused );
  return 1;
}

static int luasrc_Channel_IsPaused (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  lua_pushboolean( L, HL2SB_ChannelBool( L, 1, HL2SB_CHANNEL_FIELD_PAUSED ) );
  return 1;
}

static int luasrc_Channel_IsValid (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  lua_pushboolean( L, HL2SB_ChannelEntity( L, 1 ) != NULL );
  return 1;
}

static int luasrc_Channel_GetVolume (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  lua_pushnumber( L, HL2SB_ChannelVolume( L, 1 ) );
  return 1;
}

static int luasrc_Channel_GetPitch (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );

  CSoundPatch *pPatch = HL2SB_ChannelPatch( L, 1 );
  if ( pPatch != NULL && HL2SB_ChannelEntity( L, 1 ) != NULL ) {
    lua_pushnumber( L, CSoundEnvelopeController::GetController().SoundGetPitch( pPatch ) );
    return 1;
  }

  lua_pushnumber( L, PITCH_NORM );
  return 1;
}

static int luasrc_Channel_NoOp (lua_State *L) {
  return 0;
}

static int luasrc_Channel_Zero (lua_State *L) {
  lua_pushnumber( L, 0 );
  return 1;
}

static int luasrc_Channel_False (lua_State *L) {
  lua_pushboolean( L, false );
  return 1;
}

static int luasrc_CreateSound (lua_State *L) {
  const char *pszSound = luaL_checkstring( L, 2 );

  /* HL2SB (2026-10-07): GMod's CreateSound NEVER answers nil -- a channel it
  ** cannot back (owner gone, file missing) comes back as an invalid channel
  ** whose methods fail softly, and scripts only ever guard with
  ** "if not self.ChargeSound then ... end".  The old nil return made every
  ** copy that could not create the voice yet store nil, and the first
  ** ChargeSound:Stop()/ChangePitch() on that copy died with "attempt to index
  ** a nil value (field 'ChargeSound')" on every think (hl1sweps tau cannon).
  ** The entity/sound are stored anyway: the patch is still created lazily by
  ** Play/PlayEx once the owner answers, and until then every method takes the
  ** same no-patch soft-fail path. */
  lua_newtable( L );

  lua_pushvalue( L, 1 );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_ENT );

  lua_pushstring( L, pszSound );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_SOUND );

  lua_pushnumber( L, 1.0f );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_VOLUME );

  lua_pushinteger( L, CHAN_STATIC );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_CHANNEL );

  lua_pushboolean( L, false );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_PLAYING );

  lua_pushboolean( L, false );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_PAUSED );

  lua_pushnil( L );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_PATCH );

  struct { const char *pszName; lua_CFunction pfn; } methods[] = {
    { "Play",         luasrc_Channel_Play },
    { "PlayEx",       luasrc_Channel_PlayEx },  /* HL2SB: GMod's PlayEx( volume, pitch ) */
    { "Stop",         luasrc_Channel_Stop },
    { "Pause",        luasrc_Channel_Pause },
    { "SetVolume",    luasrc_Channel_SetVolume },
    { "ChangeVolume", luasrc_Channel_ChangeVolume },
    { "GetVolume",    luasrc_Channel_GetVolume },
    { "SetPitch",     luasrc_Channel_SetPitch },
    { "GetPitch",     luasrc_Channel_GetPitch },
    { "IsPlaying",    luasrc_Channel_IsPlaying },
    { "IsPaused",     luasrc_Channel_IsPaused },
    { "IsFinished",   luasrc_Channel_IsFinished },
    { "IsValid",      luasrc_Channel_IsValid },
    { "FadeOut",      luasrc_Channel_FadeOut },
    { "ChangePitch",  luasrc_Channel_ChangePitch },
    { "Is3D",         luasrc_Channel_False },
    { "GetTime",      luasrc_Channel_Zero },
    { "GetState",     luasrc_Channel_Zero },
    { "GetPlaybackRate", luasrc_Channel_Zero },
    /* Accepted and ignored: the seeking / spatialisation / looping controls a
    ** GMod script may poke at.  A CSoundPatch loops or not by its own wave, and
    ** nothing in the addon path uses them. */
    { "SetTime",      luasrc_Channel_NoOp },
    { "SetPlaybackRate", luasrc_Channel_NoOp },
    { "EnableLooping", luasrc_Channel_NoOp },
    { "Set3DPosition", luasrc_Channel_NoOp },
    { "SetPos",       luasrc_Channel_NoOp },
  };

  for ( int i = 0; i < (int)( sizeof( methods ) / sizeof( methods[0] ) ); ++i ) {
    lua_pushcfunction( L, methods[i].pfn );
    lua_setfield( L, -2, methods[i].pszName );
  }

  return 1;
}

LUALIB_API int luaopen_UTIL_shared (lua_State *L) {
  // luaL_register(L, "_G", util_funcs);
  luaL_register(L, "util", util_funcs);

  lua_pushcfunction(L, luasrc_SysTime);
  lua_setglobal(L, "SysTime");

  lua_pushcfunction(L, luasrc_CreateSound);
  lua_setglobal(L, "CreateSound");

  return 1;
}

