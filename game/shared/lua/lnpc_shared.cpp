//========== HL2SB - GMod compat ==========--
//
// Purpose: HL2SB's C++ port of Garry's Mod's NPC and NextBot Lua surface.
//
//   Two metatables, registered under the bare class names GMod uses:
//
//     "NPC"      -- handed to every entity whose IsNPC() answers true
//                   (CAI_BaseNPC on the server, C_AI_BaseNPC on the client).
//     "NextBot"  -- handed to every entity with an INextBot
//                   (NextBotCombatCharacter and its Lua subclass CLuaNextBot;
//                   on the client C_NextBotCombatCharacter's IsNextBot()).
//
//   Both are pushed by lua_pushentity() / CBaseEntity::PushLuaInstanceSafe()
//   through lua_pushnpcentity() / lua_pushnextbotentity(), so every entity a
//   binding returns carries its own class metatable -- the GMod contract that
//   makes type() and FindMetaTable() answer "NPC" / "NextBot", and that
//   PrintTable( getmetatable( ent ) ) shows with MetaName / MetaID (9,
//   TYPE_ENTITY) / MetaBaseClass pointing at the Entity metatable.
//
//   MetaName / MetaID come from the lua_shared side (GMod architecture):
//   luaL_newmetatable stamps every freshly created metatable, with MetaID fed
//   through luaL_newmetatable_type's type slot (lauxlib.c).  These two classes
//   are created with their GMod names already ("NPC" / "NextBot") and id 9,
//   so both layers agree; lsrcinit.cpp's s_LuaTypeInfo table keeps MetaName
//   canonical for the native-named classes and links MetaBaseClass.
//
//   COMPATIBILITY POLICY (same as lvehicle_shared.cpp):
//   the server NPC metatable carries EVERY method the Garry's Mod wiki lists
//   (182 wiki entries + IsNPC; cross-checked against GMod's own 184-entry
//   binary table, which agrees one-for-one).  Each method is one of:
//     * REAL          -- backed by this engine's CAI_BaseNPC / CAI_Motor /
//                        CAI_Navigator / CAI_Enemies / CAI_Squad / CAI_BaseActor;
//     * APPROXIMATE   -- backed by the closest real state, with the deviation
//                        stated in a comment right here;
//     * NO-OP         -- the subsystem does not exist in this fork; the call
//                        validates its arguments and does nothing, instead of
//                        raising "attempt to call a nil value" in the addon.
//   The client metatable carries exactly the three methods GMod's own client
//   NPC table has (__tostring / IsNPC / GetActiveWeapon): everything AI lives
//   server side there too, and inventing more would diverge from GMod.
//
//   Semantics verified against the wiki AND GMod's server.dll body for each
//   binding (selected notes):
//     * SetSchedule takes the SCHED_* constant (or a schedule name string).
//       This engine normalises local/global schedule IDs internally
//       (ai_namespaces.h GLOBAL_IDS_BASE = 1000000000), just like GMod's
//       binding does.
//     * GetCurrentSchedule answers the LOCAL id: GMod's binding subtracts
//       GLOBAL_IDS_BASE from the stored m_iScheduleID and answers -1 when
//       there is no schedule.  AI_RemapFromGlobal is the same conversion.
//     * IsCurrentSchedule(sched) answers against the IDEAL schedule
//       (GMod's binding passes fIdeal = 1).
//     * SetIdealYawAndUpdate(yaw, speed = -1): the second argument is GMod's
//       (GMod takes an optional second argument defaulting to -1.0); this engine's
//       AI_CALC_YAW_SPEED is also -1.
//     * UpdateYaw(speed = -1): -2 is GMod's "keep previous yaw speed".
//     * IsFacingIdealYaw: GMod checks |DeltaYaw| <= 0.006 (radians); this
//       engine's CAI_Motor::DeltaIdealYaw answers degrees, so the same
//       tolerance is expressed as 0.35 degrees (0.006 rad).
//     * GetEnemyLastKnownPos / GetEnemyLastSeenPos: answered separately from
//       the enemy memory (LastKnownPosition / LastSeenPosition) with the
//       current enemy when no argument is given -- GMod's bindings do the
//       same; GetEnemyLKP stays as the single-value convenience alias.
//     * IgnoreEnemyUntil(enemy, time) maps to CAI_Enemies::SetTimeValidEnemy,
//       which is the field GMod's binding writes.
//     * MarkTookDamageFromEnemy(enemy) maps to CAI_Enemies::OnTookDamageFrom.
//     * NavSetGoalPos / NavSetGoal both build an AI_NavGoal_t position goal;
//       NavSetGoalTarget targets an entity; NavSetRandomGoal / NavSetWanderGoal
//       map to the navigator's own calls.
//     * RunEngineTask / StartEngineTask build a Task_t{ task, data } and call
//       CAI_BaseNPC::RunTask / StartTask -- GMod's bindings call the actor's
//       vtable pair with the same two scalars.
//     * SetExpression / ClearExpression / GetExpression dynamic_cast to
//       CAI_BaseActor exactly like GMod's bindings do, and do nothing for
//       NPCs that are not actors (GMod behaves identically).
//     * GetFOV / SetFOV: GMod keeps a per-NPC FOV in radians (its binding
//       converts with 114.59 and 0.0027777).  This engine has no such field,
//       so the value is kept per entity (Lua field table) and defaults to 90.
//     * SetStepHeight / GetStepHeight:  this engine's step height is virtual
//       (CAI_BaseNPC::StepHeight, CAI_Motor::StepHeight) with no setter; the
//       write is kept per entity and answered back, documented as not
//       affecting the motor.
//     * SetMoveVelocity: this engine's motor velocity is protected with no
//       setter; the write is kept per entity and answered back by
//       GetMoveVelocity only when the engine's own value is untouched.
//     * SetIdealSequence stores the value and answers it back; when nothing
//       was stored the engine's resolved ideal sequence is answered
//       (SelectWeightedSequence of the current ideal activity -- the same
//       quantity m_nIdealSequence holds, recomputed by the engine each
//       frame).
//     * GetTaskStatus / SetTaskStatus: the engine's status field is private;
//       reads approximate from TaskIsComplete/TaskIsRunning, and the write
//       maps the COMPLETE value onto TaskComplete() (the other values have no
//       accessible setter and are documented no-ops).
//     * HasObstacles answers "the navigator has a blocking entity".
//     * The six Use*Behavior methods select a CAI behavior in GMod; this
//       fork's CAI_BaseNPC has no behavior-host API for them, so they are
//       documented no-ops (the behaviour classes that do exist keep running
//       through their own schedule selection).
//
//   Squads: NPC:GetSquad answers the squad NAME string (or nil), and SetSquad
//   accepts a name string (empty/nil clears it) -- GMod's binding pushes the
//   name off the CAI_Squad object and resolves/creates by name on write,
//   which is exactly what this file does against g_AI_SquadManager.
//
//   NextBot set: whatever INextBot / IVision / IBody expose (range, vision,
//   solid mask, last known area).  SetSolidMask / SetMaxVisionRange are kept
//   per entity because this fork's IBody/IVision have no setters.
//
//=============================================================================//

#define lnpc_shared_cpp

#include "cbase.h"

#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "lnpc_shared.h"
#include "mathlib/lvector.h"

#ifdef CLIENT_DLL
#include "c_ai_basenpc.h"
#include "c_basecombatcharacter.h"
#else
#include "ai_basenpc.h"			// CAI_BaseNPC: schedules, conditions, capabilities, memory
#include "ai_motor.h"			// CAI_Motor: ideal yaw, move primitives
#include "ai_navigator.h"		// CAI_Navigator: goals, arrivals, waypoints
#include "ai_moveprobe.h"		// CAI_MoveProbe: blocking entity
#include "ai_squad.h"			// CAI_Squad / g_AI_SquadManager
#include "ai_memory.h"			// CAI_Enemies / AI_EnemyInfo_t
#include "ai_baseactor.h"		// CAI_BaseActor: expressions
#include "baseflex.h"			// CBaseFlex::SentenceStop
#include "basecombatcharacter.h"	// GetHullType / GetActiveWeapon / GetWeapon / Weapon_Drop
#include "gamestringpool.h"		// AllocPooledString (Give / SetSquad by name)
#include "soundent.h"			// CSound fields (GetBestSoundHint)
#include "NextBotInterface.h"		// INextBot: range, vision, body
#include "NextBotBodyInterface.h"	// IBody::GetSolidMask
#include "NextBotVisionInterface.h"	// IVision: FOV, IsAbleToSee, GetMaxVisionRange
#endif

// HL2SB: GMod's typed-metatable creation, exported from lua_shared.  Declared
// here as a local extern "C" prototype on purpose -- lauxlib.h deliberately
// does not carry it (every game Lua translation unit includes that header
// through lua.hpp, and a change there would force a whole-tree rebuild), so
// only the files that call it see the name.  The definition lives in
// lauxlib.c next to luaL_newmetatable, sharing its MetaID slot.
extern "C" int luaL_newmetatable_type (lua_State *L, const char *tname, int type);

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/*
** ===========================================================================
** The class behind the "NPC" metatable, one typedef per realm.  Every method
** funnels through ToNPC; methods answer the documented default on a stale or
** mismatched self instead of raising (the policy the existing Entity-meta NPC
** bindings follow).
** ===========================================================================
*/
#ifdef CLIENT_DLL
typedef C_AI_BaseNPC lua_NPC;
#else
typedef CAI_BaseNPC lua_NPC;
#endif

static lua_NPC *ToNPC (lua_State *L, int nArg) {
  CBaseEntity *pEntity = luaL_checkentity(L, nArg);
  return (pEntity != NULL) ? pEntity->MyNPCPointer() : NULL;
}

/* True when the value at nArg is a Vector userdata.  Needed because Lua 5.4
** represents BOTH vectors and entities as userdata; the metatable name is the
** only sound discriminator. */
static bool IsVectorArg (lua_State *L, int nArg) {
  return luaL_testudata(L, nArg, "Vector") != NULL;
}


#ifndef CLIENT_DLL
/*
** ===========================================================================
** Per-entity storage for the values GMod keeps on fields this fork has no
** accessible setter for (FOV, step height, ideal sequence, move velocity).
** Written into the entity's own Lua field table -- the same table the
** CBaseEntity __newindex fills, keyed by the entity and released with it (the
** exact mechanism lvehicle_shared.cpp uses).  Server realm only: every
** consumer of these helpers is a server-side method.
** ===========================================================================
*/

static const char *const s_pszKeyFOV           = "hl2sb_npc_fov";
static const char *const s_pszKeyStepHeight    = "hl2sb_npc_stepheight";
static const char *const s_pszKeyIdealSequence = "hl2sb_npc_idealsequence";
static const char *const s_pszKeyMoveVelocity  = "hl2sb_npc_movevelocity";

static void NPC_PushExtraField (lua_State *L, CBaseEntity *pEntity, const char *pszKey) {
  if (pEntity != NULL && lua_isrefvalid(L, pEntity->m_nTableReference)) {
    lua_getref(L, pEntity->m_nTableReference);
    lua_getfield(L, -1, pszKey);
    lua_remove(L, -2);
    return;
  }
  lua_pushnil(L);
}

