//-----------------------------------------------------------------------------
// HL2SB (sbrust): GMod's third-party binary module contract.
//
// GMod loads binary modules from <game>/lua/bin/ named
//     gm{sv,cl}_{name}_{platform}.dll|.so     (e.g. gmsv_foo_win64.dll)
// and calls their exported  int gmod13_open(lua_State*)  -- and
// gmod13_close(lua_State*) at state teardown.  The module resolves the Lua
// C API itself from the already-loaded lua_shared library (the reason Lua
// builds as a shlib in this branch).
//
// This file implements the engine half as GMod has it inside lua_shared:
//   HL2SB_InstallBinaryModuleSearcher(L, gamedir)  - installs a
//       package.searchers entry that maps require("name") to the module file
//       and invokes gmod13_open (Lua-file searchers run first, so a Lua
//       module with the same name still wins).
//   HL2SB_RunBinaryModuleCloses(L)                 - calls every loaded
//       module's gmod13_close once, right before the engine tears the state
//       down (luamanager's two lua_close sites).
//
// Lives in the lua_shared target on purpose: it is plain C with no engine
// dependencies, so a standalone host (see sbrust/host-test) can exercise the
// exact same code path the game runs.
//
// Naming/platform table (fork convention -- GMod has no android column):
//   win64/win32, linux64/linux, android64/android32, osx64/osx
// chosen at compile time: a binary only ever runs on the ABI it was built
// for, so the suffix is a property of this DLL, not of the runtime.
//
// NOTE: keep this file pure ASCII -- MSVC reads it as code page 936 and a
// UTF-8 byte sequence ending in 0x5C would line-splice the next directive.
//-----------------------------------------------------------------------------

#define LUA_LIB /* LUA_API -> dllexport on Windows (same pattern as bit.c) */
#include "lua.h"
#include "lauxlib.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/* Platform suffix: compile-time constant (sbrust plan). */
static const char *HL2SB_BinModSuffix( void )
{
#if defined( _WIN64 )
	return "win64";
#elif defined( _WIN32 )
	return "win32";
#elif defined( __ANDROID__ )
	return "android64";
#elif defined( __aarch64__ ) || defined( __x86_64__ )
	return "linux64";
#elif defined( __APPLE__ )
	return "osx64";
#else
	return "linux"; /* 32-bit POSIX (android armv7 / linux x86) */
#endif
}

static const char *HL2SB_BinModExt( void )
{
#ifdef _WIN32
	return ".dll";
#else
	return ".so";
#endif
}

/* GMod's realm prefix formula (util.lua:407): CLIENT and not MENU_DLL -> cl,
 * otherwise sv.  Note the menu realm uses the sv prefix on purpose (GMod
 * quirk), and on Android system.IsLinux() is true so the suffix table in
 * util.lua carries a separate android column. */
static const char *HL2SB_BinModPrefix( lua_State *L )
{
	int bClient = 0;
	int bMenu = 0;

	lua_getglobal( L, "CLIENT" );
	bClient = lua_toboolean( L, -1 );
	lua_pop( L, 1 );

	lua_getglobal( L, "MENU_DLL" );
	bMenu = lua_toboolean( L, -1 );
	lua_pop( L, 1 );

	if ( bClient && !bMenu )
		return "cl";
	return "sv";
}

/*---------------------------------------------------------------------------
 * Module loader: require calls this (module name sits on the stack as an
 * argument; gmod13_open ignores it per contract).  gmod13_open's return
 * value is the number of results it pushed -- pass straight through so
 * require caches whatever the module returned.
 *---------------------------------------------------------------------------*/
static int HL2SB_BinModLoader( lua_State *L )
{
	lua_CFunction pfnOpen = ( lua_CFunction )lua_touserdata( L, lua_upvalueindex( 1 ) );
	return pfnOpen( L );
}

/* Protected wrapper for gmod13_close: an error inside close must not
 * longjmp out of the shutdown path. */
static int HL2SB_BinModCloser( lua_State *L )
{
	lua_CFunction pfnClose = ( lua_CFunction )lua_touserdata( L, lua_upvalueindex( 1 ) );
	pfnClose( L );
	return 0;
}

#define HL2SB_BINMOD_CLOSES_KEY "HL2SB_BINMOD_CLOSES"

/* Register the close function in a registry table (key = full path); the
 * engine walks and calls them right before lua_close. */
static void HL2SB_BinModRegisterClose( lua_State *L, const char *pszPath, lua_CFunction pfnClose )
{
	if ( pfnClose == NULL )
		return;

	lua_getfield( L, LUA_REGISTRYINDEX, HL2SB_BINMOD_CLOSES_KEY );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_newtable( L );
		lua_pushvalue( L, -1 );
		lua_setfield( L, LUA_REGISTRYINDEX, HL2SB_BINMOD_CLOSES_KEY );
	}

	lua_pushstring( L, pszPath );
	lua_pushlightuserdata( L, (void *)pfnClose );
	lua_pushcclosure( L, HL2SB_BinModCloser, 1 );
	lua_settable( L, -3 );

	lua_pop( L, 1 );
}

/* The searcher: returns the loader function, or an error note (require
 * aggregates searcher errors into "module 'x' not found").  Error strings
 * are verbatim from lua_shared.dll's string table. */
