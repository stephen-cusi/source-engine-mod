//===== Copy	right © 1996-2005, Valve Corporation, All rights reserved. ==//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//
#define lc_baseanimating_cpp

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lc_baseanimating.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "mathlib/lvector.h"
#include "model_types.h"	// HL2SB: STUDIO_RENDER, the default of Entity:DrawModel()
#include "lvphysics_interface.h"
#include "mathlib/lvmatrix.h"	// HL2SB: lua_pushvmatrix for Entity:GetBoneMatrix()
#include "studio.h"			// HL2SB: studiohdr_t for Entity:GetBoneCount()

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

/*
** access functions (stack -> C)
*/


LUA_API lua_CBaseAnimating *lua_toanimating (lua_State *L, int idx) {
  CBaseHandle *hEntity = dynamic_cast<CBaseHandle *>((CBaseHandle *)lua_touserdata(L, idx));
  if (hEntity == NULL)
    return NULL;
  return dynamic_cast<lua_CBaseAnimating *>(hEntity->Get());
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushanimating (lua_State *L, CBaseAnimating *pEntity) {
  CBaseHandle *hEntity = (CBaseHandle *)lua_newuserdata(L, sizeof(CBaseHandle));
  hEntity->Set(pEntity);
  luaL_getmetatable(L, "CBaseAnimating");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_CBaseAnimating *luaL_checkanimating (lua_State *L, int narg) {
  lua_CBaseAnimating *d = lua_toanimating(L, narg);
  if (d == NULL)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "CBaseAnimating expected, got NULL entity");
  return d;
}


static int CBaseAnimating_AddEntity (lua_State *L) {
  luaL_checkanimating(L, 1)->AddEntity();
  return 0;
}

static int CBaseAnimating_AddToClientSideAnimationList (lua_State *L) {
  luaL_checkanimating(L, 1)->AddToClientSideAnimationList();
  return 0;
}

static int CBaseAnimating_BecomeRagdollOnClient (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->BecomeRagdollOnClient());
  return 1;
}

static int CBaseAnimating_CalculateIKLocks (lua_State *L) {
  luaL_checkanimating(L, 1)->CalculateIKLocks(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_ClampCycle (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->ClampCycle(luaL_checknumber(L, 2), luaL_checkboolean(L, 3)));
  return 1;
}

static int CBaseAnimating_Clear (lua_State *L) {
  luaL_checkanimating(L, 1)->Clear();
  return 0;
}

static int CBaseAnimating_ClearRagdoll (lua_State *L) {
  luaL_checkanimating(L, 1)->ClearRagdoll();
  return 0;
}

static int CBaseAnimating_ClientSideAnimationChanged (lua_State *L) {
  luaL_checkanimating(L, 1)->ClientSideAnimationChanged();
  return 0;
}

static int CBaseAnimating_ComputeClientSideAnimationFlags (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->ComputeClientSideAnimationFlags());
  return 1;
}

static int CBaseAnimating_ComputeEntitySpaceHitboxSurroundingBox (lua_State *L) {
  Vector pVecWorldMins, pVecWorldMaxs;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->ComputeEntitySpaceHitboxSurroundingBox(&pVecWorldMins, &pVecWorldMaxs));
  lua_pushvector(L, pVecWorldMins);
  lua_pushvector(L, pVecWorldMaxs);
  return 3;
}

static int CBaseAnimating_ComputeHitboxSurroundingBox (lua_State *L) {
  Vector pVecWorldMins, pVecWorldMaxs;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->ComputeHitboxSurroundingBox(&pVecWorldMins, &pVecWorldMaxs));
  lua_pushvector(L, pVecWorldMins);
  lua_pushvector(L, pVecWorldMaxs);
  return 3;
}

static int CBaseAnimating_CreateRagdollCopy (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->CreateRagdollCopy());
  return 1;
}

static int CBaseAnimating_CreateUnragdollInfo (lua_State *L) {
  luaL_checkanimating(L, 1)->CreateUnragdollInfo(luaL_checkanimating(L, 2));
  return 0;
}

static int CBaseAnimating_DisableMuzzleFlash (lua_State *L) {
  luaL_checkanimating(L, 1)->DisableMuzzleFlash();
  return 0;
}

static int CBaseAnimating_DispatchMuzzleEffect (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->DispatchMuzzleEffect(luaL_checkstring(L, 2), luaL_checkboolean(L, 3)));
  return 1;
}

static int CBaseAnimating_DoMuzzleFlash (lua_State *L) {
  luaL_checkanimating(L, 1)->DoMuzzleFlash();
  return 0;
}

static int CBaseAnimating_DrawClientHitboxes (lua_State *L) {
  luaL_checkanimating(L, 1)->DrawClientHitboxes(luaL_optnumber(L, 2, 0.0f), luaL_optboolean(L, 3, false));
  return 0;
}

/*
** HL2SB: the player-model colour, i.e. the piece the 3D preview needs.
**
** GMod draws a clientside model with the colour the entity's Lua GetPlayerColor
** answers, which is exactly why GMod's player model selector writes
**
**     mdl.Entity.GetPlayerColor = function() return Vector( GetConVarString( "cl_playercolor" ) ) end
**
** (garrysmod/gamemodes/sandbox/gamemode/editor_player.lua, UpdateFromConvars).  In
** this fork nothing ever asked the entity for it, so moving the Colors tab wrote
** cl_playercolor and the preview model never changed.
**
** Only a Lua-provided GetPlayerColor is honoured: an entity without one is drawn
** with whatever modulation the caller set (lua/vgui/DModelPanel.lua:Paint sets its
** own Color), so nothing else changes behaviour.
**
** Returns true when a colour was applied; the caller must restore pflPrev (3 floats)
** after the draw.
*/
static bool HL2SB_IsLuaVector (lua_State *L, int idx) {
  if (!lua_isuserdata(L, idx))
    return false;
  if (lua_getmetatable(L, idx) == 0)
    return false;
  luaL_getmetatable(L, "Vector");
  bool bVector = lua_rawequal(L, -1, -2) != 0;
  lua_pop(L, 2);
  return bVector;
}

