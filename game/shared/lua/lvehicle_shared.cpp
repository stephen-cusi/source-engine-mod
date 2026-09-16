//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB's C++ port of Garry's Mod's Vehicle Lua library
//          (https://wiki.facepunch.com/gmod/Vehicle).
//
// Method list taken from the wiki; the metatable is registered under the name
// "Vehicle" so that FindMetaTable("Vehicle") / s_LuaMetatableAliases resolve it
// the same way FindMetaTable("Player") resolves CHL2MP_Player.
//
// Where the data lives in THIS engine (read the .h of each, not invented):
//   * game/server/vehicle_base.h            CPropVehicleDriveable, CPropVehicle,
//                                           CFourWheelServerVehicle (IServerVehicle)
//                                           -- GetDriver, GetPhysics, engine on/off,
//                                           m_nSpeed/m_nRPM/m_flThrottle/m_nBoost*,
//                                           IsVehicleBodyInWater, StartEngine/StopEngine
//   * game/server/fourwheelvehiclephysics.h CFourWheelVehiclePhysics - wheels, boost,
//                                           throttle/steering, operating + vehicle params
//   * game/server/vehicle_baseserver.h      CBaseServerVehicle - passengers, seat points,
//                                           CheckExitPoint
//   * game/server/iservervehicle.h          IServerVehicle / IDrivableVehicle
//   * game/client/c_prop_vehicle.h          C_PropVehicleDriveable  (client realm; only
//                                           speed/RPM/throttle/boost are networked)
//   * game/client/iclientvehicle.h          IClientVehicle
//
// HL2SB policy is "compatibility with addons, not a byte-for-byte GMod clone", so
// every method that has no engine-side backing in the current realm answers a
// documented default instead of raising.  Those methods carry an
// "HL2SB fallback:" comment that says exactly what is missing and why.
//
//=============================================================================//
#define lvehicle_shared_cpp

#include "cbase.h"

#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "lvehicle_shared.h"
#include "lvphysics_interface.h"
#include "mathlib/lvector.h"

#ifdef CLIENT_DLL
#include "iclientvehicle.h"
// HL2SB: the vehicle third person camera state is the pair of archived client
// convars defined in game/client/clientmode_shared.cpp and applied to the final
// CViewSetup by ClientModeShared::OverrideView.  Declared extern here exactly the
// way game/client/c_baseplayer.cpp:1906 declares hl2sb_veh_thirdperson.
extern ConVar hl2sb_veh_thirdperson;
extern ConVar hl2sb_veh_thirdperson_dist;
#else
#include "vehicle_baseserver.h"
#include "fourwheelvehiclephysics.h"
#include "iservervehicle.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/*
** ===========================================================================
** Per-entity storage for the values GMod keeps on the vehicle but this engine
** has nowhere to put.
**
** These are written into the entity's own Lua field table -- the same table the
** generic CBaseEntity __newindex fills (see CBaseEntity___newindex in
** lbaseentity_shared.cpp).  That table is keyed by the entity, is released by
** ~CBaseEntity (baseentity.cpp:484), and is invisible to the vehicle code, so
** nothing here can leak or outlive the vehicle.
** ===========================================================================
*/
static const char *const s_pszKeyVehicleClass    = "hl2sb_vehicleclass";
static const char *const s_pszKeyThirdPerson     = "hl2sb_thirdperson";
static const char *const s_pszKeyCameraDistance  = "hl2sb_cameradistance";
static const char *const s_pszKeyBrakePedal      = "hl2sb_brakepedal";
static const char *const s_pszKeyEngineEnabled   = "hl2sb_engineenabled";

// The default third person distance; the same value hl2sb_veh_thirdperson_dist
// ships with (game/client/clientmode_shared.cpp).
#define VEHICLE_DEFAULT_CAMERA_DISTANCE 280.0f

// CFourWheelVehiclePhysics stores the wheels in a 4 entry array (m_pWheels[4] in
// fourwheelvehiclephysics.h) while IPhysicsVehicleController::GetWheelCount() can
// report up to VEHICLE_MAX_WHEEL_COUNT (8).  Every wheel accessor below clamps to
// this so a Lua call can never index past the array.
#define VEHICLE_LUA_MAX_WHEELS 4


/*
** access functions (stack -> C)
*/

LUA_API lua_Vehicle *lua_tovehicle (lua_State *L, int idx) {
  CBaseHandle *hEntity = dynamic_cast<CBaseHandle *>((CBaseHandle *)lua_touserdata(L, idx));
  if (hEntity == NULL)
    return NULL;
  return dynamic_cast<lua_Vehicle *>(hEntity->Get());
}

/* The single CBaseEntity base path out of lua_Vehicle. */
static CBaseEntity *VehicleToEntity (lua_Vehicle *pVehicle) {
  return pVehicle;
}

/* Resolves the entity's C++ type before anything is pushed.  The two virtuals are
** overridden by every vehicle (C_BaseEntity::GetClientVehicle /
** CBaseEntity::GetServerVehicle) and answer NULL for everything else, so the
** dynamic_cast only runs for candidates -- but they are not sufficient on their
** own:
**
**   * CPropVehicleDriveable::CreateServerVehicle() (and therefore
**     GetServerVehicle()) only runs from Spawn(), so a vehicle that Lua just
**     ents.Create()d would be missed.  GMod hands the Vehicle metatable out from
**     the first Create() -- its own docs only say the Vehicle METHODS are unusable
**     before Spawn (IsValidVehicle() answers false) -- and SetVehicleClass /
**     SetVehicleParams are exactly what an addon calls there.  Every drivable
**     vehicle in this tree is registered as "prop_vehicle*", so a failed virtual
**     test falls back to that prefix.
**   * C_VehicleCrane / CPropVehicleCrane answer the virtual but are not
**     CPropVehicleDriveable, so they keep the plain entity metatable: the Vehicle
**     library is the driveable four-wheel library, and inventing crane behaviour
**     would be worse than the missing methods.
*/
static lua_Vehicle *ToVehicleFromEntity (CBaseEntity *pEntity) {
  if (pEntity == NULL)
    return NULL;

#ifdef CLIENT_DLL
  bool bCandidate = (pEntity->GetClientVehicle() != NULL);
#else
  bool bCandidate = (pEntity->GetServerVehicle() != NULL);
#endif

  if (!bCandidate) {
    const char *pszClassname = pEntity->GetClassname();
    if (pszClassname == NULL || Q_strncmp(pszClassname, "prop_vehicle", 12) != 0)
      return NULL;
  }

  return dynamic_cast<lua_Vehicle *>(pEntity);
}


/*
** push functions (C -> stack)
*/

LUA_API void lua_pushvehicle (lua_State *L, lua_Vehicle *pVehicle) {
  if (pVehicle == NULL) {
    lua_pushentity(L, NULL);
    return;
  }

  CBaseHandle *hVehicle = (CBaseHandle *)lua_newuserdata(L, sizeof(CBaseHandle));
  hVehicle->Set(VehicleToEntity(pVehicle));
  luaL_getmetatable(L, "Vehicle");
  lua_setmetatable(L, -2);
}

/*
** HL2SB: the entity-level dispatch.  Answers false and leaves the stack
** untouched when pEntity is not a drivable vehicle, or when the "Vehicle"
** metatable has not been opened in this Lua state, so lua_pushentity() /
** CBaseEntity::PushLuaInstanceSafe() can fall through to their own push.
** Declared in lbaseentity_shared.h.
*/
LUA_API bool lua_pushvehicleentity (lua_State *L, CBaseEntity *pEntity) {
  lua_Vehicle *pVehicle = ToVehicleFromEntity(pEntity);
  if (pVehicle == NULL)
    return false;

  luaL_getmetatable(L, "Vehicle");
  bool bHasMetatable = lua_istable(L, -1);
  lua_pop(L, 1);
  if (!bHasMetatable)
    return false;

  lua_pushvehicle(L, pVehicle);
  return true;
}

/* HL2SB: Entity:IsVehicle() (see lbaseentity_shared.cpp) -- same predicate, no
** stack traffic. */
LUA_API bool lua_entityisvehicle (CBaseEntity *pEntity) {
  return ToVehicleFromEntity(pEntity) != NULL;
}


/*
** Throws "Vehicle expected, got NULL entity" for a removed vehicle.  Every
** method below goes through this, so calling a Vehicle method through a stale
** Lua object raises exactly like every other entity binding in this tree does;
** it does NOT crash.
*/
LUALIB_API lua_Vehicle *luaL_checkvehicle (lua_State *L, int narg) {
  lua_Vehicle *d = lua_tovehicle(L, narg);
  if (d == NULL)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "Vehicle expected, got NULL entity");
  return d;
}

LUALIB_API lua_Vehicle *luaL_optvehicle (lua_State *L, int narg,
                                                  lua_Vehicle *def) {
  return luaL_opt(L, luaL_checkvehicle, narg, def);
}


