//========== HL2SB - GMod compat ==========--
//
// Purpose: the navmesh Lua surface (`navmesh.*`, `CNavArea:*`, `Path()`).
//
//   Kept apart from luanextbot.h because it is a Lua LIBRARY rather than part of
//   the nextbot host: a script that only wants navmesh queries does not need a
//   bot at all.  Implementation and the deviations from GMod are documented in
//   lnavmesh.cpp.
//===========================================================================//

#ifndef LNAVMESH_H
#define LNAVMESH_H

#ifdef LUA_SDK

// Registers `navmesh`, the CNavArea / PathFollower metatables and the Path()
// factory into the current Lua state.  Idempotent; called when the first Lua
// nextbot is registered (see RegisterLuaNextBot).
void LuaNavMesh_Install( void );

#endif // LUA_SDK
#endif // LNAVMESH_H