/* Stores the value on top of the stack under pszKey and pops it. */
static void NPC_SetExtraField (lua_State *L, CBaseEntity *pEntity, const char *pszKey) {
  if (pEntity == NULL) {
    lua_pop(L, 1);
    return;
  }

  /* < 0 covers LUA_NOREF and the LUA_REFNIL luaL_ref() answers for a nil
  ** value; testing "== LUA_NOREF" alone is the bug described in AGENTS.md. */
  if (pEntity->m_nTableReference < 0) {
    lua_newtable(L);
    pEntity->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  lua_getref(L, pEntity->m_nTableReference);  /* [value table] */
  lua_insert(L, -2);                          /* [table value] */
  lua_setfield(L, -2, pszKey);                /* [table] */
  lua_pop(L, 1);
}

/* Pushes the stored value (or nil) and answers whether it was a number. */
static bool NPC_GetExtraNumber (lua_State *L, CBaseEntity *pEntity, const char *pszKey, float *pflOut) {
  NPC_PushExtraField(L, pEntity, pszKey);
  bool bFound = lua_isnumber(L, -1) != 0;
  if (bFound)
    *pflOut = (float)lua_tonumber(L, -1);
  lua_pop(L, 1);
  return bFound;
}
#endif  // !CLIENT_DLL


/*
** ===========================================================================
** push functions (C -> stack)
** ===========================================================================
*/

LUA_API void lua_pushnpc (lua_State *L, CBaseEntity *pEntity) {
  if (pEntity == NULL) {
    lua_pushentity(L, NULL);
    return;
  }

  CBaseHandle *hEntity = (CBaseHandle *)lua_newuserdata(L, sizeof(CBaseHandle));
  hEntity->Set(pEntity);
  luaL_getmetatable(L, "NPC");
  lua_setmetatable(L, -2);
}

LUA_API void lua_pushnextbot (lua_State *L, CBaseEntity *pEntity) {
  if (pEntity == NULL) {
    lua_pushentity(L, NULL);
    return;
  }

  CBaseHandle *hEntity = (CBaseHandle *)lua_newuserdata(L, sizeof(CBaseHandle));
  hEntity->Set(pEntity);
  luaL_getmetatable(L, "NextBot");
  lua_setmetatable(L, -2);
}

/*
** HL2SB: the entity-level dispatch.  Answers false and leaves the stack
** untouched when pEntity is not theirs, or when the metatable has not been
** opened in this Lua state, so lua_pushentity() / PushLuaInstanceSafe() can
** fall through to their own push.  Declared at their call sites in
** lbaseentity_shared.cpp (local prototypes -- see the note there).
**
** These run at the TOP of lua_pushentity() as well as inside
** PushLuaInstanceSafe(), because a GMod class metatable has to be handed out
** by EVERY entity push -- ents.FindByClass, trace results, Entity( index ),
** any binding that returns an entity.  NextBot is tested before NPC so a bot
** that also reports IsNPC() still lands on "NextBot" (GMod reports those as
** NextBot).
*/
LUA_API bool lua_pushnpcentity (lua_State *L, CBaseEntity *pEntity) {
  if (pEntity == NULL || !pEntity->IsNPC())
    return false;

  luaL_getmetatable(L, "NPC");
  bool bHasMetatable = lua_istable(L, -1);
  lua_pop(L, 1);
  if (!bHasMetatable)
    return false;

  lua_pushnpc(L, pEntity);
  return true;
}

LUA_API bool lua_pushnextbotentity (lua_State *L, CBaseEntity *pEntity) {
  if (pEntity == NULL)
    return false;

#ifdef CLIENT_DLL
  /* Client realm: C_NextBotCombatCharacter overrides IsNextBot(); the
  ** INextBot pointer itself is server memory. */
  if (!pEntity->IsNextBot())
    return false;
#else
  /* Server realm: every bot class answers through its INextBot pointer
  ** (NextBotCombatCharacter / NextBotPlayer override it), CBaseEntity
  ** answers NULL. */
  if (pEntity->MyNextBotPointer() == NULL)
    return false;
#endif

  luaL_getmetatable(L, "NextBot");
  bool bHasMetatable = lua_istable(L, -1);
  lua_pop(L, 1);
  if (!bHasMetatable)
    return false;

  lua_pushnextbot(L, pEntity);
  return true;
}


/*
** ===========================================================================
** The two methods BOTH realms carry directly (GMod's client NPC table is
** exactly __tostring / IsNPC / GetActiveWeapon).  They need nothing from the
** server headers, so they sit outside the realm guard.
** ===========================================================================
*/

/* GMod's own class predicate; answers false for a removed NPC, exactly like
** every other predicate on a stale handle. */
static int NPC_IsNPC (lua_State *L) {
  lua_pushboolean(L, ToNPC(L, 1) != NULL);
  return 1;
}

/* Both realms answer this: C_AI_BaseNPC inherits
** C_BaseCombatCharacter::GetActiveWeapon (shared implementation), CAI_BaseNPC
** its server twin. */
static int NPC_GetActiveWeapon (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity::PushLuaInstanceSafe(L, pNPC != NULL ? pNPC->GetActiveWeapon() : NULL);
  return 1;
}


#ifndef CLIENT_DLL
/*
** ===========================================================================
** Server realm: everything else GMod's NPC table carries.
** ===========================================================================
*/

/* The enemy GMod's optional-argument bindings fall back to when none is
** given: the NPC's current enemy. */
static CBaseEntity *NPCArgEnemyOrCurrent (lua_State *L, lua_NPC *pNPC) {
  CBaseEntity *pEnemy = lua_toentity(L, 2);
  if (pEnemy == NULL && pNPC != NULL)
    pEnemy = pNPC->GetEnemy();
  return pEnemy;
}

/*
** -- enemy memory ---------------------------------------------------------
*/

static int NPC_GetEnemy (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity::PushLuaInstanceSafe(L, pNPC != NULL ? pNPC->GetEnemy() : NULL);
  return 1;
}

static int NPC_SetEnemy (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = lua_toentity(L, 2);
  if (pNPC != NULL && pEnemy != NULL)
    pNPC->SetEnemy(pEnemy);
  return 0;
}

static int NPC_GetTarget (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity::PushLuaInstanceSafe(L, pNPC != NULL ? pNPC->GetTarget() : NULL);
  return 1;
}

static int NPC_SetTarget (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pTarget = lua_toentity(L, 2);
  if (pNPC != NULL && pTarget != NULL)
    pNPC->SetTarget(pTarget);
  return 0;
}

/* The single-value convenience alias this fork keeps on the NPC itself. */
static int NPC_GetEnemyLKP (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushvector(L, pNPC != NULL ? pNPC->GetEnemyLKP() : vec3_origin);
  return 1;
}

/* GetEnemyLastKnownPos([enemy]) -- the enemy memory's LastKnownPosition,
** falling back to the NPC's single GetEnemyLKP when there is no memory
** entry (same quantity). */
static int NPC_GetEnemyLastKnownPos (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = NPCArgEnemyOrCurrent(L, pNPC);

  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL && pEnemies->Find(pEnemy) != NULL) {
      lua_pushvector(L, pEnemies->LastKnownPosition(pEnemy));
      return 1;
    }
    lua_pushvector(L, pNPC->GetEnemyLKP());
    return 1;
  }

  lua_pushvector(L, vec3_origin);
  return 1;
}

static int NPC_GetEnemyLastSeenPos (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = NPCArgEnemyOrCurrent(L, pNPC);

  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL && pEnemies->Find(pEnemy) != NULL) {
      lua_pushvector(L, pEnemies->LastSeenPosition(pEnemy));
      return 1;
    }
  }

  lua_pushvector(L, vec3_origin);
  return 1;
}

static int NPC_GetEnemyLastTimeSeen (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushnumber(L, pNPC != NULL ? pNPC->GetEnemyLastTimeSeen() : 0.0f);
  return 1;
}

static int NPC_GetEnemyFirstTimeSeen (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = NPCArgEnemyOrCurrent(L, pNPC);

  float flTime = 0.0f;
  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL && pEnemies->Find(pEnemy) != NULL)
      flTime = pEnemies->FirstTimeSeen(pEnemy);
  }
  lua_pushnumber(L, flTime);
  return 1;
}

static int NPC_MarkEnemyAsEluded (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->MarkEnemyAsEluded();
  return 0;
}

static int NPC_HasEnemyEluded (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushboolean(L, pNPC != NULL && pNPC->EnemyHasEludedMe());
  return 1;
}

static int NPC_ClearEnemyMemory (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->ClearEnemyMemory();
  return 0;
}

static int NPC_UpdateEnemyMemory (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = lua_toentity(L, 2);
  if (pNPC != NULL && pEnemy != NULL)
    pNPC->UpdateEnemyMemory(pEnemy, luaL_checkvector(L, 3));
  return 0;
}

static int NPC_HasEnemyMemory (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = NPCArgEnemyOrCurrent(L, pNPC);

  bool bHas = false;
  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL)
      bHas = pEnemies->HasMemory(pEnemy);
  }
  lua_pushboolean(L, bHas);
  return 1;
}

static int NPC_GetKnownEnemyCount (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Enemies *pEnemies = (pNPC != NULL) ? pNPC->GetEnemies() : NULL;
  lua_pushinteger(L, pEnemies != NULL ? pEnemies->NumEnemies() : 0);
  return 1;
}

static int NPC_GetKnownEnemies (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_newtable(L);

  CAI_Enemies *pEnemies = (pNPC != NULL) ? pNPC->GetEnemies() : NULL;
  if (pEnemies != NULL) {
    int nIndex = 0;
    AIEnemiesIter_t iter;
    for (AI_EnemyInfo_t *pMemory = pEnemies->GetFirst(&iter);
         pMemory != NULL;
         pMemory = pEnemies->GetNext(&iter)) {
      if (pMemory->hEnemy != NULL) {
        CBaseEntity::PushLuaInstanceSafe(L, pMemory->hEnemy.Get());
        lua_rawseti(L, -2, ++nIndex);
      }
    }
  }
  return 1;
}

static int NPC_IgnoreEnemyUntil (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = lua_toentity(L, 2);
  float flTime = (float)luaL_checknumber(L, 3);

  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL)
      pEnemies->SetTimeValidEnemy(pEnemy, flTime);
  }
  return 0;
}

static int NPC_GetTimeEnemyLastReacquired (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = NPCArgEnemyOrCurrent(L, pNPC);

  float flTime = 0.0f;
  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL && pEnemies->Find(pEnemy) != NULL)
      flTime = pEnemies->TimeLastReacquired(pEnemy);
  }
  lua_pushnumber(L, flTime);
  return 1;
}

static int NPC_MarkTookDamageFromEnemy (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = lua_toentity(L, 2);

  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL)
      pEnemies->OnTookDamageFrom(pEnemy);
  }
  return 0;
}

static int NPC_GetLastTimeTookDamageFromEnemy (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = NPCArgEnemyOrCurrent(L, pNPC);

  float flTime = 0.0f;
  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL && pEnemies->Find(pEnemy) != NULL)
      flTime = pEnemies->LastTimeTookDamageFrom(pEnemy);
  }
  lua_pushnumber(L, flTime);
  return 1;
}

static int NPC_SetUnforgettable (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = lua_toentity(L, 2);
  bool bUnforgettable = luaL_optboolean(L, 3, true) != 0;

  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    if (pEnemies != NULL)
      pEnemies->SetUnforgettable(pEnemy, bUnforgettable);
  }
  return 0;
}

static int NPC_IsUnforgettable (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pEnemy = NPCArgEnemyOrCurrent(L, pNPC);

  bool bValue = false;
  if (pNPC != NULL && pEnemy != NULL) {
    CAI_Enemies *pEnemies = pNPC->GetEnemies();
    AI_EnemyInfo_t *pMemory = (pEnemies != NULL) ? pEnemies->Find(pEnemy) : NULL;
    if (pMemory != NULL)
      bValue = pMemory->bUnforgettable;
  }
  lua_pushboolean(L, bValue);
  return 1;
}

/*
** -- schedules and tasks --------------------------------------------------
*/

