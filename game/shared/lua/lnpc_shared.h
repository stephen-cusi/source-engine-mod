//========== HL2SB - GMod compat ==========--
//
// Purpose: HL2SB's Lua bindings for Garry's Mod's NPC and NextBot metatables.
//
//   GMod registers one metatable per entity class family and reports the class
//   name through it: an NPC userdata answers type() == "NPC", carries
//   MetaName / MetaID (9, TYPE_ENTITY) / MetaBaseClass (the Entity metatable)
//   and FindMetaTable( "NPC" ) resolves it.  This fork used to push every NPC
//   and nextbot through the shared "CBaseAnimating" metatable instead, so
//   scripts could not tell them apart by type and the NPC/NextBot method
//   families resolved to nil.
//
//   Realm split, mirroring the Vehicle library's arrangement:
//     * server: CAI_BaseNPC owns the real schedules, conditions, capabilities,
//               enemy memory, motor and speech APIs -- and EVERY method the
//               Garry's Mod wiki lists for NPC (182 entries) is present, as a
//               real implementation, a documented approximation, or a
//               documented no-op for the subsystems this fork lacks (see the
//               policy block in lnpc_shared.cpp).  The NextBot methods run on
//               INextBot, which only exists server-side.
//     * client: GMod's own client NPC table carries exactly three methods
//               (IsNPC / GetActiveWeapon / __tostring) because every AI
//               method is server state there too; the client metatable below
//               matches that exactly rather than inventing defaults.
//
//   The metatable chain mirrors the GMod arrangement that
//   PrintTable( getmetatable( npc ) ) shows: MetaBaseClass points at the
//   Entity metatable, and method lookups that the NPC table does not answer
//   fall back through CBaseAnimating to CBaseEntity (see NPC___index below).
//
//=============================================================================//

#ifndef LNPC_SHARED_H
#define LNPC_SHARED_H

#ifdef _WIN32
#pragma once
#endif

/*
** push functions (C -> stack)
**
** lua_pushnpc() / lua_pushnextbot() install the "NPC" / "NextBot" metatable.
** lua_pushentity() and CBaseEntity::PushLuaInstanceSafe() reach them through
** lua_pushnpcentity() / lua_pushnextbotentity(), which are declared as LOCAL
** prototypes at their call sites in lbaseentity_shared.cpp -- declared there
** rather than in a shared header so no header touch forces a whole-tree
** rebuild (the luaopen_CSEmitter precedent in lsrcinit.cpp).
*/
LUA_API void (lua_pushnpc) (lua_State *L, CBaseEntity *pEntity);
LUA_API void (lua_pushnextbot) (lua_State *L, CBaseEntity *pEntity);

/*
** Open the metatables.  Registered from lsrcinit.cpp's library list under the
** names "NPC" and "NextBot", which is also how FindMetaTable resolves them
** through the alias table.
*/
LUALIB_API int (luaopen_NPC_shared) (lua_State *L);
LUALIB_API int (luaopen_NextBot_shared) (lua_State *L);

#endif  // LNPC_SHARED_H
