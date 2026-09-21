//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//
#define lc_baseentity_cpp

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"
#include "model_types.h"	// HL2SB: STUDIO_RENDER, the default of Entity:DrawModel()
#include "c_baseanimating.h"	// HL2SB: non-dispatching DrawModel path, see CBaseEntity_DrawModel
#include "particles_new.h"	// HL2SB: Entity:CreateParticleEffect (CNewParticleEffect)
#include "particle_parse.h"	// HL2SB: ParticleAttachment_t
#include "lnewparticle.h"	// HL2SB: HL2SB_PushNewParticleEffect

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


static int CBaseEntity_SpawnClientEntity (lua_State *L) {
  luaL_checkentity(L, 1)->SpawnClientEntity();
  return 0;
}

static int CBaseEntity_Interp_HierarchyUpdateInterpolationAmounts (lua_State *L) {
  luaL_checkentity(L, 1)->Interp_HierarchyUpdateInterpolationAmounts();
  return 0;
}

static int CBaseEntity_Init (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->Init(luaL_checkint(L, 2), luaL_checkint(L, 3)));
  return 1;
}

static int CBaseEntity_Term (lua_State *L) {
  luaL_checkentity(L, 1)->Term();
  return 0;
}

static int CBaseEntity_EnableInToolView (lua_State *L) {
  luaL_checkentity(L, 1)->EnableInToolView(luaL_checkboolean(L, 2));
  return 0;
}

static int CBaseEntity_IsEnabledInToolView (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->IsEnabledInToolView());
  return 1;
}

static int CBaseEntity_SetToolRecording (lua_State *L) {
  luaL_checkentity(L, 1)->SetToolRecording(luaL_checkboolean(L, 2));
  return 0;
}

static int CBaseEntity_IsToolRecording (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->IsToolRecording());
  return 1;
}

static int CBaseEntity_HasRecordedThisFrame (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->HasRecordedThisFrame());
  return 1;
}

static int CBaseEntity_RecordToolMessage (lua_State *L) {
  luaL_checkentity(L, 1)->RecordToolMessage();
  return 0;
}

static int CBaseEntity_DontRecordInTools (lua_State *L) {
  luaL_checkentity(L, 1)->DontRecordInTools();
  return 0;
}

static int CBaseEntity_ShouldRecordInTools (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->ShouldRecordInTools());
  return 1;
}

static int CBaseEntity_Release (lua_State *L) {
  luaL_checkentity(L, 1)->Release();
  return 0;
}

static int CBaseEntity_GetRenderOrigin (lua_State *L) {
  Vector v = luaL_checkentity(L, 1)->GetRenderOrigin();
  lua_pushvector(L, v);
  return 1;
}

static int CBaseEntity_GetRenderAngles (lua_State *L) {
  QAngle v = luaL_checkentity(L, 1)->GetRenderAngles();
  lua_pushangle(L, v);
  return 1;
}

static int CBaseEntity_GetObserverCamOrigin (lua_State *L) {
  Vector v = luaL_checkentity(L, 1)->GetObserverCamOrigin();
  lua_pushvector(L, v);
  return 1;
}

static int CBaseEntity_IsTwoPass (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->IsTwoPass());
  return 1;
}

static int CBaseEntity_UsesPowerOfTwoFrameBufferTexture (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->UsesPowerOfTwoFrameBufferTexture());
  return 1;
}

static int CBaseEntity_UsesFullFrameBufferTexture (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->UsesFullFrameBufferTexture());
  return 1;
}

static int CBaseEntity_DrawModel (lua_State *L) {
  // HL2SB GMod compat: `flags` is optional and defaults to STUDIO_RENDER (wiki,
  // Entity:DrawModel) - see the same fix on C_BaseAnimating.
  //
  // HL2SB: like CBaseAnimating_DrawModel, this must NOT go through the virtual
  // DrawModel() -- on a C_BaseScripted that re-enters ENT:Draw() and the model
  // is never actually drawn (the cod_c4 thrown entity rendered nothing while
  // its ENT:Draw ran every frame).  Route through InternalDrawModel, which
  // draws the model without the script dispatch.
  C_BaseEntity *pEntity = luaL_checkentity(L, 1);
  int nFlags = luaL_optint(L, 2, STUDIO_RENDER);

  int nResult = 0;
  C_BaseAnimating *pAnim = dynamic_cast<C_BaseAnimating *>(pEntity);
  if (pAnim != NULL)
    nResult = pAnim->InternalDrawModel(nFlags);
  else
    nResult = pEntity->DrawModel(nFlags);

  lua_pushinteger(L, nResult);
  return 1;
}