/*
** ===========================================================================
** Per-entity extra fields (see the comment above s_pszKeyVehicleClass).
** ===========================================================================
*/

static void Vehicle_PushExtraField (lua_State *L, CBaseEntity *pEntity, const char *pszKey) {
  if (pEntity != NULL && lua_isrefvalid(L, pEntity->m_nTableReference)) {
    lua_getref(L, pEntity->m_nTableReference);
    lua_getfield(L, -1, pszKey);
    lua_remove(L, -2);
    return;
  }
  lua_pushnil(L);
}

/* Stores the value on top of the stack under pszKey and pops it. */
static void Vehicle_SetExtraField (lua_State *L, CBaseEntity *pEntity, const char *pszKey) {
  if (pEntity == NULL) {
    lua_pop(L, 1);
    return;
  }

  /* < 0 covers LUA_NOREF and the LUA_REFNIL luaL_ref() answers for a nil value;
  ** testing "== LUA_NOREF" alone is the bug described in AGENTS.md 5.4.1. */
  if (pEntity->m_nTableReference < 0) {
    lua_newtable(L);
    pEntity->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  lua_getref(L, pEntity->m_nTableReference);  /* [value table] */
  lua_insert(L, -2);                          /* [table value] */
  lua_setfield(L, -2, pszKey);                /* [table] */
  lua_pop(L, 1);
}

static bool Vehicle_GetExtraBool (lua_State *L, CBaseEntity *pEntity, const char *pszKey, bool bDefault) {
  Vehicle_PushExtraField(L, pEntity, pszKey);
  bool bValue = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : bDefault;
  lua_pop(L, 1);
  return bValue;
}

static float Vehicle_GetExtraNumber (lua_State *L, CBaseEntity *pEntity, const char *pszKey, float flDefault) {
  Vehicle_PushExtraField(L, pEntity, pszKey);
  float flValue = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : flDefault;
  lua_pop(L, 1);
  return flValue;
}

/* Leaves the stored string (or pszDefault) on the stack. */
static void Vehicle_PushExtraString (lua_State *L, CBaseEntity *pEntity, const char *pszKey, const char *pszDefault) {
  Vehicle_PushExtraField(L, pEntity, pszKey);
  if (lua_isstring(L, -1))
    return;
  lua_pop(L, 1);
  lua_pushstring(L, pszDefault);
}


/*
** ===========================================================================
** Lua table helpers.  All of them expect the table to write into on top of the
** stack.
** ===========================================================================
*/

static void LuaSetFloat (lua_State *L, const char *pszKey, float flValue) {
  lua_pushnumber(L, flValue);
  lua_setfield(L, -2, pszKey);
}

static void LuaSetInt (lua_State *L, const char *pszKey, int iValue) {
  lua_pushinteger(L, iValue);
  lua_setfield(L, -2, pszKey);
}

static void LuaSetBool (lua_State *L, const char *pszKey, bool bValue) {
  lua_pushboolean(L, bValue);
  lua_setfield(L, -2, pszKey);
}

#ifndef CLIENT_DLL
/* Only the server realm builds the VehicleParams tables, so these two are
** server-only too.  (MSVC's C4505 fires for a static function nothing calls.) */
static void LuaSetVector (lua_State *L, const char *pszKey, const Vector &vecValue) {
  lua_pushvector(L, vecValue);
  lua_setfield(L, -2, pszKey);
}

/* Reads table[pszKey] as a number.  Answers false when the key is absent. */
static bool LuaGetFloat (lua_State *L, int iTable, const char *pszKey, float *pflOut) {
  lua_getfield(L, iTable, pszKey);
  bool bFound = lua_isnumber(L, -1) != 0;
  if (bFound)
    *pflOut = (float)lua_tonumber(L, -1);
  lua_pop(L, 1);
  return bFound;
}

static bool LuaGetBool (lua_State *L, int iTable, const char *pszKey, bool *pbOut) {
  lua_getfield(L, iTable, pszKey);
  bool bFound = lua_isboolean(L, -1) != 0;
  if (bFound)
    *pbOut = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return bFound;
}

/*
** Server-side accessors.  CFourWheelVehiclePhysics documents that every getter
** answers a safe value when m_pVehicle is NULL (a vehicle whose script failed to
** parse), and GetVehicleController() answers NULL in the same case.
*/

/* Vehicles with no vphysics controller hand out these zeroed stand-ins for the
** params tables.  A namespace-scope object has static storage duration, so it is
** zero-initialised; both types are plain aggregates (declared without an
** initialiser at fourwheelvehiclephysics.cpp:386 - the same device that file uses
** in GetVehicleParams()/GetVehicleOperatingParams()). */
static const vehicle_operatingparams_t s_ZeroOperatingParams = {};
static const vehicleparams_t s_ZeroVehicleParams = {};

static CFourWheelVehiclePhysics *VehiclePhysics (lua_Vehicle *pVehicle) {
  return pVehicle != NULL ? pVehicle->GetPhysics() : NULL;
}

static IPhysicsVehicleController *VehicleController (lua_Vehicle *pVehicle) {
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(pVehicle);
  return pPhysics != NULL ? pPhysics->GetVehicleController() : NULL;
}

static CBaseServerVehicle *VehicleServerVehicle (lua_Vehicle *pVehicle) {
  if (pVehicle == NULL)
    return NULL;
  /* dynamic_cast, not static_cast: the crane's IServerVehicle is a different
  ** class and must never be reinterpreted as a CBaseServerVehicle. */
  return dynamic_cast<CBaseServerVehicle *>(pVehicle->GetServerVehicle());
}

/* Wheel index validation shared by GetWheel / GetWheelBaseHeight /
** GetWheelTotalHeight / SetSpringLength / SetWheelFriction / GetWheelContactPoint. */
static bool VehicleWheelIndexValid (CFourWheelVehiclePhysics *pPhysics, int iWheel) {
  if (pPhysics == NULL || iWheel < 0 || iWheel >= VEHICLE_LUA_MAX_WHEELS)
    return false;
  return iWheel < pPhysics->GetWheelCount();
}
#endif  // !CLIENT_DLL


/*
** ===========================================================================
** The methods.
** ===========================================================================
*/

static int Vehicle_BoostTimeLeft (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB: real engine data - m_nBoostTimeLeft is networked (RecvPropInt in
  ** game/client/c_prop_vehicle.cpp). */
  lua_pushinteger(L, luaL_checkvehicle(L, 1)->HL2SB_BoostTimeLeft());
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushinteger(L, pPhysics != NULL ? pPhysics->BoostTimeLeft() : 0);
#endif
  return 1;
}

static int Vehicle_CheckExitPoint (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);
  float flYaw = (float)luaL_checknumber(L, 2);
  int iDistance = luaL_checkint(L, 3);

#ifdef CLIENT_DLL
  /* HL2SB fallback: the exit points are parsed from the vehicle script by
  ** CBaseServerVehicle (vehicle_baseserver.cpp) and none of them are networked
  ** to the client, so the client cannot answer this.  Returning the vehicle's own
  ** origin keeps the documented "Vector" contract: an addon gets a position
  ** instead of "attempt to call a nil value" or a throw. */
  lua_pushvector(L, pVehicle->GetAbsOrigin());
#else
  Vector vecEndPoint = pVehicle->GetAbsOrigin();
  CBaseServerVehicle *pServerVehicle = VehicleServerVehicle(pVehicle);
  if (pServerVehicle != NULL)
    pServerVehicle->CheckExitPoint(flYaw, iDistance, &vecEndPoint);
  lua_pushvector(L, vecEndPoint);
#endif
  return 1;
}

static int Vehicle_EnableEngine (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);
  bool bEnable = luaL_checkboolean(L, 2) != 0;

#ifdef CLIENT_DLL
  /* HL2SB fallback: the engine lock lives in the server-side physics controller
  ** (IPhysicsVehicleController::SetEngineDisabled) and is not networked.  The
  ** flag is kept per entity so EnableEngine/IsEngineEnabled stay consistent for
  ** the client addon, but it has no effect on the engine; that is server state. */
  lua_pushboolean(L, bEnable);
  Vehicle_SetExtraField(L, VehicleToEntity(pVehicle), s_pszKeyEngineEnabled);
#else
  /* HL2SB: real engine data on the server. */
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(pVehicle);
  if (pPhysics != NULL)
    pPhysics->SetDisableEngine(!bEnable);
#endif
  return 0;
}

static int Vehicle_GetAmmo (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);

#ifdef CLIENT_DLL
  IClientVehicle *pClientVehicle = pVehicle->GetClientVehicle();
#if defined( HL2_CLIENT_DLL )
  /* HL2SB: real when the vehicle has weapons - the same accessors the HL2 HUD
  ** uses (game/client/hl2/hud_ammo.cpp).  Order (count, clip, type) follows
  ** GMod's Vehicle:GetAmmo. */
  lua_pushinteger(L, pClientVehicle != NULL ? pClientVehicle->GetPrimaryAmmoCount() : -1);
  lua_pushinteger(L, pClientVehicle != NULL ? pClientVehicle->GetPrimaryAmmoClip() : -1);
  lua_pushinteger(L, pClientVehicle != NULL ? pClientVehicle->GetPrimaryAmmoType() : -1);