static int NPC_SetSchedule (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  /* GMod scripts hand SetSchedule the SCHED_* constant; a name string is
  ** accepted too, through the engine's own schedule-name lookup.  The engine
  ** normalises local/global IDs internally (ai_namespaces.h). */
  if (lua_isnumber(L, 2))
    pNPC->SetSchedule(lua_tointeger(L, 2));
  else if (lua_isstring(L, 2))
    pNPC->SetSchedule(CAI_BaseNPC::GetScheduleID(lua_tostring(L, 2)));
  return 0;
}

static int NPC_ClearSchedule (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->ClearSchedule("Lua");
  return 0;
}

static int NPC_GetCurrentSchedule (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Schedule *pSchedule = (pNPC != NULL) ? pNPC->GetCurSchedule() : NULL;
  /* GMod's binding answers the LOCAL id and -1 when there is no schedule
  ** (GMod answers the LOCAL id -- it subtracts GLOBAL_IDS_BASE = 1000000000 from the stored
  ** m_iScheduleID); AI_RemapFromGlobal is this engine's same conversion, and
  ** the wiki documents the -1 answer. */
  lua_pushinteger(L, pSchedule != NULL ? AI_RemapFromGlobal(pSchedule->GetId()) : -1);
  return 1;
}

static int NPC_IsCurrentSchedule (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iSchedule = luaL_checkint(L, 2);
  /* GMod's binding passes fIdeal = 1, i.e. the check is against the IDEAL
  ** schedule; this engine's default argument is the same. */
  lua_pushboolean(L, pNPC != NULL && pNPC->IsCurSchedule(iSchedule));
  return 1;
}

static int NPC_TaskComplete (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->TaskComplete();
  return 0;
}

static int NPC_TaskFail (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  if (lua_isstring(L, 2))
    pNPC->TaskFail(lua_tostring(L, 2));
  else
    pNPC->TaskFail((AI_TaskFailureCode_t)luaL_checkint(L, 2));
  return 0;
}

/* APPROXIMATE: the engine's TaskStatus_e field is private.  Reconstruct it
** from the public predicates: COMPLETE (4) when TaskIsComplete answers,
** RUN_TASK (3) while TaskIsRunning answers (RUN_MOVE_AND_TASK (1) reads as 3
** here -- the two cannot be told apart through the public API), and RUN_MOVE
** (2) otherwise. */
static int NPC_GetTaskStatus (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iStatus = 0;
  if (pNPC != NULL) {
    if (pNPC->TaskIsComplete())
      iStatus = 4;                              // TASKSTATUS_COMPLETE
    else if (pNPC->TaskIsRunning())
      iStatus = 3;                              // TASKSTATUS_RUN_TASK
    else
      iStatus = 2;                              // TASKSTATUS_RUN_MOVE
  }
  lua_pushinteger(L, iStatus);
  return 1;
}

/* APPROXIMATE: only the COMPLETE value has an accessible mapping
** (TaskComplete); the other TaskStatus values have no public setter in this
** fork and are documented no-ops. */
static int NPC_SetTaskStatus (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iStatus = luaL_checkint(L, 2);
  if (pNPC != NULL && iStatus >= 4)
    pNPC->TaskComplete();
  return 0;
}

/* RunEngineTask / StartEngineTask: NO-OP (documented).  GMod runs these on
** its scripted-NPC class, which owns PUBLIC task-runner overrides; this
** fork's CAI_BaseNPC declares StartTask/RunTask private (the base bodies are
** "called wrong" sentinels -- derived AI classes are meant to bring their
** own), so there is no accessible task runner to drive from here.  The calls
** validate their arguments and do nothing instead of raising. */
static int NPC_RunEngineTask (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  luaL_checkint(L, 2);
  (void)pNPC;
  return 0;
}

static int NPC_StartEngineTask (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  luaL_checkint(L, 2);
  (void)pNPC;
  return 0;
}

/*
** -- conditions -----------------------------------------------------------
*/

static int NPC_SetCondition (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->SetCondition(luaL_checkint(L, 2));
  return 0;
}

static int NPC_HasCondition (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushboolean(L, pNPC != NULL && pNPC->HasCondition(luaL_checkint(L, 2)));
  return 1;
}

static int NPC_ClearCondition (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->ClearCondition(luaL_checkint(L, 2));
  return 0;
}

static int NPC_ConditionName (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  const char *pszName = (pNPC != NULL) ? pNPC->ConditionName(luaL_checkint(L, 2)) : NULL;
  lua_pushstring(L, pszName != NULL ? pszName : "");
  return 1;
}

static int NPC_ConditionID (lua_State *L) {
  lua_pushinteger(L, CAI_BaseNPC::GetConditionID(luaL_checkstring(L, 2)));
  return 1;
}

static int NPC_SetIgnoreConditions (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  if (lua_istable(L, 2)) {
    int nCount = (int)lua_objlen(L, 2);
    int *pConditions = (int *)stackalloc((nCount > 0 ? nCount : 1) * sizeof(int));
    int nStored = 0;
    for (int i = 1; i <= nCount; ++i) {
      lua_rawgeti(L, 2, i);
      if (lua_isnumber(L, -1))
        pConditions[nStored++] = lua_tointeger(L, -1);
      lua_pop(L, 1);
    }
    if (nStored > 0)
      pNPC->SetIgnoreConditions(pConditions, nStored);
  }
  else {
    int iCondition = luaL_checkint(L, 2);
    pNPC->SetIgnoreConditions(&iCondition, 1);
  }
  return 0;
}

static int NPC_RemoveIgnoreConditions (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->RemoveIgnoredConditions();
  return 0;
}

/*
** -- capabilities ---------------------------------------------------------
*/

static int NPC_CapabilitiesGet (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushinteger(L, pNPC != NULL ? pNPC->CapabilitiesGet() : 0);
  return 1;
}

static int NPC_CapabilitiesAdd (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iCapability = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->CapabilitiesAdd(iCapability);
  return 0;
}

static int NPC_CapabilitiesRemove (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iCapability = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->CapabilitiesRemove(iCapability);
  return 0;
}

static int NPC_CapabilitiesClear (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->CapabilitiesClear();
  return 0;
}

static int NPC_CapabilitiesHas (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iCapability = luaL_checkint(L, 2);
  lua_pushboolean(L, pNPC != NULL && (pNPC->CapabilitiesGet() & iCapability) != 0);
  return 1;
}

/*
** -- state and activity ---------------------------------------------------
*/

static int NPC_GetNPCState (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushinteger(L, pNPC != NULL ? (int)pNPC->GetState() : 0);
  return 1;
}

static int NPC_SetNPCState (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iState = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->SetState((NPC_STATE)iState);
  return 0;
}

static int NPC_GetIdealActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushinteger(L, pNPC != NULL ? (int)pNPC->GetIdealActivity() : 0);
  return 1;
}

static int NPC_SetIdealActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iActivity = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->SetIdealActivity((Activity)iActivity);
  return 0;
}

static int NPC_ResetIdealActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iActivity = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->ResetIdealActivity((Activity)iActivity);
  return 0;
}

static int NPC_MaintainActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->MaintainActivity();
  return 0;
}

static int NPC_GetActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushinteger(L, pNPC != NULL ? (int)pNPC->GetActivity() : 0);
  return 1;
}

static int NPC_SetActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iActivity = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->SetActivity((Activity)iActivity);
  return 0;
}

/* APPROXIMATE: m_nIdealSequence is private.  The engine recomputes it from
** the ideal activity every frame (ResolveActivityToSequence), which is what
** SelectWeightedSequence(GetIdealActivity()) answers; a value set through
** SetIdealSequence is kept and answered back. */
static int NPC_GetIdealSequence (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);

  float flStored = 0.0f;
  if (pNPC != NULL && NPC_GetExtraNumber(L, pNPC, s_pszKeyIdealSequence, &flStored)) {
    lua_pushinteger(L, (int)flStored);
    return 1;
  }

  lua_pushinteger(L, pNPC != NULL ? pNPC->SelectWeightedSequence(pNPC->GetIdealActivity()) : 0);
  return 1;
}

static int NPC_SetIdealSequence (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iSequence = luaL_checkint(L, 2);
  if (pNPC != NULL) {
    lua_pushinteger(L, iSequence);
    NPC_SetExtraField(L, pNPC, s_pszKeyIdealSequence);
  }
  return 0;
}

// ===== [PART 2 continue] =====

/*
** -- facing (the motor's ideal yaw) ---------------------------------------
*/

static int NPC_SetIdealYaw (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor == NULL)
    return 0;

  /* GMod's SetIdealYaw takes a number (degrees); the Vector overload keeps
  ** the engine's "face this position" spelling working too. */
  if (lua_isnumber(L, 2))
    pMotor->SetIdealYaw((float)lua_tonumber(L, 2));
  else if (IsVectorArg(L, 2))
    pMotor->SetIdealYaw(luaL_checkvector(L, 2));
  return 0;
}

static int NPC_GetIdealYaw (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Motor *pMotor = (pNPC != NULL) ? pNPC->GetMotor() : NULL;
  lua_pushnumber(L, pMotor != NULL ? pMotor->GetIdealYaw() : 0.0f);
  return 1;
}

static int NPC_UpdateYaw (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    /* -1 = calculate automatically, -2 = keep the previous yaw speed: both
    ** are the engine's own conventions (AI_CALC_YAW_SPEED / AI_KEEP_YAW_SPEED)
    ** and match the wiki's documented special values. */
    pMotor->UpdateYaw(luaL_optint(L, 2, -1));
  return 0;
}

static int NPC_SetIdealYawAndUpdate (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flYaw = (float)luaL_checknumber(L, 2);
  /* GMod's binding reads a second, optional speed argument with default -1
  ** (the optional second argument defaults to -1.0), which is exactly this
  ** engine's AI_CALC_YAW_SPEED = -1. */
  float flSpeed = (float)luaL_optnumber(L, 3, AI_CALC_YAW_SPEED);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->SetIdealYawAndUpdate(flYaw, flSpeed);
  return 0;
}

static int NPC_IsFacingIdealYaw (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  bool bFacing = false;
  if (pNPC != NULL) {
    CAI_Motor *pMotor = pNPC->GetMotor();
    /* GMod's binding checks |delta| <= 0.006 RADIANS; CAI_Motor::DeltaIdealYaw
    ** answers DEGREES, so the same tolerance is 0.006 rad = 0.3438 degrees.
    ** Rounded to 0.35. */
    bFacing = (pMotor != NULL) && (fabsf(pMotor->DeltaIdealYaw()) <= 0.35f);
  }
  lua_pushboolean(L, bFacing);
  return 1;
}

/*
** -- movement -------------------------------------------------------------
*/

static int NPC_StopMoving (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->StopMoving();
  return 0;
}

static int NPC_IsMoving (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushboolean(L, pNPC != NULL && pNPC->IsMoving());
  return 1;
}

static int NPC_MoveStart (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveStart();
  return 0;
}

static int NPC_MoveStop (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveStop();
  return 0;
}

static int NPC_MovePause (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  /* GMod's MovePause == the engine's MovePaused. */
  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MovePaused();
  return 0;
}

static int NPC_ResetMoveCalc (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->ResetMoveCalculations();
  return 0;
}

