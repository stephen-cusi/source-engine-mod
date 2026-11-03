// lua.hpp
// Lua header files for C++
// <<extern "C">> not supplied automatically because Lua also compiles as C++

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

/*
 * Compatibility with Lua 5.1
 */

#ifndef LUA_COMPAT_LOADED_HPP
#define LUA_COMPAT_LOADED_HPP
#ifdef _WIN32
#pragma once
#endif

#define LUA_ENAME   "_E"
#define LUA_RNAME   "_R"

#define LUA_QL(x)	"'" x "'"
#define LUA_QS		LUA_QL("%s")

/*
 * HL2SB: these four are Lua 5.1 spellings, and 5.1 TRUNCATED the argument.
 *
 * lua_tointeger() in 5.1 was `(int)lua_tonumber()`, so luaL_checkint( "2.7" )
 * yielded 2.  Lua 5.4's lua_tointegerx() refuses any float that has a
 * fractional part and raises
 *     bad argument #N to '<fn>' (number has no integer representation)
 *
 * Forwarding the 5.1 names to the strict 5.4 luaL_checkinteger() therefore
 * broke every 5.1-era binding that is handed a *computed* coordinate -- which
 * is the normal case for the surface drawing API here, because scripts do
 *     surface.DrawFilledRect( x, y, x + w, y + h )
 * with an eased/spring float x.  Every such call died inside the Paint hook,
 * so whole HUDs drew nothing at all (undo notification, pickup strip, ...).
 *
 * Route them through the number checks, which accept and truncate floats
 * exactly like 5.1 did.  luaL_checkinteger() itself is left strict for the
 * bindings that genuinely want an integer.
 */
#if LUA_VERSION_NUM >= 502
#define luaL_checkint(L, n) ((int)luaL_checknumber(L, (n)))
#define luaL_optint(L, n, d) ((int)luaL_optnumber(L, (n), (d)))
#define luaL_checklong(L, n) ((long)luaL_checknumber(L, (n)))
#define luaL_optlong(L, n, d) ((long)luaL_optnumber(L, (n), (d)))

/* HL2SB: Lua 5.1 spelling of lua_rawlen (removed in 5.2). */
#define lua_objlen(L,i) lua_rawlen(L,(i))
#define lua_strlen(L,i) ((int)lua_rawlen(L,(i)))
#else
#define LUA_LOADED_TABLE "_LOADED"
#define LUA_OPEQ 0
#undef luaL_getmetatable
#define luaL_getmetatable(L,n) (lua_getfield(L, LUA_REGISTRYINDEX, (n)), lua_type(L, -1))
#define lua_rawlen(L,i) lua_objlen(L,(i))
#define lua_pushglobaltable(L) lua_pushvalue(L,LUA_GLOBALSINDEX)
int lua_absindex(lua_State *L, int idx);
int lua_geti(lua_State *L, int idx, lua_Integer n);
int lua_compare(lua_State *L, int a, int b, int op);
int lua_isinteger(lua_State *L, int idx);
int luaL_getsubtable(lua_State *L, int idx, const char *name);
lua_Integer luaL_len(lua_State *L, int idx);
const char *luaL_tolstring(lua_State *L, int idx, size_t *len);
#endif

#if LUA_VERSION_NUM >= 502
lua_State *lua_open();

void luaL_register( lua_State *L, const char *libname, const luaL_Reg *l );

int luaL_typerror(lua_State *L, int narg, const char *tname);

void *luaL_checkudata(lua_State *L, int ud, const char *tname);

const char *luaL_optlstring(lua_State *L, int numArg, const char *def, size_t *l);
#endif

void lua_getref(lua_State *L, int ref);

void lua_unref(lua_State *L, int ref);

bool lua_isrefvalid(lua_State *L, int ref);

#endif // LUA_COMPAT_LOADED_HPP