// HL2SB GMod compat: Entity:SetRenderBounds( mins, maxs, add ).
//
// Wiki: "Sets the render bounds for the entity", and the third argument (default
// Vector( 0, 0, 0 )) "adds this vector to maxs and subtracts this vector from mins".
// Client only, like the wiki says.  There was no such method anywhere in this fork's
// entity code, so a scripted entity that draws itself (npc_verity draws a 128x128
// sprite 64 units above its origin, with no model at all) could not widen the box
// the renderer culls against.
static int CBaseEntity_SetRenderBounds (lua_State *L) {
  C_BaseEntity *pEntity = luaL_checkentity(L, 1);
  Vector mins = luaL_checkvector(L, 2);
  Vector maxs = luaL_checkvector(L, 3);
  Vector vecZero( 0.0f, 0.0f, 0.0f );
  Vector add  = luaL_optvector(L, 4, &vecZero);

  pEntity->SetScriptedRenderBounds( mins - add, maxs + add );
  return 0;
}

// HL2SB GMod compat: Entity:SetRenderBoundsWS( mins, maxs, add ) -- world-space
// variant (wiki).  Converts to local relative to the entity origin, then reuses
// the local setter.  EFFECT:SetRenderBoundsWS already exists on effect tables;
// this is the entity method.
static int CBaseEntity_SetRenderBoundsWS (lua_State *L) {
  C_BaseEntity *pEntity = luaL_checkentity(L, 1);
  Vector mins = luaL_checkvector(L, 2);
  Vector maxs = luaL_checkvector(L, 3);
  Vector vecZero( 0.0f, 0.0f, 0.0f );
  Vector add  = luaL_optvector(L, 4, &vecZero);

  const Vector origin = pEntity->GetAbsOrigin();
  pEntity->SetScriptedRenderBounds( ( mins - add ) - origin, ( maxs + add ) - origin );
  return 0;
}

static int CBaseEntity_ComputeFxBlend (lua_State *L) {
  luaL_checkentity(L, 1)->ComputeFxBlend();
  return 0;
}

static int CBaseEntity_GetFxBlend (lua_State *L) {
  lua_pushinteger(L, luaL_checkentity(L, 1)->GetFxBlend());
  return 1;
}

static int CBaseEntity_LODTest (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->LODTest());
  return 1;
}

static int CBaseEntity_SetNextClientThink (lua_State *L) {
  luaL_checkentity(L, 1)->SetNextClientThink(luaL_checknumber(L, 2));
  return 0;
}