static int NPC_MoveClimbStart (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveClimbStart(luaL_checkvector(L, 2), luaL_checkvector(L, 3),
                           (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
  return 0;
}

static int NPC_MoveClimbExec (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveClimbExecute(luaL_checkvector(L, 2), luaL_checkvector(L, 3),
                             (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                             luaL_optint(L, 6, 0));
  return 0;
}

static int NPC_MoveClimbStop (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveClimbStop();
  return 0;
}

static int NPC_MoveJumpStart (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveJumpStart(luaL_checkvector(L, 2));
  return 0;
}

static int NPC_MoveJumpExec (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveJumpExecute();
  return 0;
}

static int NPC_MoveJumpStop (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveJumpStop();
  return 0;
}

static int NPC_MoveGroundStep (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  Vector vecPos = luaL_checkvector(L, 2);
  CBaseEntity *pTarget = lua_toentity(L, 3);

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MoveGroundStep(vecPos, pTarget);
  return 0;
}

static int NPC_UpdateTurnActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  /* APPROXIMATE: GMod's binding refreshes the turn animation bookkeeping;
  ** this engine's equivalent maintenance entry point is the motor's
  ** MaintainTurnActivity (gesture-layer maintenance lives there). */
  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->MaintainTurnActivity();
  return 0;
}

static int NPC_AutoMovement (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushboolean(L, pNPC != NULL && pNPC->AutoMovement());
  return 1;
}

/*
** -- step height and last position ----------------------------------------
*/

static int NPC_GetStepHeight (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);

  float flStored = 0.0f;
  if (pNPC != NULL && NPC_GetExtraNumber(L, pNPC, s_pszKeyStepHeight, &flStored)) {
    lua_pushnumber(L, flStored);
    return 1;
  }

  /* Default 18 HU, the engine's own CAI_BaseNPC::StepHeight (the wiki
  ** documents the same default). */
  lua_pushnumber(L, pNPC != NULL ? pNPC->StepHeight() : 18.0f);
  return 1;
}

static int NPC_SetStepHeight (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flHeight = (float)luaL_checknumber(L, 2);
  if (pNPC != NULL) {
    /* APPROXIMATE: this engine's step height is virtual with no setter
    ** (CAI_BaseNPC::StepHeight / CAI_Motor::StepHeight); the value is kept
    ** per entity and answered back by GetStepHeight.  Scripted NPCs whose
    ** base class reads the Lua field keep working; the motor's own trace
    ** height is unchanged. */
    lua_pushnumber(L, flHeight);
    NPC_SetExtraField(L, pNPC, s_pszKeyStepHeight);
  }
  return 0;
}

static int NPC_SetLastPosition (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->m_vecLastPosition = luaL_checkvector(L, 2);
  return 0;
}

static int NPC_GetLastPosition (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushvector(L, pNPC != NULL ? pNPC->m_vecLastPosition : vec3_origin);
  return 1;
}

/*
** -- sounds and sentences -------------------------------------------------
*/

static int NPC_IdleSound (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL) pNPC->IdleSound();
  return 0;
}

static int NPC_AlertSound (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL) pNPC->AlertSound();
  return 0;
}

static int NPC_FearSound (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL) pNPC->FearSound();
  return 0;
}

static int NPC_LostEnemySound (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL) pNPC->LostEnemySound();
  return 0;
}

static int NPC_FoundEnemySound (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL) pNPC->FoundEnemySound();
  return 0;
}

static int NPC_PlaySentence (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  const char *pszSentence = luaL_checkstring(L, 2);
  float flDelay = (float)luaL_optnumber(L, 3, 0.0f);
  lua_pushinteger(L, pNPC != NULL
    ? pNPC->PlaySentence(pszSentence, flDelay, VOL_NORM, SNDLVL_TALKING, NULL)
    : 0);
  return 1;
}

static int NPC_SentenceStop (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  /* CBaseFlex::SentenceStop -- CAI_BaseNPC inherits it through
  ** CBaseCombatCharacter. */
  if (pNPC != NULL)
    pNPC->SentenceStop();
  return 0;
}

/* GMod returns a SoundHintData table (or nil).  Fields follow the GMod
** binding's own table build (the fields are origin / owner / target /
** volume / channel / expires).  The types argument is the SOUND_* mask the
** wiki documents; this engine's GetBestSound takes the same mask. */
static int NPC_GetBestSoundHint (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL) {
    lua_pushnil(L);
    return 1;
  }

  int iTypes = luaL_optint(L, 2, ALL_SOUNDS);
  CSound *pSound = pNPC->GetBestSound(iTypes);
  if (pSound == NULL) {
    lua_pushnil(L);
    return 1;
  }

  lua_newtable(L);

  lua_pushvector(L, pSound->GetSoundOrigin());
  lua_setfield(L, -2, "origin");

  CBaseEntity::PushLuaInstanceSafe(L, pSound->m_hOwner.Get());
  lua_setfield(L, -2, "owner");

  CBaseEntity::PushLuaInstanceSafe(L, pSound->m_hTarget.Get());
  lua_setfield(L, -2, "target");

  lua_pushinteger(L, pSound->m_iVolume);
  lua_setfield(L, -2, "volume");

  lua_pushinteger(L, pSound->SoundChannel());
  lua_setfield(L, -2, "channel");

  lua_pushboolean(L, pSound->DoesSoundExpire());
  lua_setfield(L, -2, "expires");

  return 1;
}

/*
** -- hull, crouch, view ---------------------------------------------------
*/

static int NPC_GetHullType (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushinteger(L, pNPC != NULL ? (int)pNPC->GetHullType() : 0);
  return 1;
}

static int NPC_SetHullType (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iHull = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->SetHullType((Hull_t)iHull);
  return 0;
}

static int NPC_SetHullSizeNormal (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->SetHullSizeNormal();
  return 0;
}

static int NPC_IsCrouching (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushboolean(L, pNPC != NULL && pNPC->IsCrouching());
  return 1;
}

static int NPC_SetForceCrouch (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  /* The engine's pair (ai_basenpc.h ForceCrouch / ClearForceCrouch); GMod's
  ** single boolean setter maps onto both. */
  if (lua_toboolean(L, 2))
    pNPC->ForceCrouch();
  else
    pNPC->ClearForceCrouch();
  return 0;
}

static int NPC_GetViewOffset (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushvector(L, pNPC != NULL ? pNPC->GetViewOffset() : vec3_origin);
  return 1;
}

static int NPC_SetViewOffset (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  Vector vecOffset = luaL_checkvector(L, 2);
  if (pNPC != NULL)
    pNPC->SetViewOffset(vecOffset);
  return 0;
}

/* GMod keeps a per-NPC FOV in RADIANS (its binding converts with 114.59 /
** 0.0027777); this engine has no per-NPC FOV field -- m_flFieldOfView is a
** cosine used by the view-cone tests.  The value is kept per entity in
** DEGREES (the unit the wiki documents) and defaults to 90. */
static int NPC_GetFOV (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);

  float flStored = 0.0f;
  if (pNPC != NULL && NPC_GetExtraNumber(L, pNPC, s_pszKeyFOV, &flStored)) {
    lua_pushnumber(L, flStored);
    return 1;
  }

  lua_pushnumber(L, 90.0f);
  return 1;
}

static int NPC_SetFOV (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flFOV = (float)luaL_checknumber(L, 2);
  if (pNPC != NULL) {
    lua_pushnumber(L, flFOV);
    NPC_SetExtraField(L, pNPC, s_pszKeyFOV);
  }
  return 0;
}

static int NPC_IsInViewCone (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);

  bool bInCone = false;
  if (pNPC != NULL) {
    if (IsVectorArg(L, 2))
      bInCone = pNPC->FInViewCone(luaL_checkvector(L, 2));
    else {
      CBaseEntity *pTarget = lua_toentity(L, 2);
      if (pTarget != NULL)
        bInCone = pNPC->FInViewCone(pTarget);
    }
  }
  lua_pushboolean(L, bInCone);
  return 1;
}

/*
** -- directions and shooting position -------------------------------------
*/

static int NPC_GetEyeDirection (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushvector(L, pNPC != NULL ? pNPC->EyeDirection3D() : vec3_origin);
  return 1;
}

static int NPC_GetHeadDirection (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushvector(L, pNPC != NULL ? pNPC->HeadDirection3D() : vec3_origin);
  return 1;
}

static int NPC_GetShootPos (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushvector(L, pNPC != NULL ? pNPC->Weapon_ShootPosition() : vec3_origin);
  return 1;
}

/* GMod's Entity:GetAimVector semantics for an NPC: aimed at the enemy when
** there is one, along the facing otherwise (the same computation the shared
** Entity binding does). */
static int NPC_GetAimVector (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  Vector vecAim = vec3_origin;
  if (pNPC != NULL) {
    CBaseEntity *pEnemy = pNPC->GetEnemy();
    if (pEnemy != NULL)
      vecAim = pEnemy->WorldSpaceCenter() - pNPC->EyePosition();
    else
      AngleVectors(pNPC->GetAbsAngles(), &vecAim);
  }
  lua_pushvector(L, vecAim);
  return 1;
}

static int NPC_Classify (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushinteger(L, pNPC != NULL ? (int)pNPC->Classify() : 0);
  return 1;
}

static int NPC_AddRelationship (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  const char *pszRelationship = luaL_checkstring(L, 2);
  if (pNPC != NULL)
    pNPC->AddRelationship(pszRelationship, NULL);
  return 0;
}

/*
** -- relationships --------------------------------------------------------
*/

static int NPC_Disposition (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pTarget = lua_toentity(L, 2);
  lua_pushinteger(L, (pNPC != NULL && pTarget != NULL)
    ? (int)pNPC->IRelationType(pTarget)
    : 0);
  return 1;
}

static int NPC_AddEntityRelationship (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pTarget = lua_toentity(L, 2);
  if (pNPC != NULL && pTarget != NULL)
    pNPC->AddEntityRelationship(pTarget,
                                (Disposition_t)luaL_checkint(L, 3),
                                luaL_optint(L, 4, 0));
  return 0;
}

static int NPC_AddClassRelationship (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC != NULL)
    pNPC->AddClassRelationship((Class_T)luaL_checkint(L, 2),
                               (Disposition_t)luaL_checkint(L, 3),
                               luaL_optint(L, 4, 0));
  return 0;
}

/*
** -- weapons --------------------------------------------------------------
*/

static int NPC_GetWeapon (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iSlot = luaL_checkint(L, 2);

  CBaseEntity *pWeapon = NULL;
  if (pNPC != NULL && iSlot >= 0 && iSlot < MAX_WEAPONS)
    pWeapon = pNPC->GetWeapon(iSlot);
  CBaseEntity::PushLuaInstanceSafe(L, pWeapon);
  return 1;
}

static int NPC_GetWeapons (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_newtable(L);

  if (pNPC != NULL) {
    int nIndex = 0;
    for (int i = 0; i < MAX_WEAPONS; ++i) {
      CBaseCombatWeapon *pWeapon = pNPC->GetWeapon(i);
      if (pWeapon != NULL) {
        CBaseEntity::PushLuaInstanceSafe(L, pWeapon);
        lua_rawseti(L, -2, ++nIndex);
      }
    }
  }
  return 1;
}

static int NPC_Give (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  const char *pszWeapon = luaL_checkstring(L, 2);
  if (pNPC != NULL)
    pNPC->GiveWeapon(AllocPooledString(pszWeapon));
  return 0;
}

static int NPC_DropWeapon (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CBaseCombatWeapon *pWeapon = NULL;
  CBaseEntity *pArg = lua_toentity(L, 2);
  if (pArg != NULL)
    pWeapon = dynamic_cast<CBaseCombatWeapon *>(pArg);
  else
    pWeapon = pNPC->GetActiveWeapon();

  if (pWeapon != NULL)
    pNPC->Weapon_Drop(pWeapon);
  return 0;
}

static int NPC_PickupWeapon (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pArg = lua_toentity(L, 2);
  if (pNPC == NULL || pArg == NULL)
    return 0;

  CBaseCombatWeapon *pWeapon = dynamic_cast<CBaseCombatWeapon *>(pArg);
  if (pWeapon != NULL)
    pNPC->PickupWeapon(pWeapon);
  return 0;
}

