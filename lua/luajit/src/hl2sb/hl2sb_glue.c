//-----------------------------------------------------------------------------
// HL2SB (sbrust): GMod lua_shared glue, LuaJIT-core edition.
//
// The LuaJIT core covers the whole 5.1 C-API export set natively, so this
// file keeps only the glue GMod's lua_shared carries on top of the VM:
//
//   CreateInterface                - Valve-style factory; the reference build
//                                    has no registered interfaces in this
//                                    module, so every lookup misses.
//   cvar                           - data export modules link directly; the
//                                    game DLLs push the real ICvar pointer in
//                                    through HL2SB_InitializeEngineInterfaces.
//   AdvancedLuaErrorReporter       - message handler: printable errors pass
//                                    through, everything else becomes
//                                    "<type> was given as Lua error!".
//   HL2SB_InitializeEngineInterfaces - the game side hands in the filesystem
//                                    and cvar pointers once per realm.
//
// g_pFullFileSystem (C++-decorated data export) and the C++-decorated twin
// of AdvancedLuaErrorReporter live in hl2sb_glue_cpp.cpp, the one TU that
// must compile under C++ linkage.  The setter below is the C-linkage bridge
// between the two.
//
// Linkage contract:
//   * LUA_LIB before lua.h -> LUA_API is dllexport on Windows, default
//     visibility on gcc (the ELF leg patches luaconf for LUA_BUILD_AS_SHARED).
//   * This TU is compiled as C (the waf taskgen strips the global /TP);
//     everything here is extern "C" anyway.
//
// Keep this file pure ASCII.
//-----------------------------------------------------------------------------

#define LUA_LIB /* LUA_API -> dllexport on Windows (hl2sb_hooks.c pattern) */
#include "lua.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* C-linkage bridge into hl2sb_glue_cpp.cpp, which owns the decorated
 * g_pFullFileSystem data export. */
void HL2SB_SetCppEngineInterfaces( void *pFileSystem, void *pCvarRef );

/* Valve-style interface factory.  GMod's lua_shared keeps a small registry
 * of interfaces registered from inside the DLL; ours has none, so every
 * lookup misses -- *pReturn = 1 (interface failed), NULL, which is exactly
 * what the reference wrapper does on a miss. */
LUA_API void *CreateInterface( const char *pName, int *pReturn )
{
	( void )pName;
	if ( pReturn != NULL )
		*pReturn = 1;
	return NULL;
}

/* GMod exports the engine cvar pointer as plain-C data from lua_shared
 * (modules link it directly).  Declaration carries the export attribute,
 * definition stays clean ("extern void *cvar = NULL" warns on gcc). */
#if defined( _WIN32 )
#define HL2SB_DATAEXPORT __declspec( dllexport )
#elif defined( __GNUC__ )
#define HL2SB_DATAEXPORT __attribute__( ( visibility( "default" ) ) )
#else
#define HL2SB_DATAEXPORT
#endif

HL2SB_DATAEXPORT extern void *cvar;
void *cvar = NULL;

/* Engine call site: tail of luasrc_setmodulepaths, after both
 * g_pFullFileSystem and g_pCVar are up.  Until then both slots read NULL,
 * which is also what the reference BSS holds before its init runs. */
LUA_API void HL2SB_InitializeEngineInterfaces( void *pFileSystem, void *pCvar )
{
	HL2SB_SetCppEngineInterfaces( pFileSystem, pCvar );
	if ( pCvar != NULL )
		cvar = pCvar;
}

/* Message handler twin of the C++-decorated one in hl2sb_glue_cpp.cpp.
 * Exported with C linkage (plain name): the game DLLs import it plain, and
 * keeping both spellings is what the reference layout does (its reporter is
 * defined in a .cpp, ours keeps the plain name here so every platform and
 * toolchain sees the same import shape). */
LUA_API int AdvancedLuaErrorReporter( lua_State *L )
{
	if ( lua_isstring( L, 1 ) ) /* strings AND numbers, like 5.1 lua_isstring */
		return 1;
	lua_pushfstring( L, "%s was given as Lua error!", lua_typename( L, lua_type( L, 1 ) ) );
	return 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