// HL2SB (2026-09-21): Entity:CreateParticleEffect( particle, attachment, options )
// -- GMod realm: client.  Creates a .pcf particle system owned by the entity's
// CParticleProperty and returns the CNewParticleEffect wrapper.  The options
// table (IDs 1..64) drives extra control points: each entry may carry
// attachtype (PATTACH_*, default PATTACH_ABSORIGIN), entity (the CP follows
// it) and position (a static CP position).
static int CBaseEntity_CreateParticleEffect (lua_State *L) {
  CBaseEntity *pEnt = luaL_checkentity(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
  int iAttachment = luaL_optint(L, 3, 0);

  // With an attachment index the system must follow that attachment
  // (PATTACH_POINT_FOLLOW); without one, follow the origin.
  ParticleAttachment_t iAttachType = iAttachment > 0 ? PATTACH_POINT_FOLLOW : PATTACH_ABSORIGIN_FOLLOW;

  CNewParticleEffect *pEffect = pEnt->ParticleProp()->Create( pszName, iAttachType, iAttachment );
  if ( pEffect == NULL ) {
    lua_pushnil(L);
    return 1;
  }
  if ( !pEffect->IsValid() ) {
    Warning( "[HL2SB] Entity:CreateParticleEffect('%s'): the particle system is unknown -- was it loaded with game.AddParticles and registered with PrecacheParticleSystem?\n", pszName );
  }

  if ( lua_istable(L, 4) ) {
    for ( int i = 1; i <= 64; ++i ) {
      lua_rawgeti(L, 4, i);
      if ( lua_istable(L, -1) ) {
        lua_getfield(L, -1, "entity");
        CBaseEntity *pCPEnt = lua_isnil(L, -1) ? NULL : luaL_checkentity(L, -1);
        lua_pop(L, 1);

        int iCPType = PATTACH_ABSORIGIN;   // wiki: attachtype defaults to PATTACH_ABSORIGIN
        lua_getfield(L, -1, "attachtype");
        if ( !lua_isnil(L, -1) )
          iCPType = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        if ( pCPEnt != NULL ) {
          pEnt->ParticleProp()->AddControlPoint( pEffect, i, pCPEnt, (ParticleAttachment_t)iCPType, NULL, vec3_origin );
        }

        lua_getfield(L, -1, "position");
        if ( !lua_isnil(L, -1) )
          pEffect->SetControlPoint( i, luaL_checkvector(L, -1) );
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
  }

  HL2SB_PushNewParticleEffect(L, pEffect);
  return 1;
}

// HL2SB (2026-09-21): Entity:StopParticlesInvolving( entity ) -- GMod realm:
// client.  Stops every particle system on this entity that has a control
// point attached to the given entity.
static int CBaseEntity_StopParticlesInvolving (lua_State *L) {
  CBaseEntity *pEnt = luaL_checkentity(L, 1);
  CBaseEntity *pOther = luaL_checkentity(L, 2);
  pEnt->ParticleProp()->StopParticlesInvolving( pOther );
  return 0;
}

static const luaL_Reg CBaseEntitymeta[] = {
  {"SpawnClientEntity", CBaseEntity_SpawnClientEntity},
  {"Interp_HierarchyUpdateInterpolationAmounts", CBaseEntity_Interp_HierarchyUpdateInterpolationAmounts},
  {"Init", CBaseEntity_Init},
  {"Term", CBaseEntity_Term},
  {"EnableInToolView", CBaseEntity_EnableInToolView},
  {"IsEnabledInToolView", CBaseEntity_IsEnabledInToolView},
  {"SetToolRecording", CBaseEntity_SetToolRecording},
  {"IsToolRecording", CBaseEntity_IsToolRecording},
  {"HasRecordedThisFrame", CBaseEntity_HasRecordedThisFrame},
  {"RecordToolMessage", CBaseEntity_RecordToolMessage},
  {"DontRecordInTools", CBaseEntity_DontRecordInTools},
  {"ShouldRecordInTools", CBaseEntity_ShouldRecordInTools},
  {"Release", CBaseEntity_Release},
  {"GetRenderOrigin", CBaseEntity_GetRenderOrigin},
  {"GetRenderAngles", CBaseEntity_GetRenderAngles},
  {"GetObserverCamOrigin", CBaseEntity_GetObserverCamOrigin},
  {"IsTwoPass", CBaseEntity_IsTwoPass},
  {"UsesPowerOfTwoFrameBufferTexture", CBaseEntity_UsesPowerOfTwoFrameBufferTexture},
  {"UsesFullFrameBufferTexture", CBaseEntity_UsesFullFrameBufferTexture},
  {"DrawModel", CBaseEntity_DrawModel},
  {"SetRenderBounds", CBaseEntity_SetRenderBounds},
  {"SetRenderBoundsWS", CBaseEntity_SetRenderBoundsWS},
  {"ComputeFxBlend", CBaseEntity_ComputeFxBlend},
  {"GetFxBlend", CBaseEntity_GetFxBlend},
  {"LODTest", CBaseEntity_LODTest},
  {"SetNextClientThink", CBaseEntity_SetNextClientThink},
  // HL2SB (2026-09-21): the .pcf particle surface.  StopParticles lives in the
  // SHARED entity metatable (lbaseentity_shared.cpp) -- both realms register
  // into the same table.
  {"CreateParticleEffect", CBaseEntity_CreateParticleEffect},
  {"StopParticlesInvolving", CBaseEntity_StopParticlesInvolving},
  {NULL, NULL}
};


/*
** Open CBaseEntity object
*/
LUALIB_API int luaopen_CBaseEntity (lua_State *L) {
  luaL_getmetatable(L, LUA_BASEENTITYLIBNAME);
  if (lua_isnoneornil(L, -1)) {
    lua_pop(L, 1);
    luaL_newmetatable(L, LUA_BASEENTITYLIBNAME);
  }
  luaL_register(L, NULL, CBaseEntitymeta);
  return 1;
}

