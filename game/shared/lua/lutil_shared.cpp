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
#include "leffect_dispatch_data.h"
#include <lColor.h>

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
// Only a single entity filter is honoured (that is what the ported weapons pass);
// GMod's "filter may also be a table" spelling is not implemented yet.
//-----------------------------------------------------------------------------
static bool luasrc_TraceArgsFromTable (lua_State *L, Vector *pStart, Vector *pEnd, Vector *pMins, Vector *pMaxs,
                                        int *pMask, CBaseEntity **ppFilter, int *pCollisionGroup)
{
  *pStart = vec3_origin;
  *pEnd = vec3_origin;
  *pMins = vec3_origin;
  *pMaxs = vec3_origin;
  *pMask = MASK_SHOT;
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

  lua_getfield( L, 1, "filter" );
  *ppFilter = lua_toentity( L, -1 );        // NULL for nil, GMod's NULL sentinel or a table
  lua_pop( L, 1 );

  lua_getfield( L, 1, "collisiongroup" );
  if ( lua_isnumber( L, -1 ) ) *pCollisionGroup = (int)lua_tointeger( L, -1 );
  lua_pop( L, 1 );

  return true;
}

static int luasrc_UTIL_TraceLine (lua_State *L) {
  if ( lua_istable( L, 1 ) ) {
    Vector vecStart, vecEnd, vecMins, vecMaxs;
    CBaseEntity *pFilter = NULL;
    int nMask = MASK_SHOT, nCollisionGroup = COLLISION_GROUP_NONE;
    CGameTrace trace;

    luasrc_TraceArgsFromTable( L, &vecStart, &vecEnd, &vecMins, &vecMaxs, &nMask, &pFilter, &nCollisionGroup );

    UTIL_TraceLine( vecStart, vecEnd, nMask, pFilter, nCollisionGroup, &trace );
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

    UTIL_TraceHull( vecStart, vecEnd, vecMins, vecMaxs, nMask, pFilter, nCollisionGroup, &trace );
    lua_pushtrace( L, trace );
    return 1;
  }

  UTIL_TraceHull(luaL_checkvector(L, 1), luaL_checkvector(L, 2), luaL_checkvector(L, 3), luaL_checkvector(L, 4), luaL_checkint(L, 5), luaL_checkentity(L, 6), luaL_checkint(L, 7), &luaL_checktrace(L, 8));
  return 0;
}