static bool HL2SB_ApplyLuaPlayerColor (lua_State *L, int nEntity, float *pflPrev) {
  lua_CBaseAnimating *pEntity = lua_toanimating(L, nEntity);

  if (pEntity == NULL || !lua_isrefvalid(L, pEntity->m_nTableReference))
    return false;

  lua_getref(L, pEntity->m_nTableReference);
  lua_getfield(L, -1, "GetPlayerColor");
  lua_remove(L, -2);                       // leave only the field

  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  lua_pushvalue(L, nEntity);               // self

  if (luasrc_pcall(L, 1, 1, 0) != 0) {     // the pcall leaves the error message
    lua_pop(L, 1);
    return false;
  }

  if (!HL2SB_IsLuaVector(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  const Vector &vecColor = luaL_checkvector(L, -1);
  lua_pop(L, 1);

  render->GetColorModulation(pflPrev);

  float color[3] = { vecColor.x, vecColor.y, vecColor.z };
  render->SetColorModulation(color);

  return true;
}

static int CBaseAnimating_DrawModel (lua_State *L) {
  // HL2SB GMod compat: wiki says `Entity:DrawModel( number flags = STUDIO_RENDER )`
  // - the flags are OPTIONAL. npc_shaklin_scp096's client ENT:Draw() calls it as
  // `self.Entity:DrawModel()`, and the old luaL_checkint(L, 2) turned that into
  // "bad argument #2" on every frame.
  lua_CBaseAnimating *pEntity = luaL_checkanimating(L, 1);

  float flPrevColor[3] = { 1.0f, 1.0f, 1.0f };
  bool bTinted = false;

  /*
  ** HL2SB: DISABLED (2026-09-17).  This read the entity's Lua GetPlayerColor and pushed
  ** it through render->SetColorModulation(), i.e. through the ENGINE's per-draw
  ** modulation - which writes $color2 on EVERY material of the model
  ** (studiorender/r_studio.cpp) and therefore overrode the real mechanism.
  **
  ** The real one is the player-model VMTs' own proxy:
  **
  **     Proxies { PlayerColor { resultVar $color2 ... } }      // "pass the player color
  **                                                           //  value to Gmod"
  **
  ** 42 materials in the GMod playermodel pack declare it, and the matching proxy
  ** implementation lives in game/client/c_viewmodel_attachment.cpp.  Tinting by VMT
  ** declaration is exactly GMod's behaviour ("what can be coloured, changes; what
  ** cannot, does not"), so the whole-model modulation must NOT be applied on top.
  */
  if ( false )
  {
    bTinted = HL2SB_ApplyLuaPlayerColor(L, 1, flPrevColor);
  }

  int nResult = 0;

  // HL2SB: NOT the virtual DrawModel() -- for a C_BaseScripted that virtual
  // re-dispatches ENT:Draw(), so "ENT:Draw() { self:DrawModel() }" recursed
  // into itself until the Lua C-stack limit aborted the chain.  Every level's
  // pcall swallowed the abort, so the real model draw NEVER ran and the entity
  // rendered nothing at all: the thrown cod-c4 was invisible while its red
  // blink sprite (drawn without going through DrawModel) still flashed.
  // GMod's Entity:DrawModel draws the model directly and never re-enters
  // RenderOverride / ENT:Draw -- InternalDrawModel is this fork's
  // non-dispatching path (C_BaseScripted does not override it).
  nResult = pEntity->InternalDrawModel(luaL_optint(L, 2, STUDIO_RENDER));

  if (bTinted)
    render->SetColorModulation(flPrevColor);

  lua_pushinteger(L, nResult);
  return 1;
}

// HL2SB GMod compat: bone accessors the minecraft SWEP's world model needs
// (the ClientsideModel world model is drawn from the player's hand bone
// matrix, and every bone is scaled to 0.4).
static int CBaseAnimating_GetBoneCount (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  const model_t *pModel = pEntity->GetModel();
  studiohdr_t *pHdr = pModel ? modelinfo->GetStudiomodel( pModel ) : NULL;
  lua_pushinteger(L, pHdr ? pHdr->numbones : 0);
  return 1;
}

static int CBaseAnimating_GetBoneMatrix (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);

  matrix3x4_t bones[128];
  if ( nBone < 0 || nBone >= 128 )
  {
    lua_pushnil(L);
    return 1;
  }
  if ( !pEntity->SetupBones( bones, 128, BONE_USED_BY_ANYTHING, gpGlobals->curtime ) )
  {
    lua_pushnil(L);
    return 1;
  }
  VMatrix vm;
  vm.CopyFrom3x4( bones[nBone] );
  lua_pushvmatrix(L, vm);
  return 1;
}

static int CBaseAnimating_ManipulateBoneScale (lua_State *L) {
  // HL2SB: the client C_BaseAnimating has no per-bone scale storage (that is
  // a server-side bonemanip feature this fork never ported).  The minecraft
  // world model sets the SAME uniform scale on every bone, which is exactly
  // Entity:SetModelScale -- apply it there so the held block shrinks; any
  // non-uniform per-bone scale degrades to whole-model scaling.
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  Vector scale = luaL_checkvector(L, 3);
  pEntity->SetModelScale( scale.x, 0.0f );
  return 0;
}

static int CBaseAnimating_FindBodygroupByName (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->FindBodygroupByName(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_FindFollowedEntity (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->FindFollowedEntity());
  return 1;
}

static int CBaseAnimating_FindTransitionSequence (lua_State *L) {
  int piDir;
  lua_pushinteger(L, luaL_checkanimating(L, 1)->FindTransitionSequence(luaL_checkint(L, 2), luaL_checkint(L, 3), &piDir));
  lua_pushinteger(L, piDir);
  return 2;
}

static int CBaseAnimating_FireEvent (lua_State *L) {
  luaL_checkanimating(L, 1)->FireEvent(luaL_checkvector(L, 2), luaL_checkangle(L, 3), luaL_checkint(L, 4), luaL_checkstring(L, 5));
  return 0;
}

static int CBaseAnimating_FireObsoleteEvent (lua_State *L) {
  luaL_checkanimating(L, 1)->FireObsoleteEvent(luaL_checkvector(L, 2), luaL_checkangle(L, 3), luaL_checkint(L, 4), luaL_checkstring(L, 5));
  return 0;
}

static int CBaseAnimating_ForceClientSideAnimationOn (lua_State *L) {
  luaL_checkanimating(L, 1)->ForceClientSideAnimationOn();
  return 0;
}

static int CBaseAnimating_FrameAdvance (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->FrameAdvance(luaL_optnumber(L, 2, 0.0f)));
  return 1;
}

static int CBaseAnimating_GetAimEntOrigin (lua_State *L) {
  luaL_checkanimating(L, 1)->GetAimEntOrigin(luaL_checkentity(L, 2), &luaL_checkvector(L, 3), &luaL_checkangle(L, 4));
  return 0;
}

static int CBaseAnimating_GetAnimTimeInterval (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetAnimTimeInterval());
  return 1;
}