static int HL2SB_BinaryModuleSearcher( lua_State *L )
{
	const char *pszName = lua_tolstring( L, 1, NULL );
	const char *pszGameDir;
	const char *pszPrefix;
	char szPath[ 1024 ];

	if ( pszName == NULL || pszName[ 0 ] == '\0' )
	{
		lua_pushliteral( L, "\n\tModule not found!" );
		return 1;
	}

	/* Path safety: no directory separators or upward traversal in the name. */
	if ( strstr( pszName, ".." ) || strchr( pszName, '/' ) || strchr( pszName, '\\' ) )
	{
		lua_pushfstring( L, "\n\tModule not found! (invalid name '%s')", pszName );
		return 1;
	}

	pszGameDir = lua_tolstring( L, lua_upvalueindex( 1 ), NULL );
	pszPrefix = HL2SB_BinModPrefix( L );

	snprintf( szPath, sizeof( szPath ), "%s/lua/bin/gm%s_%s_%s%s",
		pszGameDir ? pszGameDir : "", pszPrefix, pszName, HL2SB_BinModSuffix(), HL2SB_BinModExt() );

#ifdef _WIN32
	{
		HMODULE hLib;
		UINT uOldErrorMode = SetErrorMode( SEM_FAILCRITICALERRORS );
		hLib = LoadLibraryA( szPath );
		SetErrorMode( uOldErrorMode );
		if ( hLib == NULL )
		{
			lua_pushfstring( L, "\n\tCouldn't load module library! (%s)", szPath );
			return 1;
		}
		else
		{
			lua_CFunction pfnOpen = ( lua_CFunction )GetProcAddress( hLib, "gmod13_open" );
			lua_CFunction pfnClose = ( lua_CFunction )GetProcAddress( hLib, "gmod13_close" );
			if ( pfnOpen == NULL )
			{
				lua_pushfstring( L, "\n\tCouldn't find function in library! (%s)", szPath );
				return 1;
			}
			HL2SB_BinModRegisterClose( L, szPath, pfnClose );
			lua_pushlightuserdata( L, (void *)pfnOpen );
			lua_pushcclosure( L, HL2SB_BinModLoader, 1 );
		}
	}
#else
	{
		void *hLib = dlopen( szPath, RTLD_NOW );
		if ( hLib == NULL )
		{
			const char *pszDl = dlerror();
			lua_pushfstring( L, "\n\tCouldn't load module library! (%s) [%s]",
				szPath, pszDl ? pszDl : "unknown dlopen failure" );
			return 1;
		}
		else
		{
			lua_CFunction pfnOpen = ( lua_CFunction )dlsym( hLib, "gmod13_open" );
			lua_CFunction pfnClose = ( lua_CFunction )dlsym( hLib, "gmod13_close" );
			if ( pfnOpen == NULL )
			{
				lua_pushfstring( L, "\n\tCouldn't find function in library! (%s)", szPath );
				return 1;
			}
			HL2SB_BinModRegisterClose( L, szPath, pfnClose );
			lua_pushlightuserdata( L, (void *)pfnOpen );
			lua_pushcclosure( L, HL2SB_BinModLoader, 1 );
		}
	}
#endif

	return 1;
}

/* Engine call site: tail of luasrc_setmodulepaths (shared by the menu,
 * server and client states).  Absolute game dir -- Android's cwd is "/",
 * so a relative loadlib path could never work; absolute from day one.
 *
 * extern "C" guard on the exported entry points: this tree may be compiled
 * as C++ (lapi.h's HL2SB_LuaApiStackOverflow exports decorated for the same
 * reason), and the undecorated names are what modules bind to. */
#ifdef __cplusplus
extern "C" {
#endif

LUA_API void HL2SB_InstallBinaryModuleSearcher( lua_State *L, const char *pszGameDir )
{
	if ( L == NULL )
		return;

	/* upvalue 1 = game dir */
	lua_pushstring( L, pszGameDir ? pszGameDir : "" );
	lua_pushcclosure( L, HL2SB_BinaryModuleSearcher, 1 );	/* [fn] */

	/* table.insert( package.searchers, 3, fn ) -- after preload(1) and the
	 * Lua-file searcher(2), before the legacy cpath searcher: GMod tries
	 * Lua modules first, binary second. */
	lua_getglobal( L, "table" );				/* [fn table] */
	lua_getfield( L, -1, "insert" );			/* [fn table insert] */
	lua_getglobal( L, "package" );				/* [fn table insert package] */
	lua_getfield( L, -1, "searchers" );			/* [fn table insert package searchers] */
	lua_remove( L, -2 );					/* [fn table insert searchers] */
	lua_pushinteger( L, 3 );				/* [fn table insert searchers 3] */
	lua_pushvalue( L, -5 );					/* [fn table insert searchers 3 fn] */
	lua_call( L, 3, 0 );					/* [fn table] */
	lua_pop( L, 2 );

	/* close registry */
	lua_newtable( L );
	lua_setfield( L, LUA_REGISTRYINDEX, HL2SB_BINMOD_CLOSES_KEY );
}

/* Engine call site: before each lua_close.  Calls every loaded module's
 * gmod13_close once, swallows errors (no longjmp out of shutdown), then
 * clears the registry so a double run is a no-op. */
LUA_API void HL2SB_RunBinaryModuleCloses( lua_State *L )
{
	if ( L == NULL )
		return;

	lua_getfield( L, LUA_REGISTRYINDEX, HL2SB_BINMOD_CLOSES_KEY );
	if ( lua_istable( L, -1 ) )
	{
		lua_pushnil( L );
		while ( lua_next( L, -2 ) != 0 )
		{
			/* stack: ... table key value */
			if ( lua_isfunction( L, -1 ) )
				lua_pcall( L, 0, 0, 0 );	/* pcall consumes the closure; key stays on top */
			else
				lua_pop( L, 1 );		/* pop the non-function value; key stays on top */
		}
	}
	lua_pop( L, 1 );

	lua_pushnil( L );
	lua_setfield( L, LUA_REGISTRYINDEX, HL2SB_BINMOD_CLOSES_KEY );
}

#ifdef __cplusplus
} /* extern "C" */
#endif
