//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Base class for all animating characters and objects.
//
//=============================================================================//

#define lbaseanimating_cpp

#include "cbase.h"
#include "luamanager.h"
#include "lbaseanimating.h"
#include "lbaseentity_shared.h"	// HL2SB: HL2SB_PushNullEntityIndex for the NULL __index branch
#include "mathlib/lvector.h"
#include "lvphysics_interface.h"
#include "ltakedamageinfo.h"	// luaL_checkdamageinfo (Entity:BecomeRagdoll)
#include "physics_prop_ragdoll.h"	// CreateServerRagdoll (Entity:BecomeRagdoll)
#include "BaseAnimatingOverlay.h"	// Entity:RestartGesture (overlay gestures)

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


static int CBaseAnimating_CalculateIKLocks (lua_State *L) {
  luaL_checkanimating(L, 1)->CalculateIKLocks(luaL_checknumber(L, 2));
  return 0;
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

static int CBaseAnimating_DoMuzzleFlash (lua_State *L) {
  luaL_checkanimating(L, 1)->DoMuzzleFlash();
  return 0;
}

static int CBaseAnimating_FindBodygroupByName (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->FindBodygroupByName(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_FindTransitionSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->FindTransitionSequence(luaL_checkinteger(L, 2), luaL_checkinteger(L, 3), NULL));
  return 1;
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

static int CBaseAnimating_GetBaseAnimating (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->GetBaseAnimating());
  return 1;
}

static int CBaseAnimating_GetBodygroup (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetBodygroup(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_GetBodygroupCount (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetBodygroupCount(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_GetBodygroupName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetBodygroupName(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_GetBonePosition (lua_State *L) {
  Vector origin;
  QAngle angles;
  luaL_checkanimating(L, 1)->GetBonePosition(luaL_checkinteger(L, 2), origin, angles);
  lua_pushvector(L, origin);
  lua_pushangle(L, angles);
  return 2;
}

static int CBaseAnimating_GetCycle (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetCycle());
  return 1;
}

static int CBaseAnimating_GetFlexDescFacs (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetFlexDescFacs(luaL_checkinteger(L, 2)));
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

/*
static int CBaseAnimating_GetModelWidthScale (lua_State *L) {
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
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetPoseParameter(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_GetPoseParameterRange (lua_State *L) {
  float minValue, maxValue;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->GetPoseParameterRange(luaL_checkinteger(L, 2), minValue, maxValue));
  lua_pushnumber(L, minValue);
  lua_pushnumber(L, maxValue);
  return 3;
}

static int CBaseAnimating_GetSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetSequence());
  return 1;
}

static int CBaseAnimating_GetSequenceActivity (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetSequenceActivity(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceActivityName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetSequenceActivityName(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceGroundSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetSequenceGroundSpeed(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceLinearMotion (lua_State *L) {
  Vector pVec;
  luaL_checkanimating(L, 1)->GetSequenceLinearMotion(luaL_checkinteger(L, 2), &pVec);
  lua_pushvector(L, pVec);
  return 1;
}

static int CBaseAnimating_GetSequenceName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetSequenceName(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_InvalidateBoneCache (lua_State *L) {
  luaL_checkanimating(L, 1)->InvalidateBoneCache();
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

static int CBaseAnimating_IsOnFire (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsOnFire());
  return 1;
}

static int CBaseAnimating_IsRagdoll (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsRagdoll());
  return 1;
}

static int CBaseAnimating_IsSequenceFinished (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsSequenceFinished());
  return 1;
}

static int CBaseAnimating_IsSequenceLooping (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsSequenceLooping(luaL_checkinteger(L, 2)));
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

static int CBaseAnimating_LookupSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupSequence(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_ResetSequence (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetSequence(luaL_checkinteger(L, 2));
  return 0;
}

static int CBaseAnimating_ResetSequenceInfo (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetSequenceInfo();
  return 0;
}

static int CBaseAnimating_SelectWeightedSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->SelectWeightedSequence((Activity)luaL_checkinteger(L, 2)));
  return 1;
}

static int CBaseAnimating_SequenceDuration (lua_State *L) {
  switch(lua_type(L, 2)) {
    case LUA_TNONE:
    default:
      lua_pushnumber(L, luaL_checkanimating(L, 1)->SequenceDuration());
      break;
    case LUA_TNUMBER:
      lua_pushnumber(L, luaL_checkanimating(L, 1)->SequenceDuration(luaL_checkint(L, 2)));
      break;
  }
  return 1;
}

static int CBaseAnimating_SequenceLoops (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->SequenceLoops());
  return 1;
}

static int CBaseAnimating_SetBoneController (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->SetBoneController(luaL_checkinteger(L, 2), luaL_checknumber(L, 3)));
  return 1;
}

static int CBaseAnimating_SetCycle (lua_State *L) {
  luaL_checkanimating(L, 1)->SetCycle(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_SetHitboxSet (lua_State *L) {
  luaL_checkanimating(L, 1)->SetHitboxSet(luaL_checkinteger(L, 2));
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
  // HL2SB GMod compat: the pose NAME is the form GMod addons use
  // (scp049's MovementFunctions calls self:SetPoseParameter( "move_x", rate )).
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

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Entity:TranslatePhysBoneToBone( physBone ) -- the bone
// index a physics bone is attached to (wiki).  The C++ classes have no such
// method in this fork, but the mapping is right there in the studio bone table
// (mstudiobone_t::physicsbone), so walk it here.  No match answers -1, the
// GMod value for "not part of the ragdoll".
//-----------------------------------------------------------------------------
static int CBaseAnimating_TranslatePhysBoneToBone (lua_State *L) {
  CBaseAnimating *pEntity = luaL_checkanimating( L, 1 );
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

static int CBaseAnimating_SetSequence (lua_State *L) {
  luaL_checkanimating(L, 1)->SetSequence(luaL_checkinteger(L, 2));
  return 0;
}

// HL2SB GMod compat: Entity:StartActivity( activity ) and Entity:GetActivity().
//
// GMod scripts animate through these two NAMES - base_nextbot's BodyUpdate()
// opens with `local act = self:GetActivity()` and NPC-like scripts call
// `self:StartActivity( ACT_* )` all over their behaviour (SCP-096's
// Initialize() does, at init.lua:104, and until this existed that call raised
// "attempt to call a nil value (method 'StartActivity')", which aborted the rest
// of the function).
//
// CBaseAnimating has neither a SetActivity nor a GetActivity, so both are the
// documented pairing built from what it does have:
//   StartActivity -> SelectWeightedSequence( act ) then SetSequence( seq )
//   GetActivity   -> GetSequenceActivity( GetSequence() )
// A negative sequence means "this model has no sequence for that activity", and
// setting it anyway would be worse than doing nothing.
static int CBaseAnimating_StartActivity (lua_State *L) {
  CBaseAnimating *pEntity = luaL_checkanimating(L, 1);
  const int sequence = pEntity->SelectWeightedSequence( (Activity)luaL_checkinteger(L, 2) );

  if ( sequence >= 0 )
  {
    pEntity->SetSequence( sequence );

    // HL2SB: SetSequence() alone leaves the pose FROZEN.  ResetSequenceInfo() is
    // what sets m_flPlaybackRate back to 1 (and re-arms the frame counters), and
    // nothing else in this binding does it -- SCP-096's Initialize() played
    // ACT_IDLE_ANGRY and the engine reported "sequence=4 activity=6 cycle=0.00
    // rate=0.00": a sequence selected, and not a single frame of it ever
    // advanced.  This mirrors CBaseAnimating::SetActivity(), which is the
    // function GMod's Entity:StartActivity() is.
    pEntity->ResetSequenceInfo();
    pEntity->SetCycle( 0.0f );
  }

  return 0;
}

static int CBaseAnimating_GetActivity (lua_State *L) {
  CBaseAnimating *pEntity = luaL_checkanimating(L, 1);

  lua_pushinteger(L, (int)pEntity->GetSequenceActivity( pEntity->GetSequence() ) );
  return 1;
}

static int CBaseAnimating_StudioFrameAdvance (lua_State *L) {
  luaL_checkanimating(L, 1)->StudioFrameAdvance();
  return 0;
}

// HL2SB GMod compat: the GMod NAME for the same thing.
//
// GMod's Lua layer is shared between the realms, so `Entity:FrameAdvance()`
// exists on its server too - and base_nextbot's own BodyUpdate() is written
// against it (gamemodes/base/entities/entities/base_nextbot/sv_nextbot.lua:78,
// "If we're not walking or running we probably just want to update the anim
// system").  In this fork FrameAdvance() is a CLIENT-side method
// (C_BaseAnimating::FrameAdvance, bound in
// game/client/lua/lc_baseanimating.cpp:184), so a server-side nextbot script
// calling it raised "attempt to call a method 'FrameAdvance'".  The server's
// equivalent is StudioFrameAdvance().
static int CBaseAnimating_FrameAdvance (lua_State *L) {
  luaL_checkanimating(L, 1)->StudioFrameAdvance();
  return 0;
}

// HL2SB GMod compat: Entity:BecomeRagdoll( dmginfo, forceVector ).
//
// GMod's base_nextbot ends ENT:OnKilled() with `self:BecomeRagdoll( dmginfo )`
// (gamemodes/base/entities/entities/base_nextbot/sv_nextbot.lua:162), and a
// nextbot addon does the same, so the call has to exist on the SERVER.  In this
// fork only BecomeRagdollOnClient() was bound (and that is a client method);
// what a server entity has is
//
//     CBaseCombatCharacter::BecomeRagdoll( const CTakeDamageInfo &info, const Vector &forceVector )
//     (game/server/basecombatcharacter.h:302)
//
// which NextBotCombatCharacter overrides to add the ragdoll-magnet force
// (NextBot.cpp:444).  Hence the dynamic_cast: every nextbot is a combat
// character, an entity that is not one simply has nothing to ragdoll.
static int CBaseAnimating_BecomeRagdoll (lua_State *L) {
  CBaseAnimating *pEntity = luaL_checkanimating(L, 1);
  CBaseCombatCharacter *pCharacter = dynamic_cast< CBaseCombatCharacter * >( pEntity );

  if ( pCharacter == NULL )
    return 0;

  CTakeDamageInfo info;

  if ( lua_gettop(L) >= 2 && !lua_isnil(L, 2) )
    info = luaL_checkdamageinfo(L, 2);

  Vector forceVector = ( lua_gettop(L) >= 3 ) ? luaL_checkvector(L, 3 ) : vec3_origin;

  // GMod's ENTITY:BecomeRagdoll creates a SERVER-side ragdoll and removes the
  // bot.  The engine default (CBaseCombatCharacter::BecomeRagdoll) falls
  // through to BecomeRagdollOnClient() for anything that is not an HL2 NPC --
  // that path relies on the networked death flag being acted on by the CLIENT
  // entity, which C_NextBotCombatCharacter never does.  A killed Lua nextbot
  // therefore left NO corpse: the bot was removed and it simply vanished
  // (2026-09-20: SCP-096 shot with the admin gun).  Build the server ragdoll
  // here directly, the way the NPC death path does.
  CTakeDamageInfo info2 = info;
  info2.SetDamageForce( forceVector );

  CBaseEntity *pRagdoll = CreateServerRagdoll( pCharacter, 0, info2, COLLISION_GROUP_INTERACTIVE_DEBRIS, true );
  if ( pRagdoll != NULL )
  {
    // carry the death momentum over; the ragdoll spawns at rest.  A ragdoll
    // from a custom model can be MOVETYPE_VPHYSICS with NO vphysics object
    // (admin gun on a verify NPC, 2026-09-20): ApplyAbsVelocityImpulse
    // dereferences VPhysicsGetObject() unguarded on that path, so only go
    // through it when the physics object actually exists.
    Vector vecImpulse = pCharacter->GetAbsVelocity();
    IPhysicsObject *pRagdollPhys = pRagdoll->VPhysicsGetObject();
    if ( pRagdollPhys != NULL )
      pRagdollPhys->AddVelocity( &vecImpulse, NULL );
    else
      Warning( "[HL2SB] BecomeRagdoll: ragdoll for '%s' has no vphysics object, skipping momentum\n", pCharacter->GetClassname() );
  }

  // HL2SB GMod compat: the wiki's contract for NPC:BecomeRagdoll is
  // "Become a ragdoll AND REMOVE THE ENTITY", and internally it "handles
  // serverside/clientside ragdoll creation, momentum calculation, ...".
  //
  // The engine's CBaseAnimating::BecomeRagdoll() only does the first half: it
  // creates the ragdoll and hides the entity (EF_NODRAW) but leaves it in the
  // world.  A killed Lua nextbot therefore stayed behind as an INVISIBLE entity
  // that could not be removed afterwards -- exactly the "several invisible SCP-096
  // that cannot be undone" report.  Players are the exception: their entity
  // survives death and respawns, so it is left alone.
  if ( !pCharacter->IsPlayer() )
    UTIL_Remove( pCharacter );

  return 0;
}

static int CBaseAnimating_TransferDissolveFrom (lua_State *L) {
  luaL_checkanimating(L, 1)->TransferDissolveFrom(luaL_checkanimating(L, 2));
  return 0;
}

static int CBaseAnimating_UseClientSideAnimation (lua_State *L) {
  luaL_checkanimating(L, 1)->UseClientSideAnimation();
  return 0;
}

static int CBaseAnimating_VPhysicsUpdate (lua_State *L) {
  luaL_checkanimating(L, 1)->VPhysicsUpdate(luaL_checkphysicsobject(L, 2));
  return 0;
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
    // HL2SB (2026-09-20, resolves the two-session collision on this function):
    // SCRIPT-TABLE FUNCTIONS are overrides and win over C++ methods; a
    // script-table DATA (non-function) field never shadows a C++ method.
    //
    // Both halves matter and neither order alone works:
    //   * C-methods-first (the previous edit) broke every Lua override of a
    //     C++ method name: npc_scp_049.lua:107 defines ENT:GetEnemy() (nextbot
    //     enemies live in the script table as self.Enemy), but the C
    //     {"GetEnemy"} (the CAI enemy, always NULL for a nextbot) won instead,
    //     GetPos() on it answered the NULL-entity false, and Path:Compute threw
    //     once per behaviour tick -- the bot stood still and never attacked.
    //   * script-table-first (the edit before that) let scp0492base.lua:38's
    //     ENT.Health = 0 shadow {"Health"}, and self:Health() raised "attempt
    //     to call a number value" on every CheckValid().
    //
    // ⚠️ The metatable reads are lua_rawget on purpose: lua_gettable here would
    // re-enter THIS __index through the metatable's own __index field and
    // answer every script-table field with the NULL-sentinel method.
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

    luaL_getmetatable(L, "CBaseAnimating");
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

    // (3) script-table data value
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    // falls through to the legacy self-reference tail below (value or nil)
  }
  else {
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);

      /* the same CBaseAnimating-metatable step as above (literal name: this file does not
   pull in luasrclib.h) */
      luaL_getmetatable(L, "CBaseAnimating");
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

  /* HL2SB GMod compat: GMod's deprecated self-reference fields -- old addons
  ** open ENT:Initialize with self.Entity:SetModel( ... ) (mk-82_sent_he_missile,
  ** npc_scp_049.lua:98) and SWEPs spell the weapon self.Weapon.  GMod answers
  ** both with the entity itself; the nil lookup used to kill Initialize before
  ** SetModel ran, leaving the entity with no model on the client (2026-09-21).
  ** Lowest priority: only when nothing else answered. */
  if ( lua_isnil( L, -1 ) )
  {
    const char *pszKey = lua_tostring( L, 2 );

    if ( pszKey != NULL &&
         ( Q_stricmp( pszKey, "Entity" ) == 0 || Q_stricmp( pszKey, "Weapon" ) == 0 ) )
    {
      lua_pop( L, 1 );
      lua_pushvalue( L, 1 );       /* self.Entity == self, like GMod */
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
    pEntity->m_bClientSideAnimation = (bool)luaL_checkboolean(L, 3);
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


// HL2SB GMod compat: Entity:RestartGesture( activity ) -- wiki: restarts (or
// adds and starts) the gesture activity on the entity's animation overlay.
// npc_scp_049-2.lua:364 plays its attack gesture through it.  Only entities
// with an overlay can do this; others answer nil like GMod's engine does.
static int CBaseAnimating_RestartGesture (lua_State *L) {
  CBaseAnimating *pAnim = luaL_checkanimating( L, 1 );
  CBaseAnimatingOverlay *pOverlay = dynamic_cast< CBaseAnimatingOverlay * >( pAnim );
  if ( pOverlay == NULL )
    return 0;
  pOverlay->RestartGesture( (Activity)luaL_checkint( L, 2 ) );
  return 0;
}

// HL2SB GMod compat: Entity:Ignite( duration ) -- catch fire.  npc_scp_049-2
// lights its victims on fire (scp0492base.lua:798).  CBaseAnimating::Ignite
// spawns the engine's EntityFlame child, same as GMod's engine side.
static int CBaseAnimating_Ignite (lua_State *L) {
  CBaseAnimating *pAnim = luaL_checkanimating( L, 1 );
  if ( pAnim == NULL )
    return 0;
  const float flDuration = (float)luaL_optnumber( L, 2, 10.0f );
  pAnim->Ignite( flDuration, pAnim->IsNPC(), 0.0f, false );
  return 0;
}

static const luaL_Reg CBaseAnimatingmeta[] = {
  {"CalculateIKLocks", CBaseAnimating_CalculateIKLocks},
  {"RestartGesture", CBaseAnimating_RestartGesture},
  {"Ignite", CBaseAnimating_Ignite},
  {"ComputeEntitySpaceHitboxSurroundingBox", CBaseAnimating_ComputeEntitySpaceHitboxSurroundingBox},
  {"ComputeHitboxSurroundingBox", CBaseAnimating_ComputeHitboxSurroundingBox},
  {"DoMuzzleFlash", CBaseAnimating_DoMuzzleFlash},
  {"FindBodygroupByName", CBaseAnimating_FindBodygroupByName},
  {"FindTransitionSequence", CBaseAnimating_FindTransitionSequence},
  {"GetAnimTimeInterval", CBaseAnimating_GetAnimTimeInterval},
  {"GetAttachment", CBaseAnimating_GetAttachment},
  {"GetAttachmentLocal", CBaseAnimating_GetAttachmentLocal},
  {"GetBaseAnimating", CBaseAnimating_GetBaseAnimating},
  {"GetBodygroup", CBaseAnimating_GetBodygroup},
  {"GetBodygroupCount", CBaseAnimating_GetBodygroupCount},
  {"GetBodygroupName", CBaseAnimating_GetBodygroupName},
  {"GetBonePosition", CBaseAnimating_GetBonePosition},
  {"GetCycle", CBaseAnimating_GetCycle},
  {"GetFlexDescFacs", CBaseAnimating_GetFlexDescFacs},
  {"GetHitboxSet", CBaseAnimating_GetHitboxSet},
  {"GetActivity", CBaseAnimating_GetActivity},
  {"StartActivity", CBaseAnimating_StartActivity},
  {"GetHitboxSetCount", CBaseAnimating_GetHitboxSetCount},
  {"GetHitboxSetName", CBaseAnimating_GetHitboxSetName},
//  {"GetModelWidthScale", CBaseAnimating_GetModelWidthScale},
  {"GetNumBodyGroups", CBaseAnimating_GetNumBodyGroups},
  {"GetNumFlexControllers", CBaseAnimating_GetNumFlexControllers},
  {"GetPlaybackRate", CBaseAnimating_GetPlaybackRate},
  {"GetPoseParameter", CBaseAnimating_GetPoseParameter},
  {"GetPoseParameterRange", CBaseAnimating_GetPoseParameterRange},
  {"GetSequence", CBaseAnimating_GetSequence},
  {"GetSequenceActivity", CBaseAnimating_GetSequenceActivity},
  {"GetSequenceActivityName", CBaseAnimating_GetSequenceActivityName},
  {"GetSequenceGroundSpeed", CBaseAnimating_GetSequenceGroundSpeed},
  {"GetSequenceLinearMotion", CBaseAnimating_GetSequenceLinearMotion},
  {"GetSequenceName", CBaseAnimating_GetSequenceName},
  {"InvalidateBoneCache", CBaseAnimating_InvalidateBoneCache},
  {"InvalidateMdlCache", CBaseAnimating_InvalidateMdlCache},
  {"IsActivityFinished", CBaseAnimating_IsActivityFinished},
  {"IsOnFire", CBaseAnimating_IsOnFire},
  {"IsRagdoll", CBaseAnimating_IsRagdoll},
  {"IsSequenceFinished", CBaseAnimating_IsSequenceFinished},
  {"IsSequenceLooping", CBaseAnimating_IsSequenceLooping},
  {"LookupActivity", CBaseAnimating_LookupActivity},
  {"LookupAttachment", CBaseAnimating_LookupAttachment},
  {"LookupBone", CBaseAnimating_LookupBone},
  {"LookupPoseParameter", CBaseAnimating_LookupPoseParameter},
  {"LookupSequence", CBaseAnimating_LookupSequence},
  {"ResetSequence", CBaseAnimating_ResetSequence},
  {"ResetSequenceInfo", CBaseAnimating_ResetSequenceInfo},
  {"SelectWeightedSequence", CBaseAnimating_SelectWeightedSequence},
  {"SequenceDuration", CBaseAnimating_SequenceDuration},
  {"SequenceLoops", CBaseAnimating_SequenceLoops},
  {"SetBoneController", CBaseAnimating_SetBoneController},
  {"SetCycle", CBaseAnimating_SetCycle},
  {"SetHitboxSet", CBaseAnimating_SetHitboxSet},
  {"SetHitboxSetByName", CBaseAnimating_SetHitboxSetByName},
//  {"SetModelWidthScale", CBaseAnimating_SetModelWidthScale},
  {"SetPlaybackRate", CBaseAnimating_SetPlaybackRate},
  {"SetPoseParameter", CBaseAnimating_SetPoseParameter},
  {"SetSequence", CBaseAnimating_SetSequence},
  {"TranslatePhysBoneToBone", CBaseAnimating_TranslatePhysBoneToBone},
  {"StudioFrameAdvance", CBaseAnimating_StudioFrameAdvance},
  {"FrameAdvance", CBaseAnimating_FrameAdvance},
  {"BecomeRagdoll", CBaseAnimating_BecomeRagdoll},
  {"TransferDissolveFrom", CBaseAnimating_TransferDissolveFrom},
  {"UseClientSideAnimation", CBaseAnimating_UseClientSideAnimation},
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
  luaL_newmetatable(L, "CBaseAnimating");
  luaL_register(L, NULL, CBaseAnimatingmeta);
  lua_pushstring(L, "entity");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "entity" */
  lua_pop(L, 1);
  return 1;
}