static int CBaseAnimating_GetAttachment (lua_State *L) {
  switch(lua_type(L, 2)) {
	case LUA_TNUMBER:
      {
        if (lua_gettop(L) <= 3)
          lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachment(luaL_checkint(L, 2), luaL_checkvector(L, 3)));
        else
          lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachment(luaL_checkint(L, 2), luaL_checkvector(L, 3), luaL_checkangle(L, 4)));
        break;
      }
	case LUA_TSTRING:
	default:
      {
        if (lua_gettop(L) <= 3)
          lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachment(luaL_checkstring(L, 2), luaL_checkvector(L, 3)));
        else
          lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachment(luaL_checkstring(L, 2), luaL_checkvector(L, 3), luaL_checkangle(L, 4)));
        break;
	  }
  }
  return 1;
}

static int CBaseAnimating_GetAttachmentLocal (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachmentLocal(luaL_checkint(L, 2), luaL_checkvector(L, 3), luaL_checkangle(L, 4)));
  return 1;
}

static int CBaseAnimating_GetAttachmentVelocity (lua_State *L) {
  Vector originVel;
  Quaternion angleVel;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachmentVelocity(luaL_checkint(L, 2), originVel, angleVel));
  lua_pushvector(L, originVel);
  // Todo: implement Quaternion class!!
  // lua_pushquaternion(L, &angleVel);
  return 2;
}

static int CBaseAnimating_GetBaseAnimating (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->GetBaseAnimating());
  return 1;
}

static int CBaseAnimating_GetBlendedLinearVelocity (lua_State *L) {
  Vector pVec;
  luaL_checkanimating(L, 1)->GetBlendedLinearVelocity(&pVec);
  lua_pushvector(L, pVec);
  return 1;
}

static int CBaseAnimating_GetBody (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetBody());
  return 1;
}

static int CBaseAnimating_GetBodygroup (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetBodygroup(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetBodygroupCount (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetBodygroupCount(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetBodygroupName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetBodygroupName(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetBoneControllers (lua_State *L) {
  float controllers[MAXSTUDIOBONECTRLS];
  luaL_checkanimating(L, 1)->GetBoneControllers(controllers);
  int i;
  for( i=0; i < MAXSTUDIOBONECTRLS; i++)
  {
	  lua_pushnumber(L, controllers[ i ]);
  }
  return MAXSTUDIOBONECTRLS;
}

static int CBaseAnimating_GetBonePosition (lua_State *L) {
  luaL_checkanimating(L, 1)->GetBonePosition(luaL_checkint(L, 2), luaL_checkvector(L, 3), luaL_checkangle(L, 4));
  return 0;
}

static int CBaseAnimating_GetClientSideFade (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetClientSideFade());
  return 1;
}

static int CBaseAnimating_GetCollideType (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetCollideType());
  return 1;
}

static int CBaseAnimating_GetCycle (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetCycle());
  return 1;
}

static int CBaseAnimating_GetFlexControllerName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetFlexControllerName((LocalFlexController_t)luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetFlexControllerType (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetFlexControllerType((LocalFlexController_t)luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetFlexDescFacs (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetFlexDescFacs(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetHitboxSet (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetHitboxSet());
  return 1;
}

static int CBaseAnimating_GetHitboxSetCount (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetHitboxSetCount());
  return 1;
}

static int CBaseAnimating_GetHitboxSetName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetHitboxSetName());
  return 1;
}

/*static int CBaseAnimating_GetModelWidthScale (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetModelWidthScale());
  return 1;
}
*/

static int CBaseAnimating_GetNumBodyGroups (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetNumBodyGroups());
  return 1;
}

static int CBaseAnimating_GetNumFlexControllers (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetNumFlexControllers());
  return 1;
}

static int CBaseAnimating_GetPlaybackRate (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetPlaybackRate());
  return 1;
}

static int CBaseAnimating_GetPoseParameter (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetPoseParameter(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetPoseParameterRange (lua_State *L) {
  float minValue, maxValue;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->GetPoseParameterRange(luaL_checkint(L, 2), minValue, maxValue));
  lua_pushnumber(L, minValue);
  lua_pushnumber(L, maxValue);
  return 3;
}

static int CBaseAnimating_GetRenderAngles (lua_State *L) {
  QAngle v = luaL_checkanimating(L, 1)->GetRenderAngles();
  lua_pushangle(L, v);
  return 1;
}

static int CBaseAnimating_GetRenderBounds (lua_State *L) {
  Vector theMins, theMaxs;
  luaL_checkanimating(L, 1)->GetRenderBounds(theMins, theMaxs);
  lua_pushvector(L, theMins);
  lua_pushvector(L, theMaxs);
  return 2;
}

static int CBaseAnimating_GetRenderOrigin (lua_State *L) {
  Vector v = luaL_checkanimating(L, 1)->GetRenderOrigin();
  lua_pushvector(L, v);
  return 1;
}

static int CBaseAnimating_GetSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetSequence());
  return 1;
}

