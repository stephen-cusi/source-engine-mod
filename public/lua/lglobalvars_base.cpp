//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#define lglobalvars_base_cpp

#include "cbase.h"
#include "lua.hpp"
#include "luasrclib.h"
#include "hl2sb_framestats_cat.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"



static int gpGlobals_absoluteframetime (lua_State *L) {
  lua_pushnumber(L, gpGlobals->absoluteframetime);
  return 1;
}

static int gpGlobals_curtime (lua_State *L) {
  HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_CURTIME );
  lua_pushnumber(L, gpGlobals->curtime);
  return 1;
}

static int gpGlobals_framecount (lua_State *L) {
  lua_pushinteger(L, gpGlobals->framecount);
  return 1;
}

static int gpGlobals_frametime (lua_State *L) {
  lua_pushnumber(L, gpGlobals->frametime);
  return 1;
}

static int gpGlobals_interval_per_tick (lua_State *L) {
  lua_pushnumber(L, gpGlobals->interval_per_tick);
  return 1;
}

static int gpGlobals_IsClient (lua_State *L) {
  lua_pushboolean(L, gpGlobals->IsClient());
  return 1;
}

static int gpGlobals_maxClients (lua_State *L) {
  lua_pushinteger(L, gpGlobals->maxClients);
  return 1;
}

static int gpGlobals_network_protocol (lua_State *L) {
  lua_pushinteger(L, gpGlobals->network_protocol);
  return 1;
}

static int gpGlobals_realtime (lua_State *L) {
  lua_pushnumber(L, gpGlobals->realtime);
  return 1;
}

static int gpGlobals_simTicksThisFrame (lua_State *L) {
  lua_pushinteger(L, gpGlobals->simTicksThisFrame);
  return 1;
}

static int gpGlobals_tickcount (lua_State *L) {
  lua_pushinteger(L, gpGlobals->tickcount);
  return 1;
}


// HL2SB: GMod's SHARED-realm engine global FrameTime().
//
// GMod's FrameTime() returns "the CurTime-based time in seconds it took to
// render the last frame" - which is literally gpGlobals->frametime.  The wiki's
// own example proves it is tick based, not render based:  print(1/FrameTime())
// yields 66.666668156783, i.e. the default tick rate, not the client's fps.
// https://wiki.facepunch.com/gmod/Global.FrameTime
//
// It has to exist on the SERVER VM as well: sandbox's weapons/gmod_camera/
// shared.lua calls it from SWEP:Tick (zoom/roll on mouse 2), and this fork
// dispatches scripted-weapon Tick on both realms - GMod's shared code assumes
// the shared global, so the server side filled ds_debug.log with
//   lua/weapons/gmod_camera/shared.lua:121: attempt to call a nil value
//   (global 'FrameTime')
// once per tick while the camera was held.  The client had never noticed
// because lua/includes/extensions/gmod_globals.lua:206 shims it there - and
// that file's server branch returns at :194 before the shim, which is why only
// the server VM errored.  gmod_globals uses `FrameTime = FrameTime or ...`, so
// this engine version simply wins and the shim becomes the fallback.
//
// gpGlobals can legitimately be NULL when a Lua state is created before any
// level is loaded (menu / lua_run on an empty host), hence the guard.
static int luasrc_FrameTime (lua_State *L) {
  lua_pushnumber(L, gpGlobals ? gpGlobals->frametime : 0.0);
  return 1;
}


static const luaL_Reg gpGlobals_funcs[] = {
  {"FrameTime", luasrc_FrameTime},
  {NULL, NULL}
};

static const luaL_Reg gpGlobalslib[] = {
  {"absoluteframetime",   gpGlobals_absoluteframetime},
  {"curtime",  gpGlobals_curtime},
  {"framecount",  gpGlobals_framecount},
  {"frametime", gpGlobals_frametime},
  {"interval_per_tick",  gpGlobals_interval_per_tick},
  {"IsClient",  gpGlobals_IsClient},
  {"maxClients",  gpGlobals_maxClients},
  {"network_protocol",   gpGlobals_network_protocol},
  {"realtime",   gpGlobals_realtime},
  {"simTicksThisFrame", gpGlobals_simTicksThisFrame},
  {"tickcount",   gpGlobals_tickcount},
  {NULL, NULL}
};


/*
** Open gpGlobals library
*/
LUALIB_API int luaopen_gpGlobals (lua_State *L) {
  luaL_register(L, LUA_GLOBALSLIBNAME, gpGlobalslib);
  // HL2SB: the GMod-style engine globals that belong to this state but are NOT
  // members of the gpGlobals table (see luasrc_FrameTime above).  Same pattern
  // luaopen_ConVar uses for GetConVar_Internal (public/lua/tier1/lconvar.cpp).
  luaL_register(L, "_G", gpGlobals_funcs);
  return 1;
}

