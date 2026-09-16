//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB's Lua bindings for Garry's Mod's Vehicle library.
//
// This is the HL2SB twin of game/shared/lua/lhl2mp_player_shared.h: a per-class
// push function plus a metatable registered under the class's GMod name, so that
// a Lua object for a prop_vehicle_* entity carries the Vehicle methods
// (https://wiki.facepunch.com/gmod/Vehicle).
//
// The two realms do NOT have the same vehicle class:
//   * server: CPropVehicleDriveable (game/server/vehicle_base.h) -- owns the real
//             CFourWheelVehiclePhysics, the vehicle script data, the passengers
//             and the entry/exit points.  Almost every method is backed by it.
//   * client: C_PropVehicleDriveable (game/client/c_prop_vehicle.h) -- a
//             C_BaseAnimating that only receives a handful of networked fields
//             (speed, RPM, throttle, boost).  Wheels, params, seat points and
//             engine state are not networked, so those methods answer the
//             documented fallback instead of raising.  See lvehicle_shared.cpp.
//
//=============================================================================//

#ifndef LVEHICLE_SHARED_H
#define LVEHICLE_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#ifdef CLIENT_DLL
	#include "c_prop_vehicle.h"
#else
	#include "vehicle_base.h"
#endif

/* type for Vehicle functions */
#ifdef CLIENT_DLL
typedef C_PropVehicleDriveable lua_Vehicle;
#else
typedef CPropVehicleDriveable lua_Vehicle;
#endif



/*
** access functions (stack -> C)
*/

LUA_API lua_Vehicle     *(lua_tovehicle) (lua_State *L, int idx);


/*
** push functions (C -> stack)
**
** lua_pushvehicle() installs the "Vehicle" metatable.  lua_pushentity() and
** CBaseEntity::PushLuaInstanceSafe() reach it through lua_pushvehicleentity(),
** which is declared in lbaseentity_shared.h so those two files never have to
** include this vehicle header.
*/
LUA_API void  (lua_pushvehicle) (lua_State *L, lua_Vehicle *pVehicle);



LUALIB_API lua_Vehicle *(luaL_checkvehicle) (lua_State *L, int narg);
LUALIB_API lua_Vehicle *(luaL_optvehicle) (lua_State *L, int narg,
                                                      lua_Vehicle *def);


#endif // LVEHICLE_SHARED_H