static int CBaseAnimating_GetSequenceActivity (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetSequenceActivity(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceActivityName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetSequenceActivityName(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceGroundSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetSequenceGroundSpeed(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceLinearMotion (lua_State *L) {
  Vector pVec;
  luaL_checkanimating(L, 1)->GetSequenceLinearMotion(luaL_checkint(L, 2), &pVec);
  lua_pushvector(L, pVec);
  return 1;
}

static int CBaseAnimating_GetSequenceName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetSequenceName(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetServerIntendedCycle (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetServerIntendedCycle());
  return 1;
}

static int CBaseAnimating_GetSkin (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetSkin());
  return 1;
}

static int CBaseAnimating_IgniteRagdoll (lua_State *L) {
  luaL_checkanimating(L, 1)->IgniteRagdoll(luaL_checkanimating(L, 2));
  return 0;
}

static int CBaseAnimating_InitBoneSetupThreadPool (lua_State *L) {
  CBaseAnimating::InitBoneSetupThreadPool();
  return 0;
}

static int CBaseAnimating_InitModelEffects (lua_State *L) {
  luaL_checkanimating(L, 1)->InitModelEffects();
  return 0;
}

static int CBaseAnimating_InternalDrawModel (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->InternalDrawModel(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_Interpolate (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->Interpolate(luaL_checknumber(L, 2)));
  return 1;
}

static int CBaseAnimating_InvalidateBoneCache (lua_State *L) {
  luaL_checkanimating(L, 1)->InvalidateBoneCache();
  return 0;
}

static int CBaseAnimating_InvalidateBoneCaches (lua_State *L) {
  CBaseAnimating::InvalidateBoneCaches();
  return 0;
}

static int CBaseAnimating_InvalidateMdlCache (lua_State *L) {
  luaL_checkanimating(L, 1)->InvalidateMdlCache();
  return 0;
}

static int CBaseAnimating_IsActivityFinished (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsActivityFinished());
  return 1;
}

static int CBaseAnimating_IsBoneCacheValid (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsBoneCacheValid());
  return 1;
}

static int CBaseAnimating_IsOnFire (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsOnFire());
  return 1;
}

static int CBaseAnimating_IsRagdoll (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsRagdoll());
  return 1;
}

static int CBaseAnimating_IsSelfAnimating (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsSelfAnimating());
  return 1;
}

static int CBaseAnimating_IsSequenceFinished (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsSequenceFinished());
  return 1;
}