#else
  lua_pushinteger(L, -1);
  lua_pushinteger(L, -1);
  lua_pushinteger(L, -1);
#endif
#else
  /* HL2SB fallback: the server-side CPropVehicleDriveable stores no ammo at all -
  ** HL2 keeps it on the driver's weapon - and the ammo accessors exist only on
  ** IClientVehicle.  -1/-1/-1 is what the client class reports for a vehicle
  ** without weapons, so both realms agree. */
  lua_pushinteger(L, -1);
  lua_pushinteger(L, -1);
  lua_pushinteger(L, -1);
#endif
  return 3;
}

static int Vehicle_GetCameraDistance (lua_State *L) {
  CBaseEntity *pEntity = VehicleToEntity(luaL_checkvehicle(L, 1));

#ifdef CLIENT_DLL
  /* HL2SB: this is the live camera distance - ClientModeShared::OverrideView reads
  ** hl2sb_veh_thirdperson_dist every frame.  The convar is global rather than per
  ** vehicle (the engine has a single vehicle camera), so a distance this addon set
  ** for this particular vehicle is kept per entity and preferred; with nothing set
  ** the convar's current value is the exact distance the camera is using. */
  lua_pushnumber(L, Vehicle_GetExtraNumber(L, pEntity, s_pszKeyCameraDistance,
                                           hl2sb_veh_thirdperson_dist.GetFloat()));
#else
  lua_pushnumber(L, Vehicle_GetExtraNumber(L, pEntity, s_pszKeyCameraDistance, VEHICLE_DEFAULT_CAMERA_DISTANCE));
#endif
  return 1;
}

static int Vehicle_GetDriver (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);

#ifdef CLIENT_DLL
  /* HL2SB: real engine data - the driver handle is networked (RecvPropEHandle
  ** m_hPlayer in game/client/c_prop_vehicle.cpp) and GetPassenger(VEHICLE_ROLE_DRIVER)
  ** reads it. */
  CBaseCombatCharacter *pDriver = pVehicle->GetPassenger(VEHICLE_ROLE_DRIVER);
#else
  /* HL2SB: real engine data - CPropVehicleDriveable::GetDriver answers the player
  ** or the NPC driver (vehicle_base.cpp:625). */
  CBaseEntity *pDriver = pVehicle->GetDriver();
#endif

  /* PushLuaInstanceSafe rather than lua_pushentity: a driver is a player and has
  ** to carry the Player methods (GMod's Vehicle:GetDriver returns a Player). */
  CBaseEntity::PushLuaInstanceSafe(L, pDriver);
  return 1;
}

static int Vehicle_GetHLSpeed (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);

#ifdef CLIENT_DLL
  /* HL2SB: the wiki defines this as "Entity:GetVelocity + Vector:Length", and the
  ** client vehicle class networks no inches/sec speed (only m_nSpeed in MPH), so
  ** the entity velocity is the real answer here. */
  lua_pushnumber(L, pVehicle->GetAbsVelocity().Length());
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(pVehicle);
  lua_pushnumber(L, pPhysics != NULL ? pPhysics->GetHLSpeed() : 0.0f);
#endif
  return 1;
}

static int Vehicle_GetMaxSpeed (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB fallback: the maximum speed is engine.engine.maxSpeed in the vehicle
  ** script, which only the server parses (CFourWheelVehiclePhysics::ParseVehicleScript);
  ** nothing about it is networked, so the client answers 0 (a vehicle that is not
  ** moving) instead of inventing a number. */
  luaL_checkvehicle(L, 1);
  lua_pushnumber(L, 0.0f);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushnumber(L, pPhysics != NULL ? (float)pPhysics->GetMaxSpeed() : 0.0f);
#endif
  return 1;
}

static int Vehicle_GetOperatingParams (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);

  lua_newtable(L);

#ifndef CLIENT_DLL
  /* HL2SB: real engine data - the vphysics operating params (fourwheelvehiclephysics.h
  ** GetVehicleOperatingParams).  Keys are GMod's (wiki Structures/OperatingParams). */
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(pVehicle);
  const vehicle_operatingparams_t &params = pPhysics != NULL
                                              ? pPhysics->GetVehicleOperatingParams()
                                              : s_ZeroOperatingParams;
  LuaSetFloat(L, "speed", params.speed);
  LuaSetFloat(L, "RPM", params.engineRPM);
  LuaSetInt(L, "gear", params.gear);
  LuaSetBool(L, "isTorqueBoosting", params.isTorqueBoosting);
  LuaSetFloat(L, "steeringAngle", params.steeringAngle);
  LuaSetInt(L, "wheelsInContact", params.wheelsInContact);
#else
  /* HL2SB fallback: the operating params come from the vphysics controller, which
  ** only exists server-side.  The client fills the same keys from what IS
  ** networked (RPM, throttle, speed, boost), and leaves the rest at neutral. */
  LuaSetFloat(L, "speed", pVehicle->GetAbsVelocity().Length());
  LuaSetFloat(L, "RPM", (float)pVehicle->HL2SB_RPM());
  LuaSetInt(L, "gear", 0);
  LuaSetBool(L, "isTorqueBoosting", false);
  LuaSetFloat(L, "steeringAngle", 0.0f);
  LuaSetInt(L, "wheelsInContact", 0);
#endif

  return 1;
}

static int Vehicle_GetPassenger (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);
  int nRole = luaL_optint(L, 2, VEHICLE_ROLE_DRIVER);

#ifdef CLIENT_DLL
  /* HL2SB: real engine data - C_PropVehicleDriveable::GetPassenger reads the
  ** networked driver handle (game/client/c_prop_vehicle.cpp:111). */
  CBaseCombatCharacter *pPassenger = pVehicle->GetPassenger(nRole);
#else
  /* HL2SB: real engine data - CBaseServerVehicle::GetPassenger over the parsed
  ** passenger roles (vehicle_baseserver.cpp). */
  CBaseServerVehicle *pServerVehicle = VehicleServerVehicle(pVehicle);
  CBaseCombatCharacter *pPassenger = pServerVehicle != NULL ? pServerVehicle->GetPassenger(nRole) : NULL;
#endif

  CBaseEntity::PushLuaInstanceSafe(L, pPassenger);
  return 1;
}

static int Vehicle_GetPassengerSeatPoint (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);
  int nRole = luaL_optint(L, 2, VEHICLE_ROLE_DRIVER);

  Vector vecPoint = pVehicle->GetAbsOrigin();
  QAngle angAngles = pVehicle->GetAbsAngles();

#ifdef CLIENT_DLL
  /* HL2SB fallback: neither IVehicle nor IClientVehicle exposes the seat points -
  ** they are read from the model's passenger roles, which CBaseServerVehicle
  ** parses server-side only - so the client answers the vehicle's own
  ** origin/angles rather than raising. */
#else
  CBaseServerVehicle *pServerVehicle = VehicleServerVehicle(pVehicle);
  if (pServerVehicle != NULL)
    pServerVehicle->GetPassengerSeatPoint(nRole, &vecPoint, &angAngles);
#endif

  lua_pushvector(L, vecPoint);
  lua_pushangle(L, angAngles);
  return 2;
}

static int Vehicle_GetRPM (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB: real engine data - m_nRPM is networked. */
  lua_pushinteger(L, luaL_checkvehicle(L, 1)->HL2SB_RPM());
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushinteger(L, pPhysics != NULL ? pPhysics->GetRPM() : 0);
#endif
  return 1;
}

static int Vehicle_GetSpeed (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB: real engine data - m_nSpeed is networked, in MPH, which is the unit
  ** the wiki documents for Vehicle:GetSpeed. */
  lua_pushinteger(L, luaL_checkvehicle(L, 1)->HL2SB_Speed());
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushinteger(L, pPhysics != NULL ? pPhysics->GetSpeed() : 0);
#endif
  return 1;
}

static int Vehicle_GetSteering (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB fallback: the steering value is m_controls.steering on the server's
  ** CFourWheelVehiclePhysics and is not networked. */
  luaL_checkvehicle(L, 1);
  lua_pushnumber(L, 0.0f);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushnumber(L, pPhysics != NULL ? pPhysics->GetSteering() : 0.0f);
#endif
  return 1;
}

static int Vehicle_GetSteeringDegrees (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB fallback: the maximum steering angle lives in the vehicle script
  ** (vehicleparams_t.steering.degreesSlow), server-side only. */
  luaL_checkvehicle(L, 1);
  lua_pushnumber(L, 0.0f);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushnumber(L, pPhysics != NULL ? pPhysics->GetSteeringDegrees() : 0.0f);
#endif
  return 1;
}