static int NPC_SelectWeapon (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  /* GMod's SelectWeapon makes the NPC start using the given weapon; the
  ** engine's equivalent entry point is CBaseCombatCharacter::SetActiveWeapon.
  ** A classname string is accepted too and resolved against the NPC's own
  ** inventory. */
  CBaseEntity *pArg = lua_toentity(L, 2);
  if (pArg != NULL) {
    CBaseCombatWeapon *pWeapon = dynamic_cast<CBaseCombatWeapon *>(pArg);
    if (pWeapon != NULL)
      pNPC->SetActiveWeapon(pWeapon);
    return 0;
  }

  if (lua_isstring(L, 2)) {
    const char *pszClass = lua_tostring(L, 2);
    for (int i = 0; i < MAX_WEAPONS; ++i) {
      CBaseCombatWeapon *pWeapon = pNPC->GetWeapon(i);
      if (pWeapon != NULL && !Q_stricmp(pWeapon->GetClassname(), pszClass)) {
        pNPC->SetActiveWeapon(pWeapon);
        break;
      }
    }
  }
  return 0;
}

static int NPC_GetCurrentWeaponProficiency (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushinteger(L, pNPC != NULL ? (int)pNPC->GetCurrentWeaponProficiency() : 0);
  return 1;
}

static int NPC_SetCurrentWeaponProficiency (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iProficiency = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->SetCurrentWeaponProficiency((WeaponProficiency_t)iProficiency);
  return 0;
}

/*
** -- navigation goals, arrival, waypoints ---------------------------------
*/

static int NPC_GetNavType (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushinteger(L, pNPC != NULL ? (int)pNPC->GetNavType() : 0);
  return 1;
}

static int NPC_SetNavType (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iNavType = luaL_checkint(L, 2);
  if (pNPC != NULL)
    pNPC->SetNavType((Navigation_t)iNavType);
  return 0;
}

static int NPC_NavSetGoalPos (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  Vector vecGoal = luaL_checkvector(L, 2);
  Activity activity = (Activity)luaL_optint(L, 3, AIN_DEF_ACTIVITY);
  float flTolerance = (float)luaL_optnumber(L, 4, AIN_DEF_TOLERANCE);

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetGoal(AI_NavGoal_t(vecGoal, activity, flTolerance));
  return 0;
}

static int NPC_NavSetGoalTarget (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pTarget = lua_toentity(L, 2);
  if (pNPC == NULL || pTarget == NULL)
    return 0;

  Vector vecOffset = vec3_origin;
  if (IsVectorArg(L, 3))
    vecOffset = luaL_checkvector(L, 3);

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetGoalTarget(pTarget, vecOffset);
  return 0;
}

/* The wiki documents NavSetGoal( pos, length, dir ) as "picks a random node
** around the given vector, around the specified length, using dir as search
** direction" -- i.e. the navigator's SetRandomGoal whose search STARTS at an
** explicit position (GMod's binding branches into the same call). */
static int NPC_NavSetGoal (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  Vector vecFrom = luaL_checkvector(L, 2);
  float flLength = (float)luaL_checknumber(L, 3);
  Vector vecDir = IsVectorArg(L, 4) ? luaL_checkvector(L, 4) : vec3_origin;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  bool bOk = (pNavigator != NULL) && pNavigator->SetRandomGoal(vecFrom, flLength, vecDir);
  lua_pushboolean(L, bOk);
  return 1;
}

static int NPC_NavSetRandomGoal (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  float flLength = (float)luaL_checknumber(L, 2);
  Vector vecDir = IsVectorArg(L, 3) ? luaL_checkvector(L, 3) : vec3_origin;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  bool bOk = (pNavigator != NULL) && pNavigator->SetRandomGoal(flLength, vecDir);
  lua_pushboolean(L, bOk);
  return 1;
}

static int NPC_NavSetWanderGoal (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  float flMinRadius = (float)luaL_checknumber(L, 2);
  float flMaxRadius = (float)luaL_checknumber(L, 3);

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  bool bOk = (pNavigator != NULL) && pNavigator->SetWanderGoal(flMinRadius, flMaxRadius);
  lua_pushboolean(L, bOk);
  return 1;
}

static int NPC_AdvancePath (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->AdvancePath();
  return 0;
}

static int NPC_ClearGoal (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  lua_pushboolean(L, (pNavigator != NULL) && pNavigator->ClearGoal());
  return 1;
}

static int NPC_IsGoalActive (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushboolean(L, (pNavigator != NULL) && pNavigator->IsGoalActive());
  return 1;
}

static int NPC_GetCurGoalType (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushinteger(L, (pNavigator != NULL) ? (int)pNavigator->GetGoalType() : 0);
  return 1;
}

static int NPC_GetGoalPos (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushvector(L, (pNavigator != NULL) ? pNavigator->GetGoalPos() : vec3_origin);
  return 1;
}

static int NPC_GetGoalTarget (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  CBaseEntity::PushLuaInstanceSafe(L, (pNavigator != NULL) ? pNavigator->GetGoalTarget() : NULL);
  return 1;
}

static int NPC_HasObstacles (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushboolean(L, (pNavigator != NULL) && (pNavigator->GetBlockingEntity() != NULL));
  return 1;
}

static int NPC_GetBlockingEntity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  CBaseEntity::PushLuaInstanceSafe(L, (pNavigator != NULL) ? pNavigator->GetBlockingEntity() : NULL);
  return 1;
}

static int NPC_ClearBlockingEntity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_MoveProbe *pMoveProbe = pNPC->GetMoveProbe();
  if (pMoveProbe != NULL)
    pMoveProbe->ClearBlockingEntity();
  return 0;
}

static int NPC_SetMaxRouteRebuildTime (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flTime = (float)luaL_checknumber(L, 2);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetMaxRouteRebuildTime(flTime);
  return 0;
}

static int NPC_GetPathDistanceToGoal (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushnumber(L, (pNavigator != NULL) ? pNavigator->GetPathDistanceToGoal() : 0.0f);
  return 1;
}

static int NPC_GetPathTimeToGoal (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushnumber(L, (pNavigator != NULL) ? pNavigator->GetPathTimeToGoal() : 0.0f);
  return 1;
}

static int NPC_GetCurWaypointPos (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushvector(L, (pNavigator != NULL) ? pNavigator->GetCurWaypointPos() : vec3_origin);
  return 1;
}

static int NPC_GetNextWaypointPos (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  /* APPROXIMATE: this engine's navigator exposes the CURRENT waypoint only
  ** (GetCurWaypointPos); the next one is not separately readable, so the
  ** current one is answered -- the value GMod scripts use it for (distance
  ** checks) stays meaningful. */
  lua_pushvector(L, (pNavigator != NULL) ? pNavigator->GetCurWaypointPos() : vec3_origin);
  return 1;
}

static int NPC_IsCurWaypointGoal (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushboolean(L, (pNavigator != NULL) && pNavigator->CurWaypointIsGoal());
  return 1;
}

/*
** -- arrival tuning --------------------------------------------------------
*/

static int NPC_GetMovementActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushinteger(L, (pNavigator != NULL) ? (int)pNavigator->GetMovementActivity() : 0);
  return 1;
}

static int NPC_SetMovementActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iActivity = luaL_checkint(L, 2);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetMovementActivity((Activity)iActivity);
  return 0;
}

static int NPC_GetMovementSequence (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushinteger(L, (pNavigator != NULL) ? pNavigator->GetMovementSequence() : 0);
  return 1;
}

static int NPC_SetMovementSequence (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iSequence = luaL_checkint(L, 2);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetMovementSequence(iSequence);
  return 0;
}

static int NPC_GetArrivalActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushinteger(L, (pNavigator != NULL) ? (int)pNavigator->GetArrivalActivity() : 0);
  return 1;
}

static int NPC_SetArrivalActivity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iActivity = luaL_checkint(L, 2);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetArrivalActivity((Activity)iActivity);
  return 0;
}

static int NPC_GetArrivalSequence (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iCurSequence = luaL_optint(L, 2, 0);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushinteger(L, (pNavigator != NULL) ? pNavigator->GetArrivalSequence(iCurSequence) : 0);
  return 1;
}

static int NPC_SetArrivalSequence (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  int iSequence = luaL_checkint(L, 2);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetArrivalSequence(iSequence);
  return 0;
}

static int NPC_GetArrivalDirection (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushvector(L, (pNavigator != NULL) ? pNavigator->GetArrivalDirection() : vec3_origin);
  return 1;
}

static int NPC_SetArrivalDirection (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator == NULL)
    return 0;

  /* The engine has Vector / QAngle / entity overloads; GMod scripts pass a
  ** Vector or an entity. */
  if (IsVectorArg(L, 2))
    pNavigator->SetArrivalDirection(luaL_checkvector(L, 2));
  else {
    CBaseEntity *pTarget = lua_toentity(L, 2);
    if (pTarget != NULL)
      pNavigator->SetArrivalDirection(pTarget);
  }
  return 0;
}

static int NPC_GetArrivalSpeed (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushnumber(L, (pNavigator != NULL) ? pNavigator->GetArrivalSpeed() : 0.0f);
  return 1;
}

static int NPC_SetArrivalSpeed (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flSpeed = (float)luaL_checknumber(L, 2);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetArrivalSpeed(flSpeed);
  return 0;
}

static int NPC_GetArrivalDistance (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Navigator *pNavigator = (pNPC != NULL) ? pNPC->GetNavigator() : NULL;
  lua_pushnumber(L, (pNavigator != NULL) ? pNavigator->GetArrivalDistance() : 0.0f);
  return 1;
}

static int NPC_SetArrivalDistance (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flDistance = (float)luaL_checknumber(L, 2);
  if (pNPC == NULL)
    return 0;

  CAI_Navigator *pNavigator = pNPC->GetNavigator();
  if (pNavigator != NULL)
    pNavigator->SetArrivalDistance(flDistance);
  return 0;
}

/*
** -- movement tuning ------------------------------------------------------
*/

static int NPC_GetMoveInterval (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Motor *pMotor = (pNPC != NULL) ? pNPC->GetMotor() : NULL;
  lua_pushnumber(L, pMotor != NULL ? pMotor->GetMoveInterval() : 0.0f);
  return 1;
}

static int NPC_SetMoveInterval (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flInterval = (float)luaL_checknumber(L, 2);
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->SetMoveInterval(flInterval);
  return 0;
}

static int NPC_GetIdealMoveSpeed (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Motor *pMotor = (pNPC != NULL) ? pNPC->GetMotor() : NULL;
  lua_pushnumber(L, pMotor != NULL ? pMotor->GetIdealSpeed() : 0.0f);
  return 1;
}

static int NPC_GetIdealMoveAcceleration (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Motor *pMotor = (pNPC != NULL) ? pNPC->GetMotor() : NULL;
  lua_pushnumber(L, pMotor != NULL ? pMotor->GetIdealAccel() : 0.0f);
  return 1;
}

static int NPC_GetMoveVelocity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL) {
    lua_pushvector(L, vec3_origin);
    return 1;
  }

  /* A Vector written by SetMoveVelocity (which this engine's protected motor
  ** member cannot take) is answered back; otherwise the motor's live
  ** velocity. */
  NPC_PushExtraField(L, pNPC, s_pszKeyMoveVelocity);
  if (IsVectorArg(L, -1))
    return 1;
  lua_pop(L, 1);

  CAI_Motor *pMotor = pNPC->GetMotor();
  lua_pushvector(L, pMotor != NULL ? pMotor->GetCurVel() : vec3_origin);
  return 1;
}