static int luasrc_UTIL_TraceEntity (lua_State *L) {
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

static int luasrc_UTIL_ParticleTracer (lua_State *L) {
  UTIL_ParticleTracer(luaL_checkstring(L, 1), luaL_checkvector(L, 2), luaL_checkvector(L, 3), luaL_optint(L, 4, 0), luaL_optint(L, 5, 0), luaL_optboolean(L, 6, 0));
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
  if ( CBaseEntity::PrecacheScriptSound( pszName ) <= 0
       && !enginesound->IsSoundPrecached( pszName ) ) {
    CBaseEntity::PrecacheSound( pszName );
  }
  return 0;
}

// HL2SB GMod compat: util.PrecacheModel(mdl).  GMod scripts (gmod_camera, many
// workshop SWEPs and addons) wrap model paths in Model(...) or call
// util.PrecacheModel directly.  Precaching is a server-side operation; on the
// client the argument is validated and dropped, matching GMod's behaviour.
static int luasrc_util_PrecacheModel (lua_State *L) {
#ifndef CLIENT_DLL
  CBaseEntity::PrecacheModel(luaL_checkstring(L, 1));
#else
  luaL_checkstring(L, 1);
#endif
  return 0;
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
*/
static int luasrc_UTIL_Effect (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);
  CEffectData data = luaL_checkeffect(L, 2);
  luaL_optboolean(L, 3, false);

#ifdef CLIENT_DLL
  if ( HL2SB_CreateLuaEffect( pszName, data ) ) {
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
*/
static int luasrc_UTIL_SpriteTrail (lua_State *L) {
#ifndef CLIENT_DLL
  CBaseEntity *pEntity = lua_toentity( L, 1 );
  int iAttachment = luaL_checkint( L, 2 );
  lua_Color clr = luaL_checkcolor( L, 3 );
  bool bAdditive = luaL_checkboolean( L, 4 );
  float flStartWidth = luaL_checknumber( L, 5 );
  float flEndWidth = luaL_checknumber( L, 6 );
  float flLifetime = luaL_checknumber( L, 7 );
  float flTextureResolution = luaL_checknumber( L, 8 );
  const char *pszTexture = luaL_checkstring( L, 9 );

  Vector vecOrigin = vec3_origin;
  if ( pEntity != NULL ) {
    vecOrigin = pEntity->GetAbsOrigin();
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
*/
static int luasrc_UTIL_BlastDamage (lua_State *L) {
#ifndef CLIENT_DLL
  CBaseEntity *pInflictor = lua_toentity( L, 1 );
  CBaseEntity *pAttacker = lua_toentity( L, 2 );
  Vector vecOrigin = luaL_checkvector( L, 3 );
  float flRadius = luaL_checknumber( L, 4 );
  float flDamage = luaL_checknumber( L, 5 );

  if ( pInflictor == NULL ) {
    pInflictor = pAttacker;
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

static const luaL_Reg util_funcs[] = {
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
  // {"UTIL_IsSpaceEmpty",  luasrc_UTIL_IsSpaceEmpty},
  {"IsSpaceEmpty",  luasrc_UTIL_IsSpaceEmpty},
  // {"UTIL_PlayerByIndex",  luasrc_UTIL_PlayerByIndex},
  {"PlayerByIndex",  luasrc_UTIL_PlayerByIndex},
  // HL2SB GMod SWEP compat
  {"PrecacheSound",  luasrc_util_PrecacheSound},
  {"PrecacheModel",  luasrc_util_PrecacheModel},
  // HL2SB GMod effect compat (lua/effects/*.lua, sprite trails, radius damage)
  {"Effect",  luasrc_UTIL_Effect},
  {"SpriteTrail",  luasrc_UTIL_SpriteTrail},
  {"BlastDamage",  luasrc_UTIL_BlastDamage},
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
** The channel below is backed by the engine's own sound emitter instead: the
** object is an ordinary Lua table holding the state, and Play/Stop/ChangeVolume
** re-issue the SAME wave on the SAME channel on the SAME entity, with
** SND_CHANGE_VOL / SND_STOP.  That is how the engine itself adjusts a playing
** sound, so the loop and the beat still cross-fade the way the script intends.
*/
#define HL2SB_CHANNEL_FIELD_ENT     "__hl2sb_ent"
#define HL2SB_CHANNEL_FIELD_SOUND   "__hl2sb_sound"
#define HL2SB_CHANNEL_FIELD_VOLUME  "__hl2sb_volume"
#define HL2SB_CHANNEL_FIELD_CHANNEL "__hl2sb_channel"
#define HL2SB_CHANNEL_FIELD_PLAYING "__hl2sb_playing"

static void HL2SB_EmitChannelSound( lua_State *L, int nFlags, float flVolumeOverride )
{
  lua_getfield( L, 1, HL2SB_CHANNEL_FIELD_ENT );
  CBaseEntity *pEntity = lua_toentity( L, -1 );
  lua_pop( L, 1 );

  if ( pEntity == NULL )
    return;

  lua_getfield( L, 1, HL2SB_CHANNEL_FIELD_SOUND );
  const char *pszSound = lua_isstring( L, -1 ) ? lua_tostring( L, -1 ) : NULL;
  lua_pop( L, 1 );

  if ( pszSound == NULL || pszSound[0] == '\0' )
    return;

  lua_getfield( L, 1, HL2SB_CHANNEL_FIELD_CHANNEL );
  const int nChannel = lua_isnumber( L, -1 ) ? lua_tointeger( L, -1 ) : CHAN_STATIC;
  lua_pop( L, 1 );

  lua_getfield( L, 1, HL2SB_CHANNEL_FIELD_VOLUME );
  float flVolume = lua_isnumber( L, -1 ) ? (float)lua_tonumber( L, -1 ) : 1.0f;
  lua_pop( L, 1 );

  if ( flVolumeOverride >= 0.0f )
    flVolume = flVolumeOverride;

  EmitSound_t emit;
  emit.m_pSoundName = pszSound;
  emit.m_nChannel = nChannel;
  emit.m_flVolume = flVolume;
  emit.m_SoundLevel = SNDLVL_NORM;
  emit.m_nFlags = nFlags;
  emit.m_bWarnOnDirectWaveReference = true;

  CPASAttenuationFilter soundFilter( pEntity, pszSound );
#ifdef CLIENT_DLL
  soundFilter.UsePredictionRules();
#endif
  pEntity->EmitSound( soundFilter, pEntity->entindex(), emit );

  lua_pushnumber( L, flVolume );
  lua_setfield( L, 1, HL2SB_CHANNEL_FIELD_VOLUME );
}

static int luasrc_Channel_Play (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  HL2SB_EmitChannelSound( L, 0, -1.0f );
  lua_pushboolean( L, true );
  lua_setfield( L, 1, HL2SB_CHANNEL_FIELD_PLAYING );
  lua_pushboolean( L, true );
  return 1;
}

static int luasrc_Channel_Stop (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  HL2SB_EmitChannelSound( L, SND_STOP, -1.0f );
  lua_pushboolean( L, false );
  lua_setfield( L, 1, HL2SB_CHANNEL_FIELD_PLAYING );
  return 0;
}

static int luasrc_Channel_SetVolume (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  const float flVolume = (float)luaL_checknumber( L, 2 );
  HL2SB_EmitChannelSound( L, SND_CHANGE_VOL, flVolume );
  return 0;
}

static int luasrc_Channel_ChangeVolume (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  const float flVolume = (float)luaL_checknumber( L, 2 );
  luaL_optnumber( L, 3, 0.0f );  /* dtime: the engine's change is immediate */
  HL2SB_EmitChannelSound( L, SND_CHANGE_VOL, flVolume );
  return 0;
}

static int luasrc_Channel_IsPlaying (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  lua_getfield( L, 1, HL2SB_CHANNEL_FIELD_PLAYING );
  lua_pushboolean( L, lua_toboolean( L, -1 ) != 0 );
  lua_remove( L, -2 );
  return 1;
}

static int luasrc_Channel_IsValid (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  lua_getfield( L, 1, HL2SB_CHANNEL_FIELD_ENT );
  lua_pushboolean( L, lua_toentity( L, -1 ) != NULL );
  lua_remove( L, -2 );
  return 1;
}

static int luasrc_Channel_GetVolume (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );
  lua_getfield( L, 1, HL2SB_CHANNEL_FIELD_VOLUME );
  lua_pushnumber( L, lua_isnumber( L, -1 ) ? lua_tonumber( L, -1 ) : 1.0 );
  lua_remove( L, -2 );
  return 1;
}

static int luasrc_Channel_NoOp (lua_State *L) {
  return 0;
}

static int luasrc_Channel_Zero (lua_State *L) {
  lua_pushnumber( L, 0 );
  return 1;
}

static int luasrc_CreateSound (lua_State *L) {
  CBaseEntity *pEntity = lua_toentity( L, 1 );
  const char *pszSound = luaL_checkstring( L, 2 );

  if ( pEntity == NULL || pszSound[0] == '\0' ) {
    lua_pushnil( L );
    return 1;
  }

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

  struct { const char *pszName; lua_CFunction pfn; } methods[] = {
    { "Play",         luasrc_Channel_Play },
    { "Stop",         luasrc_Channel_Stop },
    { "Pause",        luasrc_Channel_Stop },
    { "SetVolume",    luasrc_Channel_SetVolume },
    { "ChangeVolume", luasrc_Channel_ChangeVolume },
    { "GetVolume",    luasrc_Channel_GetVolume },
    { "IsPlaying",    luasrc_Channel_IsPlaying },
    { "IsPaused",     luasrc_Channel_IsPlaying },
    { "IsValid",      luasrc_Channel_IsValid },
    { "GetTime",      luasrc_Channel_Zero },
    { "GetState",     luasrc_Channel_Zero },
    /* Accepted and ignored: the spatialisation, pitch and seeking controls a
    ** GMod script may poke at.  Nothing in the addon path uses them. */
    { "SetTime",      luasrc_Channel_NoOp },
    { "SetPitch",     luasrc_Channel_NoOp },
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