static int Vehicle_GetThirdPersonMode (lua_State *L) {
  CBaseEntity *pEntity = VehicleToEntity(luaL_checkvehicle(L, 1));

#ifdef CLIENT_DLL
  /* HL2SB: real - the camera actually honours this convar
  ** (ClientModeShared::OverrideView), so the answer is read straight from it. */
  lua_pushboolean(L, hl2sb_veh_thirdperson.GetBool());
#else
  lua_pushboolean(L, Vehicle_GetExtraBool(L, pEntity, s_pszKeyThirdPerson, false));
#endif
  return 1;
}

static int Vehicle_GetThrottle (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB: real engine data - m_flThrottle is networked. */
  lua_pushnumber(L, luaL_checkvehicle(L, 1)->HL2SB_Throttle());
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushnumber(L, pPhysics != NULL ? pPhysics->GetThrottle() : 0.0f);
#endif
  return 1;
}

static int Vehicle_GetVehicleClass (lua_State *L) {
  /* HL2SB fallback: neither vehicle class has a field for the Sandbox vehicle
  ** class name - GMod keeps it on the vehicle too (Vehicle:SetVehicleClass is
  ** "internal") - so it is stored per entity (see the comment at the key list).
  ** The empty string, not nil, keeps `veh:GetVehicleClass() == ""` comparisons
  ** and string concatenation working. */
  Vehicle_PushExtraString(L, VehicleToEntity(luaL_checkvehicle(L, 1)), s_pszKeyVehicleClass, "");
  return 1;
}

static int Vehicle_HasBoost (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB: real engine data - m_nHasBoost is networked. */
  lua_pushboolean(L, luaL_checkvehicle(L, 1)->HL2SB_HasBoost() != 0);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushboolean(L, pPhysics != NULL ? pPhysics->HasBoost() : false);
#endif
  return 1;
}

static int Vehicle_HasBrakePedal (lua_State *L) {
  CBaseEntity *pEntity = VehicleToEntity(luaL_checkvehicle(L, 1));

#ifdef CLIENT_DLL
  /* HL2SB fallback: bHasBrakePedal is a live control state on the server's
  ** CFourWheelVehiclePhysics (vehicle_controlparams_t) and is not networked; the
  ** client keeps whatever SetHasBrakePedal was told, defaulting to false. */
  lua_pushboolean(L, Vehicle_GetExtraBool(L, pEntity, s_pszKeyBrakePedal, false));
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushboolean(L, pPhysics != NULL ? pPhysics->GetVehicleControls().bHasBrakePedal : false);
#endif
  return 1;
}

static int Vehicle_IsBoosting (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB fallback: IsBoosting compares the operating boost delay against the
  ** engine boost delay (fourwheelvehiclephysics.cpp:1453); both are server-side
  ** vphysics state, nothing about them is networked, so the client answers false. */
  luaL_checkvehicle(L, 1);
  lua_pushboolean(L, false);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushboolean(L, pPhysics != NULL ? pPhysics->IsBoosting() : false);
#endif
  return 1;
}

static int Vehicle_IsEngineEnabled (lua_State *L) {
  CBaseEntity *pEntity = VehicleToEntity(luaL_checkvehicle(L, 1));

#ifdef CLIENT_DLL
  /* HL2SB fallback: see Vehicle_EnableEngine - the flag is stored per entity and
  ** has no engine effect on the client.  Defaults to true: GMod vehicles can be
  ** started unless something disabled them. */
  lua_pushboolean(L, Vehicle_GetExtraBool(L, pEntity, s_pszKeyEngineEnabled, true));
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushboolean(L, pPhysics != NULL ? !pPhysics->IsEngineDisabled() : true);
#endif
  return 1;
}

static int Vehicle_IsEngineStarted (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB fallback: the engine on/off flag is CFourWheelVehiclePhysics::m_bIsOn
  ** (CPropVehicleDriveable::IsEngineOn), which is not networked to
  ** C_PropVehicleDriveable at all. */
  luaL_checkvehicle(L, 1);
  lua_pushboolean(L, false);
#else
  /* HL2SB: real engine data. */
  lua_pushboolean(L, luaL_checkvehicle(L, 1)->IsEngineOn());
#endif
  return 1;
}

static int Vehicle_IsValidVehicle (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);

#ifdef CLIENT_DLL
  /* HL2SB fallback: GMod's "fully initialised" test is whether the server's
  ** vehicle physics controller exists.  The client has no controller, and by the
  ** time this metatable is handed out the entity exists, so it answers true. */
  lua_pushboolean(L, pVehicle != NULL);
#else
  /* HL2SB: real engine data - m_pVehicle is the vphysics controller and stays
  ** NULL until Initialize()/ParseVehicleScript() succeeded, which is exactly the
  ** "not usable yet" state the wiki describes. */
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(pVehicle);
  lua_pushboolean(L, pPhysics != NULL && pPhysics->IsVehiclePhysicsInitialized());
#endif
  return 1;
}

static int Vehicle_IsVehicleBodyInWater (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB fallback: CPropVehicleDriveable::IsVehicleBodyInWater is answered from
  ** m_WaterData (game/server/hl2/vehicle_jeep.h:50), a server-side water trace;
  ** nothing about it reaches the client vehicle class. */
  luaL_checkvehicle(L, 1);
  lua_pushboolean(L, false);
#else
  /* HL2SB: real engine data for the vehicles that implement it (the jeep, the
  ** SDK jeep and the CS jeep override it with their water data; the base class
  ** answers false, see vehicle_base.h:208). */
  lua_pushboolean(L, luaL_checkvehicle(L, 1)->IsVehicleBodyInWater());
#endif
  return 1;
}

static int Vehicle_ReleaseHandbrake (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB: no-op - the handbrake is vehicle_controlparams_t.handbrake in the
  ** server's CFourWheelVehiclePhysics, applied by the server's Think. */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (pPhysics != NULL)
    pPhysics->ReleaseHandbrake();
#endif
  return 0;
}

static int Vehicle_SetBoost (lua_State *L) {
  float flBoost = (float)luaL_checknumber(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB: no-op - boost is m_controls.boost on the server and is driven by the
  ** server's UpdateDriverControls. */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (pPhysics != NULL)
    pPhysics->SetBoost(flBoost);
#endif
  return 0;
}

static int Vehicle_SetCameraDistance (lua_State *L) {
  float flDistance = (float)luaL_checknumber(L, 2);
  CBaseEntity *pEntity = VehicleToEntity(luaL_checkvehicle(L, 1));

  lua_pushnumber(L, flDistance);
  Vehicle_SetExtraField(L, pEntity, s_pszKeyCameraDistance);

#ifdef CLIENT_DLL
  /* HL2SB: this is the live camera distance - ClientModeShared::OverrideView reads
  ** hl2sb_veh_thirdperson_dist every frame, so the camera really moves. */
  hl2sb_veh_thirdperson_dist.SetValue(flDistance);
#endif
  return 0;
}

static int Vehicle_SetHandbrake (lua_State *L) {
  bool bHandbrake = luaL_checkboolean(L, 2) != 0;

#ifdef CLIENT_DLL
  /* HL2SB: no-op - see Vehicle_ReleaseHandbrake. */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (pPhysics != NULL)
    pPhysics->SetHandbrake(bHandbrake);
#endif
  return 0;
}

static int Vehicle_SetHasBrakePedal (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);
  bool bBrakePedal = luaL_checkboolean(L, 2) != 0;

  /* Stored per entity in both realms: the server writes the live control state
  ** below, and the client needs somewhere to keep the answer HasBrakePedal gives
  ** because it has no control state of its own. */
  lua_pushboolean(L, bBrakePedal);
  Vehicle_SetExtraField(L, VehicleToEntity(pVehicle), s_pszKeyBrakePedal);

#ifndef CLIENT_DLL
  /* HL2SB: real engine data - vehicle_controlparams_t.bHasBrakePedal, read by the
  ** server's UpdateDriverControls (fourwheelvehiclephysics.cpp). */
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(pVehicle);
  if (pPhysics != NULL)
    pPhysics->SetHasBrakePedal(bBrakePedal);
#endif
  return 0;
}

