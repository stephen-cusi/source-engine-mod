//-----------------------------------------------------------------------------
// HL2SB (sbrust): C++-linkage half of the lua_shared glue.
//
// Two exports reproduce names the reference module only emits under C++
// decoration, so this TU must actually compile as C++ (MSVC /TP):
//
//   ?AdvancedLuaErrorReporter@@YAHPEAUlua_State@@@Z (win64 spelling)
//     GMod defined the reporter in a .cpp, so binary modules that declared
//     it in C++ import the mangled name.  Natural C++ linkage here lets the
//     compiler decorate per architecture - no linker alias parsing (the
//     /export alias route truncated at the first '@@' on win32; see
//     hl2sb_lua51_mangle.c history on the parent branch).
//
//   ?g_pFullFileSystem@@3PEAVIFileSystem@@EA (MSVC decorates class-typed
//     globals; the name only depends on the type spelling).  On gcc the
//     variable name stays undecorated, so this single definition serves
//     every platform.
//
// Linkage layout (mirrors hl2sb_lua51.c on the parent branch):
//   * the g_pFullFileSystem forward declaration sits BEFORE the extern "C"
//     block and its definition AFTER it - a declaration inside extern "C"
//     would flip MSVC to the undecorated name and clash with the C++
//     definition;
//   * the setter is extern "C" (the C TU calls it by plain name);
//   * the reporter twin is OUTSIDE extern "C" (its whole point is the
//     C++-decorated name); MSVC + Windows only, because a gcc .cpp would
//     collide with the plain twin at .so link time.
//
// LuaJIT's lua.h has no extern "C" wrapper (it is a C-only header), so the
// includes sit inside one here; the LuaJIT callers resolve plain anyway.
//
// Keep the reporter body in sync with hl2sb_glue.c.
//-----------------------------------------------------------------------------

#define LUA_LIB /* LUA_API -> dllexport on Windows (hl2sb_hooks.c pattern) */
#ifdef __cplusplus
extern "C" {
#endif
#include "lua.h"
#include "lauxlib.h"
#ifdef __cplusplus
}
#endif

/* Data-export spelling per toolchain (same rule as hl2sb_lua51.c): the
 * DECLARATION carries the attribute, the definition stays clean -- an
 * initialized extern warns on gcc, and MSVC needs dllexport on a
 * declaration for the data symbol to export at all. */
#if defined( _WIN32 )
#define HL2SB_DATAEXPORT __declspec( dllexport )
#elif defined( __GNUC__ )
#define HL2SB_DATAEXPORT __attribute__( ( visibility( "default" ) ) )
#else
#define HL2SB_DATAEXPORT
#endif

class IFileSystem;
extern HL2SB_DATAEXPORT IFileSystem *g_pFullFileSystem;

#ifdef __cplusplus
extern "C" {
#endif

/* C-linkage bridge called by hl2sb_glue.c's HL2SB_InitializeEngineInterfaces. */
void HL2SB_SetCppEngineInterfaces( void *pFileSystem, void *pCvarRef )
{
	( void )pCvarRef; /* cvar lives in the C TU; it handles its own slot. */
	if ( pFileSystem != NULL )
		g_pFullFileSystem = ( IFileSystem * )pFileSystem;
}

} /* extern "C" */

#if defined( _MSC_VER ) && defined( LUA_BUILD_AS_DLL )
/* C++-decorated twin of AdvancedLuaErrorReporter (hl2sb_glue.c keeps the
 * plain name). */
LUA_API int AdvancedLuaErrorReporter( lua_State *L )
{
	if ( lua_isstring( L, 1 ) ) /* strings AND numbers, like 5.1 lua_isstring */
		return 1;
	lua_pushfstring( L, "%s was given as Lua error!", lua_typename( L, lua_type( L, 1 ) ) );
	return 1;
}
#elif !defined( _MSC_VER )
typedef int hl2sb_glue_cpp_tu_not_empty;
#endif

/* The decorated data export itself: C++ linkage here (MSVC decorates the
 * name; gcc leaves globals plain), clean definition per the split above. */
IFileSystem *g_pFullFileSystem = NULL;