static int NPC_SetMoveVelocity (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  Vector vecVelocity = luaL_checkvector(L, 2);
  if (pNPC != NULL) {
    /* APPROXIMATE: CAI_Motor::m_vecVelocity is protected with no public
    ** setter; the value is kept per entity (and answered by GetMoveVelocity)
    ** but does not drive the motor. */
    lua_pushvector(L, vecVelocity);
    NPC_SetExtraField(L, pNPC, s_pszKeyMoveVelocity);
  }
  return 0;
}

static int NPC_GetMinMoveStopDist (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Motor *pMotor = (pNPC != NULL) ? pNPC->GetMotor() : NULL;
  lua_pushnumber(L, pMotor != NULL ? pMotor->MinStoppingDist() : 10.0f);
  return 1;
}

static int NPC_GetMinMoveCheckDist (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Motor *pMotor = (pNPC != NULL) ? pNPC->GetMotor() : NULL;
  lua_pushnumber(L, pMotor != NULL ? pMotor->MinCheckDist() : 0.0f);
  return 1;
}

static int NPC_IsMoveYawLocked (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Motor *pMotor = (pNPC != NULL) ? pNPC->GetMotor() : NULL;
  lua_pushboolean(L, pMotor != NULL && pMotor->IsYawLocked());
  return 1;
}

static int NPC_SetMoveYawLocked (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  bool bLocked = lua_toboolean(L, 2) != 0;
  if (pNPC == NULL)
    return 0;

  CAI_Motor *pMotor = pNPC->GetMotor();
  if (pMotor != NULL)
    pMotor->SetYawLocked(bLocked);
  return 0;
}

/* The engine's m_flMoveWaitFinished is exactly GMod's move delay field (its
** binding reads base + curtime and writes curtime + delay). */
static int NPC_GetMoveDelay (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flDelay = 0.0f;
  if (pNPC != NULL)
    flDelay = MAX(0.0f, pNPC->m_flMoveWaitFinished - gpGlobals->curtime);
  lua_pushnumber(L, flDelay);
  return 1;
}

static int NPC_SetMoveDelay (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flDelay = (float)luaL_checknumber(L, 2);
  if (pNPC != NULL)
    pNPC->DelayMoveStart(flDelay);
  return 0;
}

/* This engine's m_flDistTooFar is the "enemy farther away than this" distance
** (ai_basenpc.h); it is the only per-NPC look-distance field, and GMod's
** binding also reads/writes one dedicated field, so the mapping is direct. */
static int NPC_GetMaxLookDistance (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushnumber(L, pNPC != NULL ? pNPC->m_flDistTooFar : 0.0f);
  return 1;
}

static int NPC_SetMaxLookDistance (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  float flDistance = (float)luaL_checknumber(L, 2);
  if (pNPC != NULL)
    pNPC->m_flDistTooFar = flDistance;
  return 0;
}

/*
** -- misc state -----------------------------------------------------------
*/

static int NPC_ExitScriptedSequence (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushboolean(L, pNPC != NULL && pNPC->ExitScriptedSequence());
  return 1;
}

static int NPC_IsUnreachable (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pTarget = lua_toentity(L, 2);
  lua_pushboolean(L, pNPC != NULL && pTarget != NULL && pNPC->IsUnreachable(pTarget));
  return 1;
}

static int NPC_RememberUnreachable (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pTarget = lua_toentity(L, 2);
  if (pNPC != NULL && pTarget != NULL)
    pNPC->RememberUnreachable(pTarget, (float)luaL_optnumber(L, 3, -1.0f));
  return 0;
}

static int NPC_TargetOrder (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CBaseEntity *pTarget = lua_toentity(L, 2);
  lua_pushboolean(L, pNPC != NULL && pTarget != NULL && pNPC->TargetOrder(pTarget, NULL, 0));
  return 1;
}

static int NPC_MoveOrder (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  Vector vecDest = luaL_checkvector(L, 2);
  if (pNPC != NULL)
    pNPC->MoveOrder(vecDest, NULL, 0);
  return 0;
}

/*
** -- squad ----------------------------------------------------------------
** GMod's GetSquad answers the squad NAME (its binding reads the name string
** off the squad and pushes that); SetSquad takes a name and resolves or
** creates the squad.  Both map onto CAI_Squad / g_AI_SquadManager directly.
*/

static int NPC_GetSquad (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Squad *pSquad = (pNPC != NULL) ? pNPC->GetSquad() : NULL;
  if (pSquad != NULL)
    lua_pushstring(L, pSquad->GetName() != NULL ? pSquad->GetName() : "");
  else
    lua_pushnil(L);
  return 1;
}

static int NPC_SetSquad (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  if (pNPC == NULL)
    return 0;

  if (lua_isstring(L, 2)) {
    const char *pszName = lua_tostring(L, 2);
    if (pszName != NULL && pszName[0] != '\0')
      pNPC->SetSquad(g_AI_SquadManager.FindCreateSquad(pNPC, AllocPooledString(pszName)));
    else
      pNPC->SetSquad(NULL);
  }
  else {
    /* nil (or anything else) clears the squad, which is what GMod's binding
    ** does for an empty name. */
    pNPC->SetSquad(NULL);
  }
  return 0;
}

static int NPC_IsSquadLeader (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Squad *pSquad = (pNPC != NULL) ? pNPC->GetSquad() : NULL;
  lua_pushboolean(L, pSquad != NULL && pSquad->IsLeader(pNPC));
  return 1;
}

static int NPC_GetNearestSquadMember (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  CAI_Squad *pSquad = (pNPC != NULL) ? pNPC->GetSquad() : NULL;
  CBaseEntity *pMember = (pSquad != NULL) ? pSquad->NearestSquadMember(pNPC) : NULL;
  CBaseEntity::PushLuaInstanceSafe(L, pMember);
  return 1;
}

/*
** -- expressions (actor NPCs only, like GMod) -----------------------------
*/

static int NPC_SetExpression (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  const char *pszExpression = luaL_checkstring(L, 2);

  CAI_BaseActor *pActor = dynamic_cast<CAI_BaseActor *>(pNPC);
  lua_pushnumber(L, pActor != NULL ? pActor->SetExpression(pszExpression) : 0.0f);
  return 1;
}

static int NPC_ClearExpression (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);

  CAI_BaseActor *pActor = dynamic_cast<CAI_BaseActor *>(pNPC);
  if (pActor != NULL)
    pActor->ClearExpression();
  return 0;
}

static int NPC_GetExpression (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);

  CAI_BaseActor *pActor = dynamic_cast<CAI_BaseActor *>(pNPC);
  const char *pszExpression = (pActor != NULL) ? pActor->GetExpression() : NULL;
  lua_pushstring(L, pszExpression != NULL ? pszExpression : "");
  return 1;
}

/*
** -- behaviors ------------------------------------------------------------
** The six Use*Behavior selectors pick a CAI behavior in GMod.  This fork's
** CAI_BaseNPC has no accessible behavior-host selection API for Lua, so the
** calls validate their arguments and are documented no-ops; the behaviors
** that DO exist keep running through the engine's own schedule selection.
** IsRunningBehavior reads the engine's real running-behavior pointer.
*/

static int NPC_IsRunningBehavior (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  lua_pushboolean(L, pNPC != NULL && pNPC->GetRunningBehavior() != NULL);
  return 1;
}

static int NPC_UseNoBehavior (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  (void)pNPC;
  /* NO-OP (documented): see the block comment above. */
  return 0;
}

static int NPC_UseActBusyBehavior (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  (void)pNPC;
  return 0;
}

static int NPC_UseAssaultBehavior (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  (void)pNPC;
  return 0;
}

static int NPC_UseFollowBehavior (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  (void)pNPC;
  return 0;
}

static int NPC_UseFuncTankBehavior (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  (void)pNPC;
  return 0;
}

static int NPC_UseLeadBehavior (lua_State *L) {
  lua_NPC *pNPC = ToNPC(L, 1);
  (void)pNPC;
  return 0;
}

#endif  // !CLIENT_DLL


/*
** ===========================================================================
** The NPC metatable.
**
** __index honours the same precedence CBaseAnimating___index established: a
** script-table FUNCTION wins over everything, then the class method tables,
** then a script-table data value.  Everything the NPC table does not answer
** is delegated to CBaseAnimating's own __index, which owns the rest of the
** chain (CBaseAnimating methods, CBaseEntity methods, the deprecated
** self-reference fields, the scripted-class table and the engine field
** reads) -- delegating instead of copying keeps that behaviour in one place.
**
** That chain is also how the remaining GMod entries stay reachable without a
** second binding: BecomeRagdoll / AddRelationship-shaped helpers that live on
** the shared Entity / Animating metatables resolve through it (the NPC table
** itself carries its own AddRelationship above, because GMod documents it
** with the NPC-specific single-string form).
** ===========================================================================
*/

static int NPC___index (lua_State *L) {
  CBaseEntity *pEntity = lua_toentity(L, 1);
  if (pEntity == NULL) {
    /* GMod's NULL sentinel answers reads instead of raising -- same contract
    ** as CBaseEntity___index / CBasePlayer___index. */
    HL2SB_PushNullEntityIndex(L, lua_tostring(L, 2));
    return 1;
  }

  /* 1. script-table functions are overrides and win over C++ methods (the
  **    rule of CBaseAnimating___index; raw reads on purpose -- lua_gettable
  **    on the metatable would re-enter an __index). */
  if (lua_isrefvalid(L, pEntity->m_nTableReference)) {
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;
    lua_pop(L, 2);
  }

  /* 2. this class's own methods. */
  lua_getmetatable(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;
    lua_pop(L, 2);
  }
  else {
    lua_pop(L, 1);
  }

  /* 3. delegate: CBaseAnimating___index continues the chain. */
  luaL_getmetatable(L, "CBaseAnimating");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }
  lua_getfield(L, -1, "__index");
  lua_remove(L, -2);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_call(L, 2, 1);
  return 1;
}

static int NPC___newindex (lua_State *L) {
  /* Delegate to CBaseEntity's own __newindex: it owns the real engine fields
  ** and the per-entity fallback table, exactly like the Vehicle metatable. */
  luaL_getmetatable(L, "CBaseEntity");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "__newindex");
  lua_remove(L, -2);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_call(L, 3, 0);
  return 0;
}

static int NPC___eq (lua_State *L) {
  /* Handles, not metatable identity: the two operands may be an NPC userdata
  ** and a plain entity userdata for the same entity, and those are equal. */
  lua_pushboolean(L, lua_toentity(L, 1) == lua_toentity(L, 2));
  return 1;
}

static int NPC___tostring (lua_State *L) {
  CBaseEntity *pEntity = lua_toentity(L, 1);
  if (pEntity == NULL)
    lua_pushstring(L, "NULL");
  else
    lua_pushfstring(L, "NPC: %d %s", pEntity->entindex(), pEntity->GetClassname());
  return 1;
}

