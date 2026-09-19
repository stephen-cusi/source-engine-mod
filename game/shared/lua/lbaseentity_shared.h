//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef LBASEENTITY_SHARED_H
#define LBASEENTITY_SHARED_H
#ifdef _WIN32
#pragma once
#endif

/* type for CBaseEntity functions */
typedef CBaseEntity lua_CBaseEntity;



/*
** access functions (stack -> C)
*/

LUA_API lua_CBaseEntity     *(lua_toentity) (lua_State *L, int idx);


/*
** push functions (C -> stack)
*/
LUA_API void  (lua_pushentity) (lua_State *L, lua_CBaseEntity *pEntity);

/* HL2SB: the shared NULL branch for every entity-class __index (Player,
** CHL2MP_Player, CBaseAnimating): pushes GMod's NULL-sentinel answer --
** boolean false for the key "IsValid", a method that returns false for
** everything else -- instead of raising "attempt to index a NULL entity"
** on every read (which killed IsValid() callers like the pickup HUD hook). */
void HL2SB_PushNullEntityIndex (lua_State *L, const char *pszField);

/*
** HL2SB: GMod gives a drivable vehicle its own "Vehicle" metatable on top of the
** per-class push functions above (game/shared/lua/lvehicle_shared.cpp).  These
** two are the entity-level hooks, and they are declared here rather than in that
** file's own header so lua_pushentity() can dispatch without pulling the whole
** prop_vehicle header tree into this translation unit.
**
**   lua_pushvehicleentity  pushes the Vehicle metatable and answers true when
**                          pEntity IS a drivable vehicle whose metatable is
**                          registered in this Lua state; otherwise it answers
**                          false and leaves the stack untouched, so the caller
**                          falls through to its own push.
**   lua_entityisvehicle    the same predicate without the stack traffic, backing
**                          Entity:IsVehicle().
*/
LUA_API bool  (lua_pushvehicleentity) (lua_State *L, CBaseEntity *pEntity);
LUA_API bool  (lua_entityisvehicle) (CBaseEntity *pEntity);


/*
** Experiment: Source pushes every entity through CBaseEntity::PushLuaInstanceSafe(),
** which takes the metatable from the object's dynamic type.  HL2SB instead has one
** push function per class (lua_pushentity / lua_pushplayer / lua_pushweapon /
** lua_pushanimating), and lua_pushentity always installs the plain CBaseEntity
** metatable -- whose __index knows only the fields CBaseEntity bindings list.  An
** entity that arrives through a generic API while it is really a player or a weapon
** therefore loses every method of its own class.
**
** This helper bridges the two: it picks the push function that matches the dynamic
** type, so ported binding files can keep calling PushLuaInstanceSafe() and still get
** a userdata with the full method set.  If the metatable for the specialised class
** has not been registered (or the object is a plain entity) it falls back to
** lua_pushentity(), which is also what NULL needs.
*/
LUA_API void  (PushLuaInstanceSafe) (lua_State *L, CBaseEntity *pEntity);



LUALIB_API lua_CBaseEntity *(luaL_checkentity) (lua_State *L, int narg);
LUALIB_API lua_CBaseEntity *(luaL_optentity) (lua_State *L, int narg,
                                                            lua_CBaseEntity *def);


#endif // LBASEENTITY_SHARED_H