static int Vehicle_SetMaxReverseThrottle (lua_State *L) {
  float flMaxReverseThrottle = (float)luaL_checknumber(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB: no-op - CFourWheelVehiclePhysics::m_flMaxRevThrottle is server state. */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (pPhysics != NULL)
    pPhysics->SetMaxReverseThrottle(flMaxReverseThrottle);
#endif
  return 0;
}

static int Vehicle_SetMaxThrottle (lua_State *L) {
  float flMaxThrottle = (float)luaL_checknumber(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB: no-op - CFourWheelVehiclePhysics::m_maxThrottle is server state. */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (pPhysics != NULL)
    pPhysics->SetMaxThrottle(flMaxThrottle);
#endif
  return 0;
}

static int Vehicle_SetSpringLength (lua_State *L) {
  int iWheel = luaL_checkint(L, 2);
  float flLength = (float)luaL_checknumber(L, 3);

#ifdef CLIENT_DLL
  /* HL2SB: no-op - the wheels and their springs exist only in the server-side
  ** IPhysicsVehicleController (vphysics), which the client has no handle on. */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  IPhysicsVehicleController *pController = VehicleController(luaL_checkvehicle(L, 1));
  if (pController != NULL && VehicleWheelIndexValid(pPhysics, iWheel))
    pController->SetSpringLength(iWheel, flLength);
#endif
  return 0;
}

static int Vehicle_SetSteering (lua_State *L) {
  float flFront = (float)luaL_checknumber(L, 2);
  /* GMod takes two arguments; the second is optional here so a one-argument call
  ** cannot raise (0 means "no rate", i.e. set the steering directly). */
  float flRear = (float)luaL_optnumber(L, 3, 0.0f);

#ifdef CLIENT_DLL
  /* HL2SB: no-op - steering is m_controls.steering on the server. */
  luaL_checkvehicle(L, 1);
#else
  /* HL2SB: the engine's signature is SetSteering( steering, steeringRate )
  ** (fourwheelvehiclephysics.cpp:459) - there is no separate rear-wheel channel.
  ** GMod's first argument (front) maps to the steering value and its second
  ** (rear) to the rate, documented rather than silently dropped. */
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (pPhysics != NULL)
    pPhysics->SetSteering(flFront, flRear);
#endif
  return 0;
}

static int Vehicle_SetSteeringDegrees (lua_State *L) {
  float flDegrees = (float)luaL_checknumber(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB: no-op - sets vehicleparams_t.steering.degreesSlow/Fast, server-side. */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (pPhysics != NULL)
    pPhysics->SetSteeringDegrees(flDegrees);
#endif
  return 0;
}

static int Vehicle_SetThirdPersonMode (lua_State *L) {
  bool bEnable = luaL_checkboolean(L, 2) != 0;
  CBaseEntity *pEntity = VehicleToEntity(luaL_checkvehicle(L, 1));

  lua_pushboolean(L, bEnable);
  Vehicle_SetExtraField(L, pEntity, s_pszKeyThirdPerson);

#ifdef CLIENT_DLL
  /* HL2SB: the engine has one vehicle camera, driven by the client convar pair
  ** (ClientModeShared::OverrideView).  Setting the convar is what actually moves
  ** the camera; the per-entity copy above keeps the value per vehicle for the
  ** server realm and for GetThirdPersonMode's fallback.  This is the same convar
  ** lua/autorun/client/hl2sb_vehicle_thirdperson.lua writes. */
  hl2sb_veh_thirdperson.SetValue(bEnable ? 1 : 0);
#endif
  return 0;
}

static int Vehicle_SetThrottle (lua_State *L) {
  float flThrottle = (float)luaL_checknumber(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB: no-op - throttle is m_controls.throttle on the server. */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (pPhysics != NULL)
    pPhysics->SetThrottle(flThrottle);
#endif
  return 0;
}

static int Vehicle_SetVehicleClass (lua_State *L) {
  /* HL2SB fallback: stored per entity, see Vehicle_GetVehicleClass. */
  luaL_checkvehicle(L, 1);
  luaL_checkstring(L, 2);
  lua_pushvalue(L, 2);
  Vehicle_SetExtraField(L, VehicleToEntity(luaL_checkvehicle(L, 1)), s_pszKeyVehicleClass);
  return 0;
}

static int Vehicle_SetVehicleEntryAnim (lua_State *L) {
  bool bOn = luaL_checkboolean(L, 2) != 0;

#ifdef CLIENT_DLL
  /* HL2SB fallback: C_PropVehicleDriveable has no entry-anim setter - the flag is
  ** the server's m_bEnterAnimOn (networked read-only here).  A no-op keeps the
  ** call from raising. */
  luaL_checkvehicle(L, 1);
#else
  /* HL2SB: real engine data - IDrivableVehicle::SetVehicleEntryAnim
  ** (vehicle_base.h:224). */
  luaL_checkvehicle(L, 1)->SetVehicleEntryAnim(bOn);
#endif
  return 0;
}

static int Vehicle_SetWheelFriction (lua_State *L) {
  int iWheel = luaL_checkint(L, 2);
  float flFriction = (float)luaL_checknumber(L, 3);

#ifdef CLIENT_DLL
  /* HL2SB: no-op - see Vehicle_SetSpringLength.  (The wiki itself notes that this
  ** function "may be broken" in GMod.) */
  luaL_checkvehicle(L, 1);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  IPhysicsVehicleController *pController = VehicleController(luaL_checkvehicle(L, 1));
  if (pController != NULL && VehicleWheelIndexValid(pPhysics, iWheel))
    pController->SetWheelFriction(iWheel, flFriction);
#endif
  return 0;
}

static int Vehicle_StartEngine (lua_State *L) {
  bool bStart = luaL_checkboolean(L, 2) != 0;

#ifdef CLIENT_DLL
  /* HL2SB: no-op - starting the engine is a server action
  ** (CPropVehicleDriveable::StartEngine/StopEngine). */
  luaL_checkvehicle(L, 1);
#else
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);
  if (bStart)
    pVehicle->StartEngine();
  else
    pVehicle->StopEngine();
#endif
  return 0;
}


#ifndef CLIENT_DLL
/*
** ===========================================================================
** VehicleParams / OperatingParams tables.
**
** Field names follow Garry's Mod: the top level and the axle members are the
** wiki's (Structures/VehicleParams, VehicleParamsAxle, VehicleParamsEngine), the
** body and steering members are the engine's own struct member names (they are
** not nested in this structure).  The axle members carry the struct prefix GMod
** uses for them (wheels_radius, suspension_springConstant, ...).
**
** These three live in the server realm only: the whole structure is produced by
** parsing the vehicle script, which is server-side.
** ===========================================================================
*/

static void LuaPushVehicleAxle (lua_State *L, const vehicle_axleparams_t &axle) {
  lua_newtable(L);
  LuaSetVector(L, "offset", axle.offset);
  LuaSetVector(L, "wheelOffset", axle.wheelOffset);
  LuaSetVector(L, "raytraceCenterOffset", axle.raytraceCenterOffset);
  LuaSetVector(L, "raytraceOffset", axle.raytraceOffset);
  LuaSetFloat(L, "torqueFactor", axle.torqueFactor);
  LuaSetFloat(L, "brakeFactor", axle.brakeFactor);

  LuaSetFloat(L, "wheels_radius", axle.wheels.radius);
  LuaSetFloat(L, "wheels_mass", axle.wheels.mass);
  LuaSetFloat(L, "wheels_inertia", axle.wheels.inertia);
  LuaSetFloat(L, "wheels_damping", axle.wheels.damping);
  LuaSetFloat(L, "wheels_rotdamping", axle.wheels.rotdamping);
  LuaSetFloat(L, "wheels_frictionScale", axle.wheels.frictionScale);
  LuaSetInt(L, "wheels_materialIndex", axle.wheels.materialIndex);
  LuaSetInt(L, "wheels_brakeMaterialIndex", axle.wheels.brakeMaterialIndex);
  LuaSetInt(L, "wheels_skidMaterialIndex", axle.wheels.skidMaterialIndex);
  LuaSetFloat(L, "wheels_springAdditionalLength", axle.wheels.springAdditionalLength);

  LuaSetFloat(L, "suspension_springConstant", axle.suspension.springConstant);
  LuaSetFloat(L, "suspension_springDamping", axle.suspension.springDamping);
  LuaSetFloat(L, "suspension_stabilizerConstant", axle.suspension.stabilizerConstant);
  LuaSetFloat(L, "suspension_springDampingCompression", axle.suspension.springDampingCompression);
  LuaSetFloat(L, "suspension_maxBodyForce", axle.suspension.maxBodyForce);
}

static void LuaPushVehicleParams (lua_State *L, const vehicleparams_t &params) {
  lua_newtable(L);
  LuaSetInt(L, "axleCount", params.axleCount);
  LuaSetInt(L, "wheelsPerAxle", params.wheelsPerAxle);

  lua_newtable(L);
  int nAxles = params.axleCount;
  if (nAxles < 0)
    nAxles = 0;
  if (nAxles > VEHICLE_MAX_AXLE_COUNT)
    nAxles = VEHICLE_MAX_AXLE_COUNT;
  for (int i = 0; i < nAxles; ++i) {
    LuaPushVehicleAxle(L, params.axles[i]);
    lua_rawseti(L, -2, i + 1);  /* GMod's axles table is 1-based in Lua */
  }
  lua_setfield(L, -2, "axles");

  lua_newtable(L);
  LuaSetVector(L, "massCenterOverride", params.body.massCenterOverride);
  LuaSetFloat(L, "massOverride", params.body.massOverride);
  LuaSetFloat(L, "addGravity", params.body.addGravity);
  LuaSetFloat(L, "tiltForce", params.body.tiltForce);
  LuaSetFloat(L, "tiltForceHeight", params.body.tiltForceHeight);
  LuaSetFloat(L, "counterTorqueFactor", params.body.counterTorqueFactor);
  LuaSetFloat(L, "keepUprightTorque", params.body.keepUprightTorque);
  LuaSetFloat(L, "maxAngularVelocity", params.body.maxAngularVelocity);
  lua_setfield(L, -2, "body");

  lua_newtable(L);
  LuaSetFloat(L, "horsepower", params.engine.horsepower);
  LuaSetFloat(L, "maxSpeed", params.engine.maxSpeed);
  LuaSetFloat(L, "maxRevSpeed", params.engine.maxRevSpeed);
  LuaSetFloat(L, "maxRPM", params.engine.maxRPM);
  LuaSetFloat(L, "axleRatio", params.engine.axleRatio);
  LuaSetFloat(L, "throttleTime", params.engine.throttleTime);
  LuaSetInt(L, "gearCount", params.engine.gearCount);
  lua_newtable(L);
  int nGears = params.engine.gearCount;
  if (nGears < 0)
    nGears = 0;
  if (nGears > VEHICLE_MAX_GEAR_COUNT)
    nGears = VEHICLE_MAX_GEAR_COUNT;
  for (int i = 0; i < nGears; ++i) {
    lua_pushnumber(L, params.engine.gearRatio[i]);
    lua_rawseti(L, -2, i + 1);
  }
  lua_setfield(L, -2, "gearRatio");
  LuaSetFloat(L, "shiftUpRPM", params.engine.shiftUpRPM);
  LuaSetFloat(L, "shiftDownRPM", params.engine.shiftDownRPM);
  LuaSetFloat(L, "boostForce", params.engine.boostForce);
  LuaSetFloat(L, "boostDuration", params.engine.boostDuration);
  LuaSetFloat(L, "boostDelay", params.engine.boostDelay);
  LuaSetFloat(L, "boostMaxSpeed", params.engine.boostMaxSpeed);
  LuaSetFloat(L, "autobrakeSpeedGain", params.engine.autobrakeSpeedGain);
  LuaSetFloat(L, "autobrakeSpeedFactor", params.engine.autobrakeSpeedFactor);
  LuaSetBool(L, "torqueBoost", params.engine.torqueBoost);
  LuaSetBool(L, "isAutoTransmission", params.engine.isAutoTransmission);
  lua_setfield(L, -2, "engine");

  lua_newtable(L);
  LuaSetFloat(L, "degreesSlow", params.steering.degreesSlow);
  LuaSetFloat(L, "degreesFast", params.steering.degreesFast);
  LuaSetFloat(L, "degreesBoost", params.steering.degreesBoost);
  LuaSetFloat(L, "steeringRateSlow", params.steering.steeringRateSlow);
  LuaSetFloat(L, "steeringRateFast", params.steering.steeringRateFast);
  LuaSetFloat(L, "steeringRestRateSlow", params.steering.steeringRestRateSlow);
  LuaSetFloat(L, "steeringRestRateFast", params.steering.steeringRestRateFast);
  LuaSetFloat(L, "speedSlow", params.steering.speedSlow);
  LuaSetFloat(L, "speedFast", params.steering.speedFast);
  LuaSetFloat(L, "turnThrottleReduceSlow", params.steering.turnThrottleReduceSlow);
  LuaSetFloat(L, "turnThrottleReduceFast", params.steering.turnThrottleReduceFast);
  LuaSetFloat(L, "brakeSteeringRateFactor", params.steering.brakeSteeringRateFactor);
  LuaSetFloat(L, "throttleSteeringRestRateFactor", params.steering.throttleSteeringRestRateFactor);
  LuaSetFloat(L, "powerSlideAccel", params.steering.powerSlideAccel);
  LuaSetFloat(L, "boostSteeringRestRateFactor", params.steering.boostSteeringRestRateFactor);
  LuaSetFloat(L, "boostSteeringRateFactor", params.steering.boostSteeringRateFactor);
  LuaSetFloat(L, "steeringExponent", params.steering.steeringExponent);
  LuaSetBool(L, "isSkidAllowed", params.steering.isSkidAllowed);
  LuaSetBool(L, "dustCloud", params.steering.dustCloud);
  lua_setfield(L, -2, "steering");
}

/* Applies the subset of a VehicleParams table this engine can honour.  The wiki
** says the same thing about GMod ("Not all variables from the VehicleParams
** structure can be set"); axleCount/wheelsPerAxle/axles are read-only here
** because the wheel objects were created from the script at spawn time. */
static void LuaApplyVehicleParams (lua_State *L, int iTable, vehicleparams_t &params) {
  float flValue = 0.0f;
  bool bValue = false;

  if (!lua_istable(L, iTable))
    return;

  lua_getfield(L, iTable, "body");
  if (lua_istable(L, -1)) {
    int iBody = lua_gettop(L);
    if (LuaGetFloat(L, iBody, "massOverride", &flValue))          params.body.massOverride = flValue;
    if (LuaGetFloat(L, iBody, "addGravity", &flValue))           params.body.addGravity = flValue;
    if (LuaGetFloat(L, iBody, "tiltForce", &flValue))            params.body.tiltForce = flValue;
    if (LuaGetFloat(L, iBody, "tiltForceHeight", &flValue))      params.body.tiltForceHeight = flValue;
    if (LuaGetFloat(L, iBody, "counterTorqueFactor", &flValue))  params.body.counterTorqueFactor = flValue;
    if (LuaGetFloat(L, iBody, "keepUprightTorque", &flValue))    params.body.keepUprightTorque = flValue;
    if (LuaGetFloat(L, iBody, "maxAngularVelocity", &flValue))   params.body.maxAngularVelocity = flValue;
  }
  lua_pop(L, 1);

  lua_getfield(L, iTable, "engine");
  if (lua_istable(L, -1)) {
    int iEngine = lua_gettop(L);
    if (LuaGetFloat(L, iEngine, "horsepower", &flValue))    params.engine.horsepower = flValue;
    /* The wiki documents that SetVehicleParams takes these three in miles per
    ** hour while GetVehicleParams returns them in hammer units per second; the
    ** engine's own vehicleparams_t is in HU/s (CFourWheelVehiclePhysics::GetMaxSpeed
    ** converts with INS2MPH), so convert on the way in. */
    if (LuaGetFloat(L, iEngine, "maxSpeed", &flValue))      params.engine.maxSpeed = MPH2INS(flValue);
    if (LuaGetFloat(L, iEngine, "maxRevSpeed", &flValue))   params.engine.maxRevSpeed = MPH2INS(flValue);
    if (LuaGetFloat(L, iEngine, "boostMaxSpeed", &flValue)) params.engine.boostMaxSpeed = MPH2INS(flValue);
    if (LuaGetFloat(L, iEngine, "maxRPM", &flValue))        params.engine.maxRPM = flValue;
    if (LuaGetFloat(L, iEngine, "axleRatio", &flValue))     params.engine.axleRatio = flValue;
    if (LuaGetFloat(L, iEngine, "throttleTime", &flValue))  params.engine.throttleTime = flValue;
    if (LuaGetFloat(L, iEngine, "shiftUpRPM", &flValue))    params.engine.shiftUpRPM = flValue;
    if (LuaGetFloat(L, iEngine, "shiftDownRPM", &flValue))  params.engine.shiftDownRPM = flValue;
    if (LuaGetFloat(L, iEngine, "boostForce", &flValue))    params.engine.boostForce = flValue;
    if (LuaGetFloat(L, iEngine, "boostDuration", &flValue)) params.engine.boostDuration = flValue;
    if (LuaGetFloat(L, iEngine, "boostDelay", &flValue))    params.engine.boostDelay = flValue;
    if (LuaGetFloat(L, iEngine, "autobrakeSpeedGain", &flValue))   params.engine.autobrakeSpeedGain = flValue;
    if (LuaGetFloat(L, iEngine, "autobrakeSpeedFactor", &flValue)) params.engine.autobrakeSpeedFactor = flValue;
    if (LuaGetBool(L, iEngine, "torqueBoost", &bValue))            params.engine.torqueBoost = bValue;
    if (LuaGetBool(L, iEngine, "isAutoTransmission", &bValue))     params.engine.isAutoTransmission = bValue;

    lua_getfield(L, iEngine, "gearRatio");
    if (lua_istable(L, -1)) {
      int nGears = params.engine.gearCount;
      if (nGears < 0)
        nGears = 0;
      if (nGears > VEHICLE_MAX_GEAR_COUNT)
        nGears = VEHICLE_MAX_GEAR_COUNT;
      for (int i = 0; i < nGears; ++i) {
        lua_rawgeti(L, -1, i + 1);
        if (lua_isnumber(L, -1))
          params.engine.gearRatio[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  lua_getfield(L, iTable, "steering");
  if (lua_istable(L, -1)) {
    int iSteering = lua_gettop(L);
    if (LuaGetFloat(L, iSteering, "degreesSlow", &flValue))   params.steering.degreesSlow = flValue;
    if (LuaGetFloat(L, iSteering, "degreesFast", &flValue))   params.steering.degreesFast = flValue;
    if (LuaGetFloat(L, iSteering, "degreesBoost", &flValue))  params.steering.degreesBoost = flValue;
    if (LuaGetFloat(L, iSteering, "steeringRateSlow", &flValue))          params.steering.steeringRateSlow = flValue;
    if (LuaGetFloat(L, iSteering, "steeringRateFast", &flValue))          params.steering.steeringRateFast = flValue;
    if (LuaGetFloat(L, iSteering, "steeringRestRateSlow", &flValue))      params.steering.steeringRestRateSlow = flValue;
    if (LuaGetFloat(L, iSteering, "steeringRestRateFast", &flValue))      params.steering.steeringRestRateFast = flValue;
    if (LuaGetFloat(L, iSteering, "speedSlow", &flValue))                 params.steering.speedSlow = flValue;
    if (LuaGetFloat(L, iSteering, "speedFast", &flValue))                 params.steering.speedFast = flValue;
    if (LuaGetFloat(L, iSteering, "turnThrottleReduceSlow", &flValue))    params.steering.turnThrottleReduceSlow = flValue;
    if (LuaGetFloat(L, iSteering, "turnThrottleReduceFast", &flValue))    params.steering.turnThrottleReduceFast = flValue;
    if (LuaGetFloat(L, iSteering, "brakeSteeringRateFactor", &flValue))   params.steering.brakeSteeringRateFactor = flValue;
    if (LuaGetFloat(L, iSteering, "throttleSteeringRestRateFactor", &flValue)) params.steering.throttleSteeringRestRateFactor = flValue;
    if (LuaGetFloat(L, iSteering, "powerSlideAccel", &flValue))           params.steering.powerSlideAccel = flValue;
    if (LuaGetFloat(L, iSteering, "boostSteeringRestRateFactor", &flValue)) params.steering.boostSteeringRestRateFactor = flValue;
    if (LuaGetFloat(L, iSteering, "boostSteeringRateFactor", &flValue))   params.steering.boostSteeringRateFactor = flValue;
    if (LuaGetFloat(L, iSteering, "steeringExponent", &flValue))          params.steering.steeringExponent = flValue;
    if (LuaGetBool(L, iSteering, "isSkidAllowed", &bValue))               params.steering.isSkidAllowed = bValue;
    if (LuaGetBool(L, iSteering, "dustCloud", &bValue))                   params.steering.dustCloud = bValue;
  }
  lua_pop(L, 1);

  lua_getfield(L, iTable, "axles");
  if (lua_istable(L, -1)) {
    int nAxles = params.axleCount;
    if (nAxles < 0)
      nAxles = 0;
    if (nAxles > VEHICLE_MAX_AXLE_COUNT)
      nAxles = VEHICLE_MAX_AXLE_COUNT;
    for (int i = 0; i < nAxles; ++i) {
      lua_rawgeti(L, -1, i + 1);
      if (lua_istable(L, -1)) {
        int iAxle = lua_gettop(L);
        if (LuaGetFloat(L, iAxle, "torqueFactor", &flValue))               params.axles[i].torqueFactor = flValue;
        if (LuaGetFloat(L, iAxle, "brakeFactor", &flValue))                params.axles[i].brakeFactor = flValue;
        if (LuaGetFloat(L, iAxle, "wheels_frictionScale", &flValue))       params.axles[i].wheels.frictionScale = flValue;
        if (LuaGetFloat(L, iAxle, "wheels_radius", &flValue))              params.axles[i].wheels.radius = flValue;
        if (LuaGetFloat(L, iAxle, "wheels_mass", &flValue))                params.axles[i].wheels.mass = flValue;
        if (LuaGetFloat(L, iAxle, "wheels_springAdditionalLength", &flValue)) params.axles[i].wheels.springAdditionalLength = flValue;
        if (LuaGetFloat(L, iAxle, "suspension_springConstant", &flValue))  params.axles[i].suspension.springConstant = flValue;
        if (LuaGetFloat(L, iAxle, "suspension_springDamping", &flValue))   params.axles[i].suspension.springDamping = flValue;
        if (LuaGetFloat(L, iAxle, "suspension_stabilizerConstant", &flValue)) params.axles[i].suspension.stabilizerConstant = flValue;
        if (LuaGetFloat(L, iAxle, "suspension_maxBodyForce", &flValue))    params.axles[i].suspension.maxBodyForce = flValue;
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
}
#endif  // !CLIENT_DLL


static int Vehicle_GetVehicleParams (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB fallback: vehicleparams_t is produced by parsing the vehicle script
  ** (CFourWheelVehiclePhysics::ParseVehicleScript) and is server-side only.  The
  ** client answers the same table shape, zeroed, so addons that index
  ** params.engine.maxSpeed or iterate params.axles keep working. */
  luaL_checkvehicle(L, 1);
  lua_newtable(L);
  LuaSetInt(L, "axleCount", 0);
  LuaSetInt(L, "wheelsPerAxle", 0);
  lua_newtable(L);
  lua_setfield(L, -2, "axles");
  lua_newtable(L);
  lua_setfield(L, -2, "body");
  lua_newtable(L);
  lua_setfield(L, -2, "engine");
  lua_newtable(L);
  lua_setfield(L, -2, "steering");
#else
  /* HL2SB: real engine data. */
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  LuaPushVehicleParams(L, pPhysics != NULL ? pPhysics->GetVehicleParams() : s_ZeroVehicleParams);
#endif
  return 1;
}

static int Vehicle_SetVehicleParams (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);

#ifdef CLIENT_DLL
  /* HL2SB: no-op fallback - the parameters live in the server's vphysics
  ** controller, so the client has nothing to write them to.  The call is still
  ** validated (Vehicle + table) so a bad call still reports itself. */
  luaL_checkvehicle(L, 1);
#else
  /* HL2SB: real engine data - GetVehicleParamsForChange is the vphysics entry
  ** point for live tuning (public/vphysics/vehicles.h:84). */
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(pVehicle);
  IPhysicsVehicleController *pController = VehicleController(pVehicle);
  if (pController != NULL)
    LuaApplyVehicleParams(L, 2, pController->GetVehicleParamsForChange());
#endif
  return 0;
}


static int Vehicle_GetVehicleViewPosition (lua_State *L) {
  lua_Vehicle *pVehicle = luaL_checkvehicle(L, 1);
  int nRole = luaL_optint(L, 2, VEHICLE_ROLE_DRIVER);

  /* HL2SB: seed the outputs with the vehicle's own origin/angles.  Both
  ** implementations only fill them in when there is a driver to smooth for: the
  ** server one asserts on a non-player passenger (vehicle_base.cpp:1219) and the
  ** client one writes nothing unless the networked driver is the local player
  ** (c_prop_vehicle.cpp:206).  Without the seed an unseated vehicle would hand
  ** Lua uninitialised memory. */
  Vector vecOrigin = pVehicle->GetAbsOrigin();
  QAngle angAngles = pVehicle->GetAbsAngles();
  float flFOV = 0.0f;

#ifdef CLIENT_DLL
  pVehicle->GetVehicleViewPosition(nRole, &vecOrigin, &angAngles, &flFOV);
#else
  CBaseServerVehicle *pServerVehicle = VehicleServerVehicle(pVehicle);
  if (pServerVehicle != NULL) {
    CBaseCombatCharacter *pPassenger = pServerVehicle->GetPassenger(nRole);
    if (pPassenger != NULL && pPassenger->IsPlayer())
      pServerVehicle->GetVehicleViewPosition(nRole, &vecOrigin, &angAngles, &flFOV);
  }
#endif

  lua_pushvector(L, vecOrigin);
  lua_pushangle(L, angAngles);
  lua_pushnumber(L, flFOV);
  return 3;
}

static int Vehicle_GetWheel (lua_State *L) {
  int iWheel = luaL_checkint(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB fallback: the wheel IPhysicsObjects are created by vphysics on the
  ** server (CFourWheelVehiclePhysics::Initialize); the client has none. */
  luaL_checkvehicle(L, 1);
  lua_pushnil(L);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  if (VehicleWheelIndexValid(pPhysics, iWheel))
    lua_pushphysicsobject(L, pPhysics->GetWheel(iWheel));
  else
    lua_pushnil(L);
#endif
  return 1;
}

static int Vehicle_GetWheelBaseHeight (lua_State *L) {
  int iWheel = luaL_checkint(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB fallback: computed from the wheel offsets in the vehicle script,
  ** server-side only (fourwheelvehiclephysics.cpp CalcWheelData). */
  luaL_checkvehicle(L, 1);
  lua_pushnumber(L, 0.0f);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushnumber(L, VehicleWheelIndexValid(pPhysics, iWheel) ? pPhysics->GetWheelBaseHeight(iWheel) : 0.0f);
#endif
  return 1;
}

static int Vehicle_GetWheelContactPoint (lua_State *L) {
  int iWheel = luaL_checkint(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB fallback: the contact points are raycast in vphysics against the wheel
  ** objects, which exist only server-side. */
  luaL_checkvehicle(L, 1);
  lua_pushvector(L, vec3_origin);
  lua_pushinteger(L, 0);
  lua_pushboolean(L, false);
#else
  Vector vecContactPoint = vec3_origin;
  int iSurfaceProps = 0;
  bool bOnGround = false;

  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  IPhysicsVehicleController *pController = VehicleController(luaL_checkvehicle(L, 1));
  if (pController != NULL && VehicleWheelIndexValid(pPhysics, iWheel))
    bOnGround = pController->GetWheelContactPoint(iWheel, &vecContactPoint, &iSurfaceProps);

  lua_pushvector(L, vecContactPoint);
  lua_pushinteger(L, iSurfaceProps);
  lua_pushboolean(L, bOnGround);
#endif
  return 3;
}

static int Vehicle_GetWheelCount (lua_State *L) {
#ifdef CLIENT_DLL
  /* HL2SB fallback: no wheel data on the client at all - answer 0 (a vehicle
  ** with no wheels) rather than a wrong number. */
  luaL_checkvehicle(L, 1);
  lua_pushinteger(L, 0);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushinteger(L, pPhysics != NULL ? pPhysics->GetWheelCount() : 0);
#endif
  return 1;
}

static int Vehicle_GetWheelTotalHeight (lua_State *L) {
  int iWheel = luaL_checkint(L, 2);

#ifdef CLIENT_DLL
  /* HL2SB fallback: see Vehicle_GetWheelBaseHeight. */
  luaL_checkvehicle(L, 1);
  lua_pushnumber(L, 0.0f);
#else
  CFourWheelVehiclePhysics *pPhysics = VehiclePhysics(luaL_checkvehicle(L, 1));
  lua_pushnumber(L, VehicleWheelIndexValid(pPhysics, iWheel) ? pPhysics->GetWheelTotalHeight(iWheel) : 0.0f);
#endif
  return 1;
}


/*
** ===========================================================================
** The metatable.
** ===========================================================================
*/

static int Vehicle___index (lua_State *L) {
  const char *pszField = luaL_checkstring(L, 2);
  lua_Vehicle *pVehicle = lua_tovehicle(L, 1);

  if (pVehicle != NULL) {
    CBaseEntity *pEntity = VehicleToEntity(pVehicle);

    /* 1. the entity's own Lua field table (custom fields, plus the Vehicle
    **    extras stored above). */
    if (lua_isrefvalid(L, pEntity->m_nTableReference)) {
      lua_getref(L, pEntity->m_nTableReference);
      lua_getfield(L, -1, pszField);
      if (!lua_isnil(L, -1))
        return 1;
      lua_pop(L, 2);
    }

    /* 2..4. the class chain, in the same order CHL2MP_Player___index walks it:
    **       the Vehicle methods, then the CBaseAnimating ones, then the
    **       CBaseEntity ones (lua_pushentity installs CBaseEntity for every
    **       entity, so a vehicle has to reach it explicitly). */
    static const char *s_pszChain[] = {"Vehicle", "CBaseAnimating", "CBaseEntity"};
    for (int i = 0; i < ARRAYSIZE(s_pszChain); ++i) {
      luaL_getmetatable(L, s_pszChain[i]);
      if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        continue;
      }
      lua_getfield(L, -1, pszField);
      lua_remove(L, -2);
      if (!lua_isnil(L, -1))
        return 1;
      lua_pop(L, 1);
    }
  }

  /* 5. Everything else -- and every lookup on a removed vehicle -- is answered
  **    by CBaseEntity's own __index: it owns the engine field reads (m_iHealth,
  **    m_iClassname, ...) and the GMod-compatible behaviour for a NULL entity
  **    (IsValid answers false, every other key is a function answering false). */
  luaL_getmetatable(L, "CBaseEntity");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }
  lua_getfield(L, -1, "__index");
  lua_remove(L, -2);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_call(L, 2, 1);
  return 1;
}

static int Vehicle___newindex (lua_State *L) {
  /* HL2SB: delegate to CBaseEntity's own __newindex.  It owns both halves of the
  ** behaviour a vehicle has to keep: the real engine fields (m_iHealth,
  ** m_iClassname, ...) and the per-entity fallback table the Vehicle extras above
  ** are written into.  Reimplementing it here would be a second copy to keep in
  ** step. */
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

static int Vehicle___eq (lua_State *L) {
  /* lua_toentity, not lua_tovehicle: the two operands may be a vehicle and a
  ** plain entity userdata for the same handle, and those are equal. */
  lua_pushboolean(L, lua_toentity(L, 1) == lua_toentity(L, 2));
  return 1;
}

static int Vehicle___tostring (lua_State *L) {
  lua_Vehicle *pVehicle = lua_tovehicle(L, 1);
  if (pVehicle == NULL)
    lua_pushstring(L, "NULL");
  else
    lua_pushfstring(L, "Vehicle: %d %s", pVehicle->entindex(), pVehicle->GetClassname());
  return 1;
}


static const luaL_Reg Vehiclemeta[] = {
  {"BoostTimeLeft", Vehicle_BoostTimeLeft},
  {"CheckExitPoint", Vehicle_CheckExitPoint},
  {"EnableEngine", Vehicle_EnableEngine},
  {"GetAmmo", Vehicle_GetAmmo},
  {"GetCameraDistance", Vehicle_GetCameraDistance},
  {"GetDriver", Vehicle_GetDriver},
  {"GetHLSpeed", Vehicle_GetHLSpeed},
  {"GetMaxSpeed", Vehicle_GetMaxSpeed},
  {"GetOperatingParams", Vehicle_GetOperatingParams},
  {"GetPassenger", Vehicle_GetPassenger},
  {"GetPassengerSeatPoint", Vehicle_GetPassengerSeatPoint},
  {"GetRPM", Vehicle_GetRPM},
  {"GetSpeed", Vehicle_GetSpeed},
  {"GetSteering", Vehicle_GetSteering},
  {"GetSteeringDegrees", Vehicle_GetSteeringDegrees},
  {"GetThirdPersonMode", Vehicle_GetThirdPersonMode},
  {"GetThrottle", Vehicle_GetThrottle},
  {"GetVehicleClass", Vehicle_GetVehicleClass},
  {"GetVehicleParams", Vehicle_GetVehicleParams},
  {"GetVehicleViewPosition", Vehicle_GetVehicleViewPosition},
  {"GetWheel", Vehicle_GetWheel},
  {"GetWheelBaseHeight", Vehicle_GetWheelBaseHeight},
  {"GetWheelContactPoint", Vehicle_GetWheelContactPoint},
  {"GetWheelCount", Vehicle_GetWheelCount},
  {"GetWheelTotalHeight", Vehicle_GetWheelTotalHeight},
  {"HasBoost", Vehicle_HasBoost},
  {"HasBrakePedal", Vehicle_HasBrakePedal},
  {"IsBoosting", Vehicle_IsBoosting},
  {"IsEngineEnabled", Vehicle_IsEngineEnabled},
  {"IsEngineStarted", Vehicle_IsEngineStarted},
  {"IsValidVehicle", Vehicle_IsValidVehicle},
  {"IsVehicleBodyInWater", Vehicle_IsVehicleBodyInWater},
  {"ReleaseHandbrake", Vehicle_ReleaseHandbrake},
  {"SetBoost", Vehicle_SetBoost},
  {"SetCameraDistance", Vehicle_SetCameraDistance},
  {"SetHandbrake", Vehicle_SetHandbrake},
  {"SetHasBrakePedal", Vehicle_SetHasBrakePedal},
  {"SetMaxReverseThrottle", Vehicle_SetMaxReverseThrottle},
  {"SetMaxThrottle", Vehicle_SetMaxThrottle},
  {"SetSpringLength", Vehicle_SetSpringLength},
  {"SetSteering", Vehicle_SetSteering},
  {"SetSteeringDegrees", Vehicle_SetSteeringDegrees},
  {"SetThirdPersonMode", Vehicle_SetThirdPersonMode},
  {"SetThrottle", Vehicle_SetThrottle},
  {"SetVehicleClass", Vehicle_SetVehicleClass},
  {"SetVehicleEntryAnim", Vehicle_SetVehicleEntryAnim},
  {"SetVehicleParams", Vehicle_SetVehicleParams},
  {"SetWheelFriction", Vehicle_SetWheelFriction},
  {"StartEngine", Vehicle_StartEngine},
  {"__index", Vehicle___index},
  {"__newindex", Vehicle___newindex},
  {"__eq", Vehicle___eq},
  {"__tostring", Vehicle___tostring},
  {NULL, NULL}
};


/*
** Open Vehicle object
*/
LUALIB_API int luaopen_Vehicle_shared (lua_State *L) {
  luaL_newmetatable(L, "Vehicle");
  luaL_register(L, NULL, Vehiclemeta);
  lua_pushstring(L, "entity");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "entity" */
  return 1;
}
