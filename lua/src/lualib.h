/*
** $Id: lualib.h $
** Lua standard libraries
** See Copyright Notice in lua.h
*/

#ifndef lualib_h
#define lualib_h

/* HL2SB_C_LINKAGE: keep the C ABI when these headers are included from C++ */
#if defined(__cplusplus)
extern "C" {
#endif

#include "lua.h"

/* version suffix for environment variable names */
#define LUA_VERSUFFIX "_" LUA_VERSION_MAJOR "_" LUA_VERSION_MINOR

LUAMOD_API int( luaopen_base )( lua_State *L );
LUAMOD_API int( luaopen_base_minimal )( lua_State *L );

#define LUA_COLIBNAME "coroutine"
LUAMOD_API int( luaopen_coroutine )( lua_State *L );

#define LUA_TABLIBNAME "table"
LUAMOD_API int( luaopen_table )( lua_State *L );

#define LUA_IOLIBNAME "io"
LUAMOD_API int( luaopen_io )( lua_State *L );

#define LUA_OSLIBNAME "os"
LUAMOD_API int( luaopen_os )( lua_State *L );

#define LUA_STRLIBNAME "string"
LUAMOD_API int( luaopen_string )( lua_State *L );

#define LUA_UTF8LIBNAME "utf8"
LUAMOD_API int( luaopen_utf8 )( lua_State *L );

#define LUA_MATHLIBNAME "math"
LUAMOD_API int( luaopen_math )( lua_State *L );

#define LUA_DBLIBNAME "debug"
LUAMOD_API int( luaopen_debug )( lua_State *L );

#define LUA_LOADLIBNAME "package"
LUAMOD_API int( luaopen_package )( lua_State *L );

/* HL2SB: LuaBitOp, kept from the Lua 5.1 build so existing mod scripts that call
** bit.band / bit.bor keep working after the move to Lua 5.4. */
#define LUA_BITLIBNAME "bit"
LUAMOD_API int( luaopen_bit )( lua_State *L );

/* HL2SB (sbrust): GMod/LuaJIT exports luaopen_jit so require("jit") and the
** global jit table work for third-party modules (src/hl2sb_lua51.c). */
LUAMOD_API int( luaopen_jit )( lua_State *L );

/* open all previous libraries */
LUALIB_API void( luaL_openlibs )( lua_State *L );

#if defined(__cplusplus)
} /* extern "C" */
#endif
#endif
