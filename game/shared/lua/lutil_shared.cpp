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
// HL2SB: CSoundEnvelopeController, which owns the engine's CSoundPatch objects.
// CSoundPatch itself is defined only inside game/shared/soundenvelope.cpp, so
// the controller interface is the only public handle on it -- which is exactly
// what the GMod audio channel below needs (see luasrc_CreateSound).
#include "soundenvelope.h"
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

  if ( s_pHL2SBWarned->Count() >= 32 )
    return;

  if ( s_pHL2SBWarned->Find( pszKey ) != s_pHL2SBWarned->InvalidIndex() )
    return;

  s_pHL2SBWarned->Insert( pszKey, 1 );

  char szBuf[ 320 ];
  va_list args;
  va_start( args, pszFormat );
  Q_vsnprintf( szBuf, sizeof( szBuf ), pszFormat, args );
  va_end( args );

  Warning( "[HL2SB] %s\n", szBuf );
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

  // HL2SB: ask once per name per session (see HL2SB_PrecacheOnce).
  if ( !HL2SB_PrecacheOnce( pszName ) )
    return 0;

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

  lua_pushboolean( L, false );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_PAUSED );

  lua_pushnil( L );
  lua_setfield( L, -2, HL2SB_CHANNEL_FIELD_PATCH );

  struct { const char *pszName; lua_CFunction pfn; } methods[] = {
    { "Play",         luasrc_Channel_Play },
    { "Stop",         luasrc_Channel_Stop },
    { "Pause",        luasrc_Channel_Pause },
    { "SetVolume",    luasrc_Channel_SetVolume },
    { "ChangeVolume", luasrc_Channel_ChangeVolume },
    { "GetVolume",    luasrc_Channel_GetVolume },
    { "SetPitch",     luasrc_Channel_SetPitch },
    { "GetPitch",     luasrc_Channel_GetPitch },
    { "IsPlaying",    luasrc_Channel_IsPlaying },
    { "IsPaused",     luasrc_Channel_IsPaused },
    { "IsValid",      luasrc_Channel_IsValid },
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