#ifdef CLIENT_DLL
/* GMod's client NPC table carries exactly the three methods below; every AI
** method is server-side state there too, and the metatable chain still
** delivers the Entity / Animating surface. */
static const luaL_Reg NPCmeta[] = {
  {"IsNPC", NPC_IsNPC},
  {"GetActiveWeapon", NPC_GetActiveWeapon},
  {"__index", NPC___index},
  {"__newindex", NPC___newindex},
  {"__eq", NPC___eq},
  {"__tostring", NPC___tostring},
  {NULL, NULL}
};
#else
static const luaL_Reg NPCmeta[] = {
  {"AddClassRelationship", NPC_AddClassRelationship},
  {"AddEntityRelationship", NPC_AddEntityRelationship},
  {"AddRelationship", NPC_AddRelationship},
  {"AdvancePath", NPC_AdvancePath},
  {"AlertSound", NPC_AlertSound},
  {"AutoMovement", NPC_AutoMovement},
  {"CapabilitiesAdd", NPC_CapabilitiesAdd},
  {"CapabilitiesClear", NPC_CapabilitiesClear},
  {"CapabilitiesGet", NPC_CapabilitiesGet},
  {"CapabilitiesHas", NPC_CapabilitiesHas},
  {"CapabilitiesRemove", NPC_CapabilitiesRemove},
  {"Classify", NPC_Classify},
  {"ClearBlockingEntity", NPC_ClearBlockingEntity},
  {"ClearCondition", NPC_ClearCondition},
  {"ClearEnemyMemory", NPC_ClearEnemyMemory},
  {"ClearExpression", NPC_ClearExpression},
  {"ClearGoal", NPC_ClearGoal},
  {"ClearSchedule", NPC_ClearSchedule},
  {"ConditionID", NPC_ConditionID},
  {"ConditionName", NPC_ConditionName},
  {"Disposition", NPC_Disposition},
  {"DropWeapon", NPC_DropWeapon},
  {"ExitScriptedSequence", NPC_ExitScriptedSequence},
  {"FearSound", NPC_FearSound},
  {"FoundEnemySound", NPC_FoundEnemySound},
  {"GetActiveWeapon", NPC_GetActiveWeapon},
  {"GetActivity", NPC_GetActivity},
  {"GetAimVector", NPC_GetAimVector},
  {"GetArrivalActivity", NPC_GetArrivalActivity},
  {"GetArrivalDirection", NPC_GetArrivalDirection},
  {"GetArrivalDistance", NPC_GetArrivalDistance},
  {"GetArrivalSequence", NPC_GetArrivalSequence},
  {"GetArrivalSpeed", NPC_GetArrivalSpeed},
  {"GetBestSoundHint", NPC_GetBestSoundHint},
  {"GetBlockingEntity", NPC_GetBlockingEntity},
  {"GetCurGoalType", NPC_GetCurGoalType},
  {"GetCurrentSchedule", NPC_GetCurrentSchedule},
  {"GetCurrentWeaponProficiency", NPC_GetCurrentWeaponProficiency},
  {"GetCurWaypointPos", NPC_GetCurWaypointPos},
  {"GetEnemy", NPC_GetEnemy},
  {"GetEnemyFirstTimeSeen", NPC_GetEnemyFirstTimeSeen},
  {"GetEnemyLastKnownPos", NPC_GetEnemyLastKnownPos},
  {"GetEnemyLastSeenPos", NPC_GetEnemyLastSeenPos},
  {"GetEnemyLastTimeSeen", NPC_GetEnemyLastTimeSeen},
  {"GetEnemyLKP", NPC_GetEnemyLKP},
  {"GetExpression", NPC_GetExpression},
  {"GetEyeDirection", NPC_GetEyeDirection},
  {"GetFOV", NPC_GetFOV},
  {"GetGoalPos", NPC_GetGoalPos},
  {"GetGoalTarget", NPC_GetGoalTarget},
  {"GetHeadDirection", NPC_GetHeadDirection},
  {"GetHullType", NPC_GetHullType},
  {"GetIdealActivity", NPC_GetIdealActivity},
  {"GetIdealMoveAcceleration", NPC_GetIdealMoveAcceleration},
  {"GetIdealMoveSpeed", NPC_GetIdealMoveSpeed},
  {"GetIdealSequence", NPC_GetIdealSequence},
  {"GetIdealYaw", NPC_GetIdealYaw},
  {"GetKnownEnemies", NPC_GetKnownEnemies},
  {"GetKnownEnemyCount", NPC_GetKnownEnemyCount},
  {"GetLastPosition", NPC_GetLastPosition},
  {"GetLastTimeTookDamageFromEnemy", NPC_GetLastTimeTookDamageFromEnemy},
  {"GetMaxLookDistance", NPC_GetMaxLookDistance},
  {"GetMinMoveCheckDist", NPC_GetMinMoveCheckDist},
  {"GetMinMoveStopDist", NPC_GetMinMoveStopDist},
  {"GetMoveDelay", NPC_GetMoveDelay},
  {"GetMoveInterval", NPC_GetMoveInterval},
  {"GetMovementActivity", NPC_GetMovementActivity},
  {"GetMovementSequence", NPC_GetMovementSequence},
  {"GetMoveVelocity", NPC_GetMoveVelocity},
  {"GetNavType", NPC_GetNavType},
  {"GetNearestSquadMember", NPC_GetNearestSquadMember},
  {"GetNextWaypointPos", NPC_GetNextWaypointPos},
  {"GetNPCState", NPC_GetNPCState},
  {"GetPathDistanceToGoal", NPC_GetPathDistanceToGoal},
  {"GetPathTimeToGoal", NPC_GetPathTimeToGoal},
  {"GetShootPos", NPC_GetShootPos},
  {"GetSquad", NPC_GetSquad},
  {"GetStepHeight", NPC_GetStepHeight},
  {"GetTarget", NPC_GetTarget},
  {"GetTaskStatus", NPC_GetTaskStatus},
  {"GetTimeEnemyLastReacquired", NPC_GetTimeEnemyLastReacquired},
  {"GetViewOffset", NPC_GetViewOffset},
  {"GetWeapon", NPC_GetWeapon},
  {"GetWeapons", NPC_GetWeapons},
  {"Give", NPC_Give},
  {"HasCondition", NPC_HasCondition},
  {"HasEnemyEluded", NPC_HasEnemyEluded},
  {"HasEnemyMemory", NPC_HasEnemyMemory},
  {"HasObstacles", NPC_HasObstacles},
  {"IdleSound", NPC_IdleSound},
  {"IgnoreEnemyUntil", NPC_IgnoreEnemyUntil},
  {"IsCrouching", NPC_IsCrouching},
  {"IsCurrentSchedule", NPC_IsCurrentSchedule},
  {"IsCurWaypointGoal", NPC_IsCurWaypointGoal},
  {"IsFacingIdealYaw", NPC_IsFacingIdealYaw},
  {"IsGoalActive", NPC_IsGoalActive},
  {"IsInViewCone", NPC_IsInViewCone},
  {"IsMoveYawLocked", NPC_IsMoveYawLocked},
  {"IsMoving", NPC_IsMoving},
  {"IsRunningBehavior", NPC_IsRunningBehavior},
  {"IsSquadLeader", NPC_IsSquadLeader},
  {"IsUnforgettable", NPC_IsUnforgettable},
  {"IsUnreachable", NPC_IsUnreachable},
  {"IsNPC", NPC_IsNPC},
  {"LostEnemySound", NPC_LostEnemySound},
  {"MaintainActivity", NPC_MaintainActivity},
  {"MarkEnemyAsEluded", NPC_MarkEnemyAsEluded},
  {"MarkTookDamageFromEnemy", NPC_MarkTookDamageFromEnemy},
  {"MoveClimbExec", NPC_MoveClimbExec},
  {"MoveClimbStart", NPC_MoveClimbStart},
  {"MoveClimbStop", NPC_MoveClimbStop},
  {"MoveGroundStep", NPC_MoveGroundStep},
  {"MoveJumpExec", NPC_MoveJumpExec},
  {"MoveJumpStart", NPC_MoveJumpStart},
  {"MoveJumpStop", NPC_MoveJumpStop},
  {"MoveOrder", NPC_MoveOrder},
  {"MovePause", NPC_MovePause},
  {"MoveStart", NPC_MoveStart},
  {"MoveStop", NPC_MoveStop},
  {"NavSetGoal", NPC_NavSetGoal},
  {"NavSetGoalPos", NPC_NavSetGoalPos},
  {"NavSetGoalTarget", NPC_NavSetGoalTarget},
  {"NavSetRandomGoal", NPC_NavSetRandomGoal},
  {"NavSetWanderGoal", NPC_NavSetWanderGoal},
  {"PickupWeapon", NPC_PickupWeapon},
  {"PlaySentence", NPC_PlaySentence},
  {"RememberUnreachable", NPC_RememberUnreachable},
  {"RemoveIgnoreConditions", NPC_RemoveIgnoreConditions},
  {"ResetIdealActivity", NPC_ResetIdealActivity},
  {"ResetMoveCalc", NPC_ResetMoveCalc},
  {"RunEngineTask", NPC_RunEngineTask},
  {"SelectWeapon", NPC_SelectWeapon},
  {"SentenceStop", NPC_SentenceStop},
  {"SetActivity", NPC_SetActivity},
  {"SetArrivalActivity", NPC_SetArrivalActivity},
  {"SetArrivalDirection", NPC_SetArrivalDirection},
  {"SetArrivalDistance", NPC_SetArrivalDistance},
  {"SetArrivalSequence", NPC_SetArrivalSequence},
  {"SetArrivalSpeed", NPC_SetArrivalSpeed},
  {"SetCondition", NPC_SetCondition},
  {"SetCurrentWeaponProficiency", NPC_SetCurrentWeaponProficiency},
  {"SetEnemy", NPC_SetEnemy},
  {"SetExpression", NPC_SetExpression},
  {"SetFOV", NPC_SetFOV},
  {"SetForceCrouch", NPC_SetForceCrouch},
  {"SetHullSizeNormal", NPC_SetHullSizeNormal},
  {"SetHullType", NPC_SetHullType},
  {"SetIdealActivity", NPC_SetIdealActivity},
  {"SetIdealSequence", NPC_SetIdealSequence},
  {"SetIdealYaw", NPC_SetIdealYaw},
  {"SetIdealYawAndUpdate", NPC_SetIdealYawAndUpdate},
  {"SetIgnoreConditions", NPC_SetIgnoreConditions},
  {"SetLastPosition", NPC_SetLastPosition},
  {"SetMaxLookDistance", NPC_SetMaxLookDistance},
  {"SetMaxRouteRebuildTime", NPC_SetMaxRouteRebuildTime},
  {"SetMoveDelay", NPC_SetMoveDelay},
  {"SetMoveInterval", NPC_SetMoveInterval},
  {"SetMovementActivity", NPC_SetMovementActivity},
  {"SetMovementSequence", NPC_SetMovementSequence},
  {"SetMoveVelocity", NPC_SetMoveVelocity},
  {"SetMoveYawLocked", NPC_SetMoveYawLocked},
  {"SetNavType", NPC_SetNavType},
  {"SetNPCState", NPC_SetNPCState},
  {"SetSchedule", NPC_SetSchedule},
  {"SetSquad", NPC_SetSquad},
  {"SetStepHeight", NPC_SetStepHeight},
  {"SetTarget", NPC_SetTarget},
  {"SetTaskStatus", NPC_SetTaskStatus},
  {"SetUnforgettable", NPC_SetUnforgettable},
  {"SetViewOffset", NPC_SetViewOffset},
  {"StartEngineTask", NPC_StartEngineTask},
  {"StopMoving", NPC_StopMoving},
  {"TargetOrder", NPC_TargetOrder},
  {"TaskComplete", NPC_TaskComplete},
  {"TaskFail", NPC_TaskFail},
  {"UpdateEnemyMemory", NPC_UpdateEnemyMemory},
  {"UpdateTurnActivity", NPC_UpdateTurnActivity},
  {"UpdateYaw", NPC_UpdateYaw},
  {"UseActBusyBehavior", NPC_UseActBusyBehavior},
  {"UseAssaultBehavior", NPC_UseAssaultBehavior},
  {"UseFollowBehavior", NPC_UseFollowBehavior},
  {"UseFuncTankBehavior", NPC_UseFuncTankBehavior},
  {"UseLeadBehavior", NPC_UseLeadBehavior},
  {"UseNoBehavior", NPC_UseNoBehavior},
  {"__index", NPC___index},
  {"__newindex", NPC___newindex},
  {"__eq", NPC___eq},
  {"__tostring", NPC___tostring},
  {NULL, NULL}
};
#endif


