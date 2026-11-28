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

/* HL2SB (2026-10-08): pushes the per-entity SetVar/GetVar storage table
** ("__hl2sb_vars" subtable of the instance table).  Shared with the
** per-class __index copies so Entity:SetVar( name, v ) reads back through
** self.name on every metatable ENT methods run under (GMod stores SetVar
** values on the script table itself; the nukepack timed detonation pack
** reads self.DetTime that way).  Returns false when the entity has no
** instance table yet (bCreate=false) or is not a Lua-backed entity. */
bool HL2SB_EntityPushVarsTable (lua_State *L, CBaseEntity *pEntity, bool bCreate);

/* HL2SB (2026-10-11): the SetNW / SetNW2 variable store (issue #47).
** One struct per (entindex, name) key per family -- family NW and family NW2
** are two separate CUtlDicts, mirroring GMod's two independent tables.  The
** struct and dicts live in this header because the replication transport
** (lsrcinit.cpp) reads them for sign-on resync and drops local copies when a
** server update arrives.  Transport: the existing "HL2SB_NW" user message with
** a family byte -- family NETWORKVAR is the legacy Lua-NetworkVar channel
** (byte value 0, unchanged), NW and NW2 are the SetNW and SetNW2 families. */

struct HL2SB_NWValue
{
  CUtlString s;
  int        i;
  float      f;
  bool       b;
  EHANDLE    hEnt;
  Vector     v;
  QAngle     a;
  char       tag;   // HL2SB_NWTAG_* of the last Set, 0 = never set (drives resync)
};

// Wire tags (first byte of a stored payload), shared with lsrcinit.cpp.
enum HL2SB_NWTag_t
{
  HL2SB_NWTAG_FLOAT = 'F',
  HL2SB_NWTAG_VECTOR = 'V',
  HL2SB_NWTAG_ANGLE = 'A',
  HL2SB_NWTAG_BOOL = 'B',
  HL2SB_NWTAG_INT = 'I',
  HL2SB_NWTAG_STRING = 'S',
  HL2SB_NWTAG_ENTITY = 'E',
};

enum HL2SB_NWFamily_t
{
  HL2SB_NWFAMILY_NETWORKVAR = 0,  // legacy Lua NetworkVar transport (wire compat)
  HL2SB_NWFAMILY_NW = 1,          // SetNW*/GetNW*
  HL2SB_NWFAMILY_NW2 = 2,         // SetNW2*/GetNW2*
};

extern CUtlDict<HL2SB_NWValue, unsigned short> g_HL2SB_NWVars;
extern CUtlDict<HL2SB_NWValue, unsigned short> g_HL2SB_NW2Vars;

#ifndef CLIENT_DLL
/* server -> lsrcinit.cpp: broadcast one value on the "HL2SB_NW" user message
** (family byte included).  Called by the SetNW and SetNW2 setters on change. */
void HL2SB_NWReplicateValue( int iFamily, CBaseEntity *pEntity, const char *pszName,
                             char tag, lua_State *L, int iValue );
#endif

#ifdef CLIENT_DLL
/* lsrcinit.cpp -> getters: push the replicated copy for
** (family, entindex, name); answers false when there is none. */
bool HL2SB_NWReplicatedPushFamily( lua_State *L, int iFamily, int entindex,
                                   const char *pszName );
#endif

/* lsrcinit.cpp receiver -> store: drop the realm-local copy of one key so the
** server update wins (the local write re-shadows on the next Set, mirroring
** the legacy NetworkVar receiver's drop). */
void HL2SB_NWDropLocalVar( int iFamily, int entindex, const char *pszName );

/* server: purge both family dicts for a dying entity -- the dicts are keyed by
** bare entindex, so a recycled slot would otherwise inherit the previous
** entity's values (GMod frees its NW1 slot on EntityRemoved and its NW2 table
** dies with the entity). */
void HL2SB_NWFreeEntityVars( int entindex );

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

QAngle HL2SB_GetLuaEyeAngles( CBaseEntity *pEntity );

#endif // LBASEENTITY_SHARED_H
