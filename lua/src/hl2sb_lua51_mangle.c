//-----------------------------------------------------------------------------
// HL2SB (2026-10-08): natural C++-linkage twin of AdvancedLuaErrorReporter.
//
// GMod's own definition lived in a .cpp, so binary modules that declared the
// reporter in C++ import it under the C++-decorated name:
//     ?AdvancedLuaErrorReporter@@YAHPEAUlua_State@@@Z   (win64)
//     ?AdvancedLuaErrorReporter@@YAHPAUlua_State@@@Z   (win32)
// The extern "C" twin in hl2sb_lua51.c answers the plain name for our own
// game DLLs; this file answers the decorated one by EXISTING under C++
// linkage - the compiler mangles it per architecture and dllexport picks it
// up, so no linker alias parsing is involved.
//
// Why not a /export alias (the previous approach): the win64 alias produced
// the full decorated name, but the win32 link truncated the entry at the
// first '@@' - the raw export table read "?AdvancedLuaErrorReporter\0" -
// and with the underscored target spelling the win32 CI leg failed outright
// with LNK2001 on the very symbol sitting in the same object.  Defining the
// name naturally sidesteps the option parser entirely.
//
// MSVC + Windows only: gcc/clang give this file C linkage (no /TP), which
// would collide with the twin's symbol at .so link time - and non-Windows
// modules import the plain name anyway.
//
// Keep the body in sync with AdvancedLuaErrorReporter in hl2sb_lua51.c.
//-----------------------------------------------------------------------------
#if defined( _MSC_VER ) && defined( LUA_BUILD_AS_DLL )

#define LUA_LIB /* LUA_API -> dllexport on Windows */
#include "lua.h"
#include "lauxlib.h"

LUA_API int AdvancedLuaErrorReporter( lua_State *L )
{
    if ( lua_isstring( L, 1 ) ) /* strings AND numbers, like 5.1 lua_isstring */
        return 1;
    lua_pushfstring( L, "%s was given as Lua error!", luaL_typename( L, 1 ) );
    return 1;
}

#else

typedef int hl2sb_lua51_mangle_tu_not_empty;

#endif