/*
** Open NPC object
*/
LUALIB_API int luaopen_NPC_shared (lua_State *L) {
  /* HL2SB: GMod-shaped creation -- the type id (9 = TYPE_ENTITY, the same
  ** family id GMod's own registry wiring passes for NPC; verified from its
  ** wire call) rides lua_shared's type slot, which stamps MetaName/MetaID on
  ** the freshly created metatable (lauxlib.c, GMod lua_shared architecture).
  ** lsrcinit.cpp's s_LuaTypeInfo table later adds MetaBaseClass and keeps the
  ** display names canonical. */
  luaL_newmetatable_type(L, "NPC", 9);
  luaL_register(L, NULL, NPCmeta);
  lua_pushstring(L, "entity");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "entity" */
  return 1;
}


/*
** ===========================================================================
** The NextBot metatable.
**
** GMod's NextBot surface is thin -- range, vision, collision mask, the last
** known nav area -- because everything else a bot does (locomotion, path
** following, the behaviour coroutine) lives on its own interfaces, which this
** fork already binds elsewhere (CLuaLocomotion, Path(), navmesh.*).  The
** client realm carries IsNextBot only, matching GMod's own two-entry client
** table (its other entry is __tostring).
** ===========================================================================
*/

#ifndef CLIENT_DLL
/* Per-entity storage for the two values whose interfaces have no setters in
** this fork (IBody::GetSolidMask / IVision::GetMaxVisionRange are reads). */
static const char *const s_pszKeyNBVisionRange = "hl2sb_nb_visionrange";
static const char *const s_pszKeyNBSolidMask   = "hl2sb_nb_solidmask";

static INextBot *ToNextBot (lua_State *L, int nArg) {
  CBaseEntity *pEntity = luaL_checkentity(L, nArg);
  return (pEntity != NULL) ? pEntity->MyNextBotPointer() : NULL;
}

/* Exported by lnavmesh.cpp (server realm) -- the same helper the navmesh
** module hands every CNavArea through.  Declared locally to keep nav_area.h
** out of this translation unit. */
class CNavArea;
void LuaPushNavArea (lua_State *L, CNavArea *pArea);

static int NextBot_GetRangeTo (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  if (pBot == NULL) {
    lua_pushnumber(L, 0.0f);
    return 1;
  }

  if (IsVectorArg(L, 2)) {
    lua_pushnumber(L, pBot->GetRangeTo(luaL_checkvector(L, 2)));
  }
  else {
    CBaseEntity *pSubject = lua_toentity(L, 2);
    lua_pushnumber(L, pSubject != NULL ? pBot->GetRangeTo(pSubject) : 0.0f);
  }
  return 1;
}

static int NextBot_GetRangeSquaredTo (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  if (pBot == NULL) {
    lua_pushnumber(L, 0.0f);
    return 1;
  }

  if (IsVectorArg(L, 2)) {
    lua_pushnumber(L, pBot->GetRangeSquaredTo(luaL_checkvector(L, 2)));
  }
  else {
    CBaseEntity *pSubject = lua_toentity(L, 2);
    lua_pushnumber(L, pSubject != NULL ? pBot->GetRangeSquaredTo(pSubject) : 0.0f);
  }
  return 1;
}

static int NextBot_IsAbleToSee (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  CBaseEntity *pSubject = lua_toentity(L, 2);
  /* GMod's third argument defaults to true (USE_FOV) when absent -- its
  ** binding treats a missing third argument as true (USE_FOV). */
  bool bUseFOV = luaL_optboolean(L, 3, true) != 0;

  IVision *pVision = (pBot != NULL) ? pBot->GetVisionInterface() : NULL;
  lua_pushboolean(L, pVision != NULL && pSubject != NULL &&
    pVision->IsAbleToSee(pSubject, bUseFOV ? IVision::USE_FOV : IVision::DISREGARD_FOV));
  return 1;
}

static int NextBot_GetFOV (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  IVision *pVision = (pBot != NULL) ? pBot->GetVisionInterface() : NULL;
  lua_pushnumber(L, pVision != NULL ? pVision->GetFieldOfView() : 0.0f);
  return 1;
}

static int NextBot_SetFOV (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  float flFOV = (float)luaL_checknumber(L, 2);
  IVision *pVision = (pBot != NULL) ? pBot->GetVisionInterface() : NULL;
  if (pVision != NULL)
    pVision->SetFieldOfView(flFOV);
  return 0;
}

static int NextBot_GetMaxVisionRange (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  CBaseEntity *pEntity = lua_toentity(L, 1);

  float flStored = 0.0f;
  if (pEntity != NULL && NPC_GetExtraNumber(L, pEntity, s_pszKeyNBVisionRange, &flStored)) {
    lua_pushnumber(L, flStored);
    return 1;
  }

  IVision *pVision = (pBot != NULL) ? pBot->GetVisionInterface() : NULL;
  lua_pushnumber(L, pVision != NULL ? pVision->GetMaxVisionRange() : 0.0f);
  return 1;
}

static int NextBot_SetMaxVisionRange (lua_State *L) {
  CBaseEntity *pEntity = lua_toentity(L, 1);
  float flRange = (float)luaL_checknumber(L, 2);
  if (pEntity != NULL) {
    /* APPROXIMATE: IVision::GetMaxVisionRange is virtual read-only in this
    ** fork; the value is kept per entity and answered back by
    ** GetMaxVisionRange but does not change the vision component. */
    lua_pushnumber(L, flRange);
    NPC_SetExtraField(L, pEntity, s_pszKeyNBVisionRange);
  }
  return 0;
}

static int NextBot_GetSolidMask (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  CBaseEntity *pEntity = lua_toentity(L, 1);

  /* Read through lua_tointeger, NOT the float helper: a collision mask is a
  ** 32-bit bitfield (e.g. 0x0200400B > 2^24) and a float round-trip drops
  ** the low bits.  Lua 5.4 integers keep it exact. */
  if (pEntity != NULL) {
    NPC_PushExtraField(L, pEntity, s_pszKeyNBSolidMask);
    if (lua_isnumber(L, -1)) {
      lua_Integer iStored = lua_tointeger(L, -1);
      lua_pop(L, 1);
      lua_pushinteger(L, iStored);
      return 1;
    }
    lua_pop(L, 1);
  }

  IBody *pBody = (pBot != NULL) ? pBot->GetBodyInterface() : NULL;
  lua_pushinteger(L, (lua_Integer)(pBody != NULL ? pBody->GetSolidMask() : 0));
  return 1;
}

static int NextBot_SetSolidMask (lua_State *L) {
  CBaseEntity *pEntity = lua_toentity(L, 1);
  int iMask = luaL_checkint(L, 2);
  if (pEntity != NULL) {
    /* APPROXIMATE: IBody::GetSolidMask is virtual read-only in this fork; the
    ** value is kept per entity and answered back by GetSolidMask but does not
    ** change the bot's collision filtering. */
    lua_pushinteger(L, iMask);
    NPC_SetExtraField(L, pEntity, s_pszKeyNBSolidMask);
  }
  return 0;
}

static int NextBot_GetLastKnownArea (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  CBaseCombatCharacter *pCharacter = (pBot != NULL) ? pBot->GetEntity() : NULL;
  LuaPushNavArea(L, (pCharacter != NULL) ? pCharacter->GetLastKnownArea() : NULL);
  return 1;
}

static int NextBot_ClearLastKnownArea (lua_State *L) {
  INextBot *pBot = ToNextBot(L, 1);
  CBaseCombatCharacter *pCharacter = (pBot != NULL) ? pBot->GetEntity() : NULL;
  if (pCharacter != NULL)
    pCharacter->ClearLastKnownArea();
  return 0;
}

static int NextBot_IsNextBot (lua_State *L) {
  /* lua_toentity, not ToNextBot: the predicate answers false for any entity
  ** that is not a bot (and never raises), where luaL_checkentity would raise
  ** on a non-entity argument. */
  CBaseEntity *pEntity = lua_toentity(L, 1);
  lua_pushboolean(L, pEntity != NULL && pEntity->MyNextBotPointer() != NULL);
  return 1;
}
#else
/* GMod's client NextBot table is exactly __tostring / IsNextBot. */
static int NextBot_IsNextBot (lua_State *L) {
  CBaseEntity *pEntity = lua_toentity(L, 1);
  lua_pushboolean(L, pEntity != NULL && pEntity->IsNextBot());
  return 1;
}
#endif


static int NextBot___index (lua_State *L) {
  CBaseEntity *pEntity = lua_toentity(L, 1);
  if (pEntity == NULL) {
    HL2SB_PushNullEntityIndex(L, lua_tostring(L, 2));
    return 1;
  }

  /* Same precedence as NPC___index: script functions first, then this
  ** metatable, then CBaseAnimating's __index owns the rest of the chain. */
  if (lua_isrefvalid(L, pEntity->m_nTableReference)) {
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;
    lua_pop(L, 2);
  }

  lua_getmetatable(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;
    lua_pop(L, 2);
  }
  else {
    lua_pop(L, 1);
  }

  luaL_getmetatable(L, "CBaseAnimating");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }
  lua_getfield(L, -1, "__index");
  lua_remove(L, -2);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_call(L, 2, 1);
  return 1;
}

static int NextBot___newindex (lua_State *L) {
  luaL_getmetatable(L, "CBaseEntity");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "__newindex");
  lua_remove(L, -2);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_call(L, 3, 0);
  return 0;
}

static int NextBot___eq (lua_State *L) {
  lua_pushboolean(L, lua_toentity(L, 1) == lua_toentity(L, 2));
  return 1;
}

static int NextBot___tostring (lua_State *L) {
  CBaseEntity *pEntity = lua_toentity(L, 1);
  if (pEntity == NULL)
    lua_pushstring(L, "NULL");
  else
    lua_pushfstring(L, "NextBot: %d %s", pEntity->entindex(), pEntity->GetClassname());
  return 1;
}

static const luaL_Reg NextBotmeta[] = {
#ifdef CLIENT_DLL
  {"IsNextBot", NextBot_IsNextBot},
#else
  {"ClearLastKnownArea", NextBot_ClearLastKnownArea},
  {"GetFOV", NextBot_GetFOV},
  {"GetLastKnownArea", NextBot_GetLastKnownArea},
  {"GetMaxVisionRange", NextBot_GetMaxVisionRange},
  {"GetRangeSquaredTo", NextBot_GetRangeSquaredTo},
  {"GetRangeTo", NextBot_GetRangeTo},
  {"GetSolidMask", NextBot_GetSolidMask},
  {"IsAbleToSee", NextBot_IsAbleToSee},
  {"IsNextBot", NextBot_IsNextBot},
  {"SetFOV", NextBot_SetFOV},
  {"SetMaxVisionRange", NextBot_SetMaxVisionRange},
  {"SetSolidMask", NextBot_SetSolidMask},
#endif
  {"__index", NextBot___index},
  {"__newindex", NextBot___newindex},
  {"__eq", NextBot___eq},
  {"__tostring", NextBot___tostring},
  {NULL, NULL}
};


/*
** Open NextBot object
*/
LUALIB_API int luaopen_NextBot_shared (lua_State *L) {
  /* Same GMod-shaped creation as the NPC metatable above: id 9
  ** (TYPE_ENTITY), matching GMod's own NextBot registry wire. */
  luaL_newmetatable_type(L, "NextBot", 9);
  luaL_register(L, NULL, NextBotmeta);
  lua_pushstring(L, "entity");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "entity" */
  return 1;
}