static int CBaseAnimating_IsSequenceLooping (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsSequenceLooping(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_IsViewModel (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsViewModel());
  return 1;
}

static int CBaseAnimating_LookupActivity (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupActivity(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupAttachment (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupAttachment(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupBone (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupBone(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupPoseParameter (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupPoseParameter(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupRandomAttachment (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupRandomAttachment(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupSequence(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_NotifyShouldTransmit (lua_State *L) {
  luaL_checkanimating(L, 1)->NotifyShouldTransmit((ShouldTransmitState_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_OnDataChanged (lua_State *L) {
  luaL_checkanimating(L, 1)->OnDataChanged((DataUpdateType_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_OnPreDataChanged (lua_State *L) {
  luaL_checkanimating(L, 1)->OnPreDataChanged((DataUpdateType_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_PopBoneAccess (lua_State *L) {
  C_BaseAnimating::PopBoneAccess(luaL_checkstring(L, 1));
  return 0;
}

static int CBaseAnimating_PostDataUpdate (lua_State *L) {
  luaL_checkanimating(L, 1)->PostDataUpdate((DataUpdateType_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_PreDataUpdate (lua_State *L) {
  luaL_checkanimating(L, 1)->PreDataUpdate((DataUpdateType_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_ProcessMuzzleFlashEvent (lua_State *L) {
  luaL_checkanimating(L, 1)->ProcessMuzzleFlashEvent();
  return 0;
}

static int CBaseAnimating_PushAllowBoneAccess (lua_State *L) {
  C_BaseAnimating::PushAllowBoneAccess(luaL_checkboolean(L, 1), luaL_checkboolean(L, 2), luaL_checkstring(L, 3));
  return 0;
}

static int CBaseAnimating_RagdollMoved (lua_State *L) {
  luaL_checkanimating(L, 1)->RagdollMoved();
  return 0;
}

static int CBaseAnimating_Release (lua_State *L) {
  luaL_checkanimating(L, 1)->Release();
  return 0;
}

static int CBaseAnimating_RemoveFromClientSideAnimationList (lua_State *L) {
  luaL_checkanimating(L, 1)->RemoveFromClientSideAnimationList();
  return 0;
}

/*
static int CBaseAnimating_ResetEventsParity (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetEventsParity();
  return 0;
}
*/

static int CBaseAnimating_ResetLatched (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetLatched();
  return 0;
}

static int CBaseAnimating_ResetSequence (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetSequence(luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_ResetSequenceInfo (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetSequenceInfo();
  return 0;
}

static int CBaseAnimating_RetrieveRagdollInfo (lua_State *L) {
  Vector pos;
  Quaternion q;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->RetrieveRagdollInfo(&pos, &q));
  lua_pushvector(L, pos);
  // Todo: implement Quaternion class!!
  // lua_pushquaternion(L, &q);
  return 2;
}

static int CBaseAnimating_SelectWeightedSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->SelectWeightedSequence(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_SequenceDuration (lua_State *L) {
  if (lua_isnoneornil(L, 2))
    lua_pushnumber(L, luaL_checkanimating(L, 1)->SequenceDuration());
  else
	lua_pushnumber(L, luaL_checkanimating(L, 1)->SequenceDuration(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_SequenceLoops (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->SequenceLoops());
  return 1;
}

static int CBaseAnimating_SetBodygroup (lua_State *L) {
  luaL_checkanimating(L, 1)->SetBodygroup(luaL_checkint(L, 2), luaL_checkint(L, 3));
  return 0;
}

static int CBaseAnimating_SetBoneController (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->SetBoneController(luaL_checkint(L, 2), luaL_checknumber(L, 3)));
  return 1;
}

static int CBaseAnimating_SetCycle (lua_State *L) {
  luaL_checkanimating(L, 1)->SetCycle(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_SetHitboxSet (lua_State *L) {
  luaL_checkanimating(L, 1)->SetHitboxSet(luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_SetHitboxSetByName (lua_State *L) {
  luaL_checkanimating(L, 1)->SetHitboxSetByName(luaL_checkstring(L, 2));
  return 0;
}

/*
static int CBaseAnimating_SetModelWidthScale (lua_State *L) {
  luaL_checkanimating(L, 1)->SetModelWidthScale(luaL_checknumber(L, 2));
  return 0;
}
*/

static int CBaseAnimating_SetPlaybackRate (lua_State *L) {
  luaL_checkanimating(L, 1)->SetPlaybackRate(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_SetPoseParameter (lua_State *L) {
  switch(lua_type(L, 2)) {
	case LUA_TNUMBER:
	  lua_pushnumber(L, luaL_checkanimating(L, 1)->SetPoseParameter(luaL_checkint(L, 2), luaL_checknumber(L, 3)));
	  break;
	case LUA_TSTRING:
	default:
	  lua_pushnumber(L, luaL_checkanimating(L, 1)->SetPoseParameter(luaL_checkstring(L, 2), luaL_checknumber(L, 3)));
	  break;
  }
  return 1;
}

static int CBaseAnimating_SetPredictable (lua_State *L) {
  luaL_checkanimating(L, 1)->SetPredictable(luaL_checkboolean(L, 2));
  return 0;
}

static int CBaseAnimating_SetPredictionEligible (lua_State *L) {
  luaL_checkanimating(L, 1)->SetPredictionEligible(luaL_checkboolean(L, 2));
  return 0;
}

static int CBaseAnimating_SetPredictionPlayer (lua_State *L) {
  luaL_checkanimating(L, 1)->SetPredictionPlayer(luaL_checkplayer(L, 2));
  return 0;
}

static int CBaseAnimating_SetReceivedSequence (lua_State *L) {
  luaL_checkanimating(L, 1)->SetReceivedSequence();
  return 0;
}

static int CBaseAnimating_SetSequence (lua_State *L) {
  luaL_checkanimating(L, 1)->SetSequence(luaL_checkint(L, 2));
  return 0;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Entity:TranslatePhysBoneToBone( physBone ) -- the bone
// index a physics bone is attached to (wiki).  The mapping lives in the studio
// bone table (mstudiobone_t::physicsbone); no match answers -1.
//-----------------------------------------------------------------------------
static int CBaseAnimating_TranslatePhysBoneToBone (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating( L, 1 );
  int nPhysBone = luaL_checkint( L, 2 );

  CStudioHdr *pStudioHdr = pEntity->GetModelPtr();
  if ( pStudioHdr != NULL ) {
    for ( int i = 0; i < pStudioHdr->numbones(); i++ ) {
      if ( pStudioHdr->pBone( i )->physicsbone == nPhysBone ) {
        lua_pushinteger( L, i );
        return 1;
      }
    }
  }

  lua_pushinteger( L, -1 );
  return 1;
}

static int CBaseAnimating_SetServerIntendedCycle (lua_State *L) {
  luaL_checkanimating(L, 1)->SetServerIntendedCycle(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_ShadowCastType (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->ShadowCastType());
  return 1;
}

static int CBaseAnimating_ShouldMuzzleFlash (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->ShouldMuzzleFlash());
  return 1;
}

static int CBaseAnimating_ShouldResetSequenceOnNewModel (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->ShouldResetSequenceOnNewModel());
  return 1;
}

static int CBaseAnimating_ShutdownBoneSetupThreadPool (lua_State *L) {
  CBaseAnimating::ShutdownBoneSetupThreadPool();
  return 0;
}

static int CBaseAnimating_Simulate (lua_State *L) {
  luaL_checkanimating(L, 1)->Simulate();
  return 0;
}

static int CBaseAnimating_StudioFrameAdvance (lua_State *L) {
  luaL_checkanimating(L, 1)->StudioFrameAdvance();
  return 0;
}

static int CBaseAnimating_ThreadedBoneSetup (lua_State *L) {
  CBaseAnimating::ThreadedBoneSetup();
  return 0;
}

static int CBaseAnimating_TransferDissolveFrom (lua_State *L) {
  luaL_checkanimating(L, 1)->TransferDissolveFrom(luaL_checkanimating(L, 2));
  return 0;
}

static int CBaseAnimating_UncorrectViewModelAttachment (lua_State *L) {
  luaL_checkanimating(L, 1)->UncorrectViewModelAttachment(luaL_checkvector(L, 2));
  return 0;
}

static int CBaseAnimating_UpdateClientSideAnimation (lua_State *L) {
  luaL_checkanimating(L, 1)->UpdateClientSideAnimation();
  return 0;
}

static int CBaseAnimating_UpdateClientSideAnimations (lua_State *L) {
  CBaseAnimating::UpdateClientSideAnimations();
  return 0;
}

static int CBaseAnimating_UpdateIKLocks (lua_State *L) {
  luaL_checkanimating(L, 1)->UpdateIKLocks(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_UseClientSideAnimation (lua_State *L) {
  luaL_checkanimating(L, 1)->UseClientSideAnimation();
  return 0;
}

static int CBaseAnimating_UsesPowerOfTwoFrameBufferTexture (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->UsesPowerOfTwoFrameBufferTexture());
  return 1;
}

static int CBaseAnimating_VPhysicsGetObjectList (lua_State *L) {
  IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
  int count = luaL_checkanimating(L, 1)->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
  lua_pushinteger(L, count);
  lua_newtable(L);
  for( int i = 0 ; i < count ; i++ )
  {
	  lua_pushinteger(L, i);
	  lua_pushphysicsobject(L, pList[i]);
	  lua_settable(L, -3);
  }
  return 2;
}

static int CBaseAnimating_VPhysicsUpdate (lua_State *L) {
  luaL_checkanimating(L, 1)->VPhysicsUpdate(luaL_checkphysicsobject(L, 2));
  return 0;
}

// HL2SB GMod compat: on the CLIENT a Lua nextbot has no per-instance script table
// (m_nTableReference is taken on the server; the client entity is a plain
// C_NextBotCombatCharacter built from the network).  So a script that reaches for
// `self.Entity`, or calls one of its own hooks - base_nextbot's default
// ENTITY:DrawTranslucent does `self:Draw( flags )` - resolved to nil, and calling
// that nil aborted the game ("attempt to call a nil value" with an EMPTY traceback,
// because the lookup happened in C, outside any pcall).
//
// Give back the two things GMod's ENTITY tables provide:
//   "Entity"        -> the entity itself (GMod's ENT.Entity is the same object)
//   anything else   -> the script's class table, scripted_ents.GetStored( c ).t
//
// The class-table read goes through a protected call on purpose: a script table can
// carry an __index FUNCTION, and an error raised by a bare lua_gettable from C is
// unprotected - it lands in lua_atpanic and kills the process.
static int LuaGetTableKey (lua_State *L) {  /* [t][k] -> [t[k]] */
  lua_gettable(L, 1);
  return 1;
}

static void LuaPushScriptedEntityField ( lua_State *L, const char *pszClassname, const char *pszKey )
{
  if ( pszClassname == NULL || pszClassname[0] == '\0' || pszKey == NULL )
  {
    lua_pushnil( L );
    return;
  }

  lua_getglobal( L, "scripted_ents" );
  if ( !lua_istable( L, -1 ) ) { lua_pop( L, 1 ); lua_pushnil( L ); return; }

  lua_getfield( L, -1, "GetStored" );
  if ( !lua_isfunction( L, -1 ) ) { lua_pop( L, 2 ); lua_pushnil( L ); return; }

  lua_pushstring( L, pszClassname );
  if ( luasrc_pcall( L, 1, 1, 0 ) != 0 || !lua_istable( L, -1 ) ) { lua_pop( L, 3 ); lua_pushnil( L ); return; }

  lua_getfield( L, -1, "t" );                       // stored.t
  if ( !lua_istable( L, -1 ) ) { lua_pop( L, 4 ); lua_pushnil( L ); return; }
  lua_remove( L, -2 );                              // stored
  lua_remove( L, -2 );                              // GetStored
  lua_remove( L, -2 );                              // scripted_ents   -> [t]

  lua_pushcfunction( L, LuaGetTableKey );
  lua_pushvalue( L, -2 );                           // t
  lua_pushstring( L, pszKey );
  if ( luasrc_pcall( L, 2, 1, 0 ) != 0 )
    lua_pushnil( L );
  lua_remove( L, -2 );                              // leave only the value
}

static int CBaseAnimating___index (lua_State *L) {
  CBaseAnimating *pEntity = lua_toanimating(L, 1);
  if (pEntity == NULL) {  /* avoid extra test when d is not 0 */
    /* HL2SB: GMod's NULL sentinel answers reads instead of raising -- same
    ** contract as CBaseEntity___index / CBasePlayer___index. */
    HL2SB_PushNullEntityIndex( L, lua_tostring( L, 2 ) );
    return 1;
  }
  if (lua_isrefvalid(L, pEntity->m_nTableReference)) {
    // HL2SB (2026-09-20): SCRIPT-TABLE FUNCTIONS are overrides and win over
    // C++ methods; a script-table DATA (non-function) field never shadows a
    // C++ method.  Same rule as the server copy in lbaseanimating.cpp -- keep
    // the two in sync.  Raw reads: lua_gettable on the metatable would
    // re-enter this __index through its own __index field and answer
    // script-table fields with the NULL-sentinel method.
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;                    // Lua override beats everything
    lua_pop(L, 2);

    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;                    // C++ method beats a data field
    lua_pop(L, 2);

    luaL_getmetatable(L, LUA_BASEANIMATINGLIBNAME);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;
    lua_pop(L, 2);

    luaL_getmetatable(L, "CBaseEntity");
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;
    lua_pop(L, 2);

    // script-table data value
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    // falls through to the legacy self-reference / scripted-field tail below
  }
  else {
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);

      /* the same CBaseAnimating-metatable step as above */
      luaL_getmetatable(L, LUA_BASEANIMATINGLIBNAME);
      lua_pushvalue(L, 2);
      lua_gettable(L, -2);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        luaL_getmetatable(L, "CBaseEntity");
        lua_pushvalue(L, 2);
        lua_gettable(L, -2);
      }
    }
  }
  // HL2SB GMod compat: nothing in the entity's own table and nothing in the
  // bindings -> try the script's class table (see LuaPushScriptedEntityField),
  // and answer GMod's deprecated self-reference fields ("Entity", and the
  // SWEP spelling "Weapon") with the entity itself, like GMod does.  Reaching
  // here with a bound table now works too: the script-table-data read above
  // falls through when it produced nil (2026-09-21).
  if ( lua_isnil( L, -1 ) )
  {
    const char *pszKey = lua_tostring( L, 2 );
    lua_pop( L, 1 );

    if ( pszKey != NULL )
    {
      if ( Q_strcmp( pszKey, "Entity" ) == 0 || Q_strcmp( pszKey, "Weapon" ) == 0 )
      {
        lua_pushvalue( L, 1 );        // self.Entity == self, like GMod's ENT.Entity
        return 1;
      }

      LuaPushScriptedEntityField( L, pEntity->GetClassname(), pszKey );
      return 1;
    }
  }
  return 1;
}

static int CBaseAnimating___newindex (lua_State *L) {
  CBaseAnimating *pEntity = lua_toanimating(L, 1);
  if (pEntity == NULL) {  /* avoid extra test when d is not 0 */
    lua_Debug ar1;
    lua_getstack(L, 1, &ar1);
    lua_getinfo(L, "fl", &ar1);
    lua_Debug ar2;
    lua_getinfo(L, ">S", &ar2);
	lua_pushfstring(L, "%s:%d: attempt to index a NULL entity", ar2.short_src, ar1.currentline);
	return lua_error(L);
  }
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "m_bClientSideAnimation") == 0)
    pEntity->m_bClientSideAnimation = luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_bLastClientSideFrameReset") == 0)
    pEntity->m_bLastClientSideFrameReset = luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_nBody") == 0)
    pEntity->m_nBody = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_nHitboxSet") == 0)
    pEntity->m_nHitboxSet = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_nSkin") == 0)
    pEntity->m_nSkin = luaL_checkint(L, 3);
  else {
    // HL2SB: < 0, not == LUA_NOREF -- LUA_REFNIL (-1) is a legal "no table"
    // state; the old test let lua_getref(-1) push nil and silently drop the
    // field write (see CBaseEntity___newindex).
    if (pEntity->m_nTableReference < 0) {
      lua_newtable(L);
      pEntity->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, field);
	lua_pop(L, 1);
  }
  return 0;
}

static int CBaseAnimating___eq (lua_State *L) {
  lua_pushboolean(L, lua_toanimating(L, 1) == lua_toanimating(L, 2));
  return 1;
}

static int CBaseAnimating___tostring (lua_State *L) {
  CBaseAnimating *pEntity = lua_toanimating(L, 1);
  if (pEntity == NULL)
    lua_pushstring(L, "NULL");
  else
    lua_pushfstring(L, "CBaseAnimating: %d \"%s\"", pEntity->entindex(), pEntity->GetClassname());
  return 1;
}


static const luaL_Reg CBaseAnimatingmeta[] = {
  {"AddEntity", CBaseAnimating_AddEntity},
  {"AddToClientSideAnimationList", CBaseAnimating_AddToClientSideAnimationList},
  {"BecomeRagdollOnClient", CBaseAnimating_BecomeRagdollOnClient},
  {"CalculateIKLocks", CBaseAnimating_CalculateIKLocks},
  {"ClampCycle", CBaseAnimating_ClampCycle},
  {"Clear", CBaseAnimating_Clear},
  {"ClearRagdoll", CBaseAnimating_ClearRagdoll},
  {"ClientSideAnimationChanged", CBaseAnimating_ClientSideAnimationChanged},
  {"ComputeClientSideAnimationFlags", CBaseAnimating_ComputeClientSideAnimationFlags},
  {"ComputeEntitySpaceHitboxSurroundingBox", CBaseAnimating_ComputeEntitySpaceHitboxSurroundingBox},
  {"ComputeHitboxSurroundingBox", CBaseAnimating_ComputeHitboxSurroundingBox},
  {"CreateRagdollCopy", CBaseAnimating_CreateRagdollCopy},
  {"CreateUnragdollInfo", CBaseAnimating_CreateUnragdollInfo},
  {"DisableMuzzleFlash", CBaseAnimating_DisableMuzzleFlash},
  {"DispatchMuzzleEffect", CBaseAnimating_DispatchMuzzleEffect},
  {"DoMuzzleFlash", CBaseAnimating_DoMuzzleFlash},
  {"DrawClientHitboxes", CBaseAnimating_DrawClientHitboxes},
  {"DrawModel", CBaseAnimating_DrawModel},
  // HL2SB GMod compat: bone accessors (minecraft SWEP world model).
  {"GetBoneCount", CBaseAnimating_GetBoneCount},
  {"GetBoneMatrix", CBaseAnimating_GetBoneMatrix},
  {"ManipulateBoneScale", CBaseAnimating_ManipulateBoneScale},
  {"FindBodygroupByName", CBaseAnimating_FindBodygroupByName},
  {"FindFollowedEntity", CBaseAnimating_FindFollowedEntity},
  {"FindTransitionSequence", CBaseAnimating_FindTransitionSequence},
  {"FireEvent", CBaseAnimating_FireEvent},
  {"FireObsoleteEvent", CBaseAnimating_FireObsoleteEvent},
  {"ForceClientSideAnimationOn", CBaseAnimating_ForceClientSideAnimationOn},
  {"FrameAdvance", CBaseAnimating_FrameAdvance},
  {"GetAimEntOrigin", CBaseAnimating_GetAimEntOrigin},
  {"GetAnimTimeInterval", CBaseAnimating_GetAnimTimeInterval},
  {"GetAttachment", CBaseAnimating_GetAttachment},
  {"GetAttachmentLocal", CBaseAnimating_GetAttachmentLocal},
  {"GetAttachmentVelocity", CBaseAnimating_GetAttachmentVelocity},
  {"GetBaseAnimating", CBaseAnimating_GetBaseAnimating},
  {"GetBlendedLinearVelocity", CBaseAnimating_GetBlendedLinearVelocity},
  {"GetBody", CBaseAnimating_GetBody},
  {"GetBodygroup", CBaseAnimating_GetBodygroup},
  {"GetBodygroupCount", CBaseAnimating_GetBodygroupCount},
  {"GetBodygroupName", CBaseAnimating_GetBodygroupName},
  {"GetBoneControllers", CBaseAnimating_GetBoneControllers},
  {"GetBonePosition", CBaseAnimating_GetBonePosition},
  {"GetClientSideFade", CBaseAnimating_GetClientSideFade},
  {"GetCollideType", CBaseAnimating_GetCollideType},
  {"GetCycle", CBaseAnimating_GetCycle},
  {"GetFlexControllerName", CBaseAnimating_GetFlexControllerName},
  {"GetFlexControllerType", CBaseAnimating_GetFlexControllerType},
  {"GetFlexDescFacs", CBaseAnimating_GetFlexDescFacs},
  {"GetHitboxSet", CBaseAnimating_GetHitboxSet},
  {"GetHitboxSetCount", CBaseAnimating_GetHitboxSetCount},
  {"GetHitboxSetName", CBaseAnimating_GetHitboxSetName},
//  {"GetModelWidthScale", CBaseAnimating_GetModelWidthScale},
  {"GetNumBodyGroups", CBaseAnimating_GetNumBodyGroups},
  {"GetNumFlexControllers", CBaseAnimating_GetNumFlexControllers},
  {"GetPlaybackRate", CBaseAnimating_GetPlaybackRate},
  {"GetPoseParameter", CBaseAnimating_GetPoseParameter},
  {"GetPoseParameterRange", CBaseAnimating_GetPoseParameterRange},
  {"GetRenderAngles", CBaseAnimating_GetRenderAngles},
  {"GetRenderBounds", CBaseAnimating_GetRenderBounds},
  {"GetRenderOrigin", CBaseAnimating_GetRenderOrigin},
  {"GetSequence", CBaseAnimating_GetSequence},
  {"GetSequenceActivity", CBaseAnimating_GetSequenceActivity},
  {"GetSequenceActivityName", CBaseAnimating_GetSequenceActivityName},
  {"GetSequenceGroundSpeed", CBaseAnimating_GetSequenceGroundSpeed},
  {"GetSequenceLinearMotion", CBaseAnimating_GetSequenceLinearMotion},
  {"GetSequenceName", CBaseAnimating_GetSequenceName},
  {"GetServerIntendedCycle", CBaseAnimating_GetServerIntendedCycle},
  {"GetSkin", CBaseAnimating_GetSkin},
  {"IgniteRagdoll", CBaseAnimating_IgniteRagdoll},
  {"InitBoneSetupThreadPool", CBaseAnimating_InitBoneSetupThreadPool},
  {"InitModelEffects", CBaseAnimating_InitModelEffects},
  {"InternalDrawModel", CBaseAnimating_InternalDrawModel},
  {"Interpolate", CBaseAnimating_Interpolate},
  {"InvalidateBoneCache", CBaseAnimating_InvalidateBoneCache},
  {"InvalidateBoneCaches", CBaseAnimating_InvalidateBoneCaches},
  {"InvalidateMdlCache", CBaseAnimating_InvalidateMdlCache},
  {"IsActivityFinished", CBaseAnimating_IsActivityFinished},
  {"IsBoneCacheValid", CBaseAnimating_IsBoneCacheValid},
  {"IsOnFire", CBaseAnimating_IsOnFire},
  {"IsRagdoll", CBaseAnimating_IsRagdoll},
  {"IsSelfAnimating", CBaseAnimating_IsSelfAnimating},
  {"IsSequenceFinished", CBaseAnimating_IsSequenceFinished},
  {"IsSequenceLooping", CBaseAnimating_IsSequenceLooping},
  {"IsViewModel", CBaseAnimating_IsViewModel},
  {"LookupActivity", CBaseAnimating_LookupActivity},
  {"LookupAttachment", CBaseAnimating_LookupAttachment},
  {"LookupBone", CBaseAnimating_LookupBone},
  {"LookupPoseParameter", CBaseAnimating_LookupPoseParameter},
  {"LookupRandomAttachment", CBaseAnimating_LookupRandomAttachment},
  {"LookupSequence", CBaseAnimating_LookupSequence},
  {"NotifyShouldTransmit", CBaseAnimating_NotifyShouldTransmit},
  {"OnDataChanged", CBaseAnimating_OnDataChanged},
  {"OnPreDataChanged", CBaseAnimating_OnPreDataChanged},
  {"PopBoneAccess", CBaseAnimating_PopBoneAccess},
  {"PostDataUpdate", CBaseAnimating_PostDataUpdate},
  {"PreDataUpdate", CBaseAnimating_PreDataUpdate},
  {"ProcessMuzzleFlashEvent", CBaseAnimating_ProcessMuzzleFlashEvent},
  {"PushAllowBoneAccess", CBaseAnimating_PushAllowBoneAccess},
  {"RagdollMoved", CBaseAnimating_RagdollMoved},
  {"Release", CBaseAnimating_Release},
  {"RemoveFromClientSideAnimationList", CBaseAnimating_RemoveFromClientSideAnimationList},
//  {"ResetEventsParity", CBaseAnimating_ResetEventsParity},
  {"ResetLatched", CBaseAnimating_ResetLatched},
  {"ResetSequence", CBaseAnimating_ResetSequence},
  {"ResetSequenceInfo", CBaseAnimating_ResetSequenceInfo},
  {"RetrieveRagdollInfo", CBaseAnimating_RetrieveRagdollInfo},
  {"SelectWeightedSequence", CBaseAnimating_SelectWeightedSequence},
  {"SequenceDuration", CBaseAnimating_SequenceDuration},
  {"SequenceLoops", CBaseAnimating_SequenceLoops},
  {"SetBodygroup", CBaseAnimating_SetBodygroup},
  {"SetBoneController", CBaseAnimating_SetBoneController},
  {"TranslatePhysBoneToBone", CBaseAnimating_TranslatePhysBoneToBone},
  {"SetCycle", CBaseAnimating_SetCycle},
  {"SetHitboxSet", CBaseAnimating_SetHitboxSet},
  {"SetHitboxSetByName", CBaseAnimating_SetHitboxSetByName},
 // {"SetModelWidthScale", CBaseAnimating_SetModelWidthScale},
  {"SetPlaybackRate", CBaseAnimating_SetPlaybackRate},
  {"SetPoseParameter", CBaseAnimating_SetPoseParameter},
  {"SetPredictable", CBaseAnimating_SetPredictable},
  {"SetPredictionEligible", CBaseAnimating_SetPredictionEligible},
  {"SetPredictionPlayer", CBaseAnimating_SetPredictionPlayer},
  {"SetReceivedSequence", CBaseAnimating_SetReceivedSequence},
  {"SetSequence", CBaseAnimating_SetSequence},
  {"SetServerIntendedCycle", CBaseAnimating_SetServerIntendedCycle},
  {"ShadowCastType", CBaseAnimating_ShadowCastType},
  {"ShouldMuzzleFlash", CBaseAnimating_ShouldMuzzleFlash},
  {"ShouldResetSequenceOnNewModel", CBaseAnimating_ShouldResetSequenceOnNewModel},
  {"ShutdownBoneSetupThreadPool", CBaseAnimating_ShutdownBoneSetupThreadPool},
  {"Simulate", CBaseAnimating_Simulate},
  {"StudioFrameAdvance", CBaseAnimating_StudioFrameAdvance},
  {"ThreadedBoneSetup", CBaseAnimating_ThreadedBoneSetup},
  {"TransferDissolveFrom", CBaseAnimating_TransferDissolveFrom},
  {"UncorrectViewModelAttachment", CBaseAnimating_UncorrectViewModelAttachment},
  {"UpdateClientSideAnimation", CBaseAnimating_UpdateClientSideAnimation},
  {"UpdateClientSideAnimations", CBaseAnimating_UpdateClientSideAnimations},
  {"UpdateIKLocks", CBaseAnimating_UpdateIKLocks},
  {"UseClientSideAnimation", CBaseAnimating_UseClientSideAnimation},
  {"UsesPowerOfTwoFrameBufferTexture", CBaseAnimating_UsesPowerOfTwoFrameBufferTexture},
  {"VPhysicsGetObjectList", CBaseAnimating_VPhysicsGetObjectList},
  {"VPhysicsUpdate", CBaseAnimating_VPhysicsUpdate},
  {"__index", CBaseAnimating___index},
  {"__newindex", CBaseAnimating___newindex},
  {"__eq", CBaseAnimating___eq},
  {"__tostring", CBaseAnimating___tostring},
  {NULL, NULL}
};


/*
** Open CBaseAnimating object
*/
LUALIB_API int luaopen_CBaseAnimating (lua_State *L) {
  luaL_newmetatable(L, LUA_BASEANIMATINGLIBNAME);
  luaL_register(L, NULL, CBaseAnimatingmeta);
  lua_pushstring(L, "entity");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "entity" */
  return 1;
}

