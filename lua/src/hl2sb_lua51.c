//-----------------------------------------------------------------------------
// HL2SB (sbrust): GMod (LuaJIT 2.1 / Lua 5.1) C-API export shim.
//
// GMod's lua_shared.dll exports 151 symbols.  37 of them do not exist in a
// stock Lua 5.4 build (lua_pcall, lua_getfenv, luaL_register, ... plus the
// GMod-specific glue: GMOD_LoadBinaryModule, AdvancedLuaErrorReporter,
// CreateInterface, the cvar / g_pFullFileSystem data pointers and the
// LuaJIT marker symbols).  Third-party GMod binary modules
// (lua/bin/gm{sv,cl}_*_{platform}.dll) are linked against that export set,
// and on Windows the dynamic linker resolves every import at LoadLibrary
// time -- a single missing name fails the WHOLE module load before
// gmod13_open ever runs.
//
// This file provides the 37 names with 5.1 semantics on top of our 5.4.6
// host.  Signatures and behaviour were taken from the GMod win64
// lua_shared.dll itself (capstone analysis of every wrapper, e.g.
// lua_setfenv pops the environment table unconditionally and returns 0/1,
// lua_cpcall discards all results, lua_tointeger truncates floats with
// cvttsd2si) and from the Lua 5.1 manual where the binary was ambiguous.
//
// Linkage contract (see AGENTS.md "lua_shared export migration"):
//   * LUA_LIB before lua.h  -> LUA_API is dllexport on Windows, default
//     visibility on gcc (luaconf LUA_BUILD_AS_SHARED).
//   * The shims below live in extern "C" so the exported names are the
//     undecorated ones modules bind to -- this file is compiled as C++
//     (MSVC CFLAGS carry /TP), so without the block every definition would
//     come out mangled (the exact bug that made luaL_pushmodule export as
//     ?luaL_pushmodule@@... before this migration).
//   * g_pFullFileSystem is deliberately OUTSIDE extern "C": GMod exports it
//     under the C++-decorated data name ?g_pFullFileSystem@@3PEAVIFileSystem@@EA,
//     which only a C++-linked definition reproduces.
//
// Known ABI boundary: luaL_Buffer.  GMod's modules allocate the 5.1 struct
// {char *p; int lvl; lua_State *L; char buffer[512]} on their own stack
// (536 bytes total, measured from luaL_addlstring's B+0x218 end pointer).
// Our 5.4 struct is {b,size,n,L,init} = 32 byte header, so luaconf.h pins
// LUAL_BUFFERSIZE to 504: 32 + 504 = 536 -- every byte our functions touch
// stays inside the module's allocation.  Modules that poke B.p directly
// (the 5.1 luaL_addsize macro) still see the wrong field; that pattern is
// rare and cannot be bridged without a second struct layout.
//
// NOTE: keep this file pure ASCII -- MSVC reads it as code page 936 and a
// UTF-8 byte sequence ending in 0x5C would line-splice the next directive.
//-----------------------------------------------------------------------------

#define LUA_LIB /* LUA_API -> dllexport on Windows (hl2sb_hooks.c pattern) */
#include "lua.h"
#include "lauxlib.h"

#include <stddef.h>
#include <string.h>

/* Data-export spelling per toolchain: MSVC uses __declspec(dllexport), the
 * gcc/clang side must carry an explicit default-visibility attribute --
 * compiler_optimizations.py compiles everything with -fvisibility=hidden,
 * and LUA_API's gcc branch spells "attr extern", which would put an
 * initialized-and-extern warning on every data definition.  So data
 * symbols get HL2SB_DATAEXPORT on the DECLARATION and a plain definition
 * afterwards (both forms inherit the export attribute). */
#if defined( _WIN32 )
#define HL2SB_DATAEXPORT __declspec( dllexport )
#elif defined( __GNUC__ )
#define HL2SB_DATAEXPORT __attribute__( ( visibility( "default" ) ) )
#else
#define HL2SB_DATAEXPORT
#endif

/* Forward declaration of the filesystem pointer with C++ linkage (mangled
 * export) and C linkage on a real C build.  Declared before the extern "C"
 * block below so the setter can reference it from inside the block. */
#ifdef __cplusplus
class IFileSystem;
extern HL2SB_DATAEXPORT IFileSystem *g_pFullFileSystem;
#else
struct IFileSystem;
extern HL2SB_DATAEXPORT struct IFileSystem *g_pFullFileSystem;
#endif

/* lua.h / luaconf.h / lauxlib.h only carry these 5.1 names as macros over
 * the 5.4 spellings (the fork's GMod-compat block even keeps lua_objlen /
 * lua_equal / lua_lessthan and lauxlib keeps luaL_loadfile /
 * luaL_loadbuffer / luaL_prepbuffer as macros).  Modules link real
 * FUNCTIONS under those names, so drop the macros here and define the
 * functions.  (Only this TU is affected; every other consumer of the
 * headers keeps the macros.) */
#undef lua_call
#undef lua_pcall
#undef lua_yield
#undef lua_tonumber
#undef lua_tointeger
#undef lua_insert
#undef lua_remove
#undef lua_replace
#undef lua_newuserdata
#undef lua_objlen
#undef lua_equal
#undef lua_lessthan
#undef luaL_loadfile
#undef luaL_loadbuffer
#undef luaL_prepbuffer

#ifdef __cplusplus
extern "C" {
#endif

/* {==== stack manipulation: 5.1 exported these as functions ====} */

LUA_API void lua_insert( lua_State *L, int idx )
{
    lua_rotate( L, idx, 1 ); /* same primitive lua.h's macro used */
}

LUA_API void lua_remove( lua_State *L, int idx )
{
    lua_rotate( L, idx, -1 );
    lua_pop( L, 1 );
}

LUA_API void lua_replace( lua_State *L, int idx )
{
    lua_copy( L, -1, idx );
    lua_pop( L, 1 );
}

/* {==== call / pcall / yield family ====} */

LUA_API void lua_call( lua_State *L, int narg, int nres )
{
    lua_callk( L, narg, nres, 0, NULL );
}

LUA_API int lua_pcall( lua_State *L, int narg, int nres, int errfunc )
{
    return lua_pcallk( L, narg, nres, errfunc, 0, NULL );
}

/* Lua 5.1 manual: func runs with a single light userdata (ud) on its stack;
 * on success returns 0 and leaves the stack CHANGED ONLY by popping
 * func+ud -- "All values returned by func are discarded".  On error the
 * standard error codes come back with the error object on top.  pcall with
 * nres == 0 does exactly that. */
LUA_API int lua_cpcall( lua_State *L, lua_CFunction func, void *ud )
{
    lua_pushcfunction( L, func );
    lua_pushlightuserdata( L, ud );
    return lua_pcall( L, 1, 0, 0 );
}

/* lua_yieldk with k == NULL: our ldo.c resume() checks ci->u.c.k for NULL
 * before invoking it (the k != NULL assert sits only in finishCcall, the
 * pcallk-interrupt path), so a direct yield resumes as "the C function
 * finished, resume arguments become its results" -- the 5.1 behaviour. */
LUA_API int lua_yield( lua_State *L, int nresults )
{
    return lua_yieldk( L, nresults, 0, NULL );
}

/* LuaJIT keeps the 5.1 two-argument resume as lua_resume_real; this fork's
 * lua_resume is (L, from, narg, nres) -- 'from' only seeds the C-call depth
 * (L->nCcalls = getCcalls(from)), so NULL starts a fresh budget exactly
 * like a standalone resume.  Status codes are identical to 5.1. */
LUA_API int lua_resume_real( lua_State *L, int narg )
{
    int nres = 0;
    return lua_resume( L, NULL, narg, &nres );
}

/* LuaJIT's lua_loadx is 5.4's lua_load with the same parameter order
 * (L, reader, data, chunkname, mode) -- verified against the GMod wrapper
 * (5 register arguments). */
LUA_API int lua_loadx( lua_State *L, lua_Reader reader, void *data,
                       const char *chunkname, const char *mode )
{
    return lua_load( L, reader, data, chunkname, mode );
}

/* {==== value coercion ====} */

LUA_API lua_Number lua_tonumber( lua_State *L, int idx )
{
    return lua_tonumberx( L, idx, NULL );
}

/* 5.1 semantics: lua_tointeger converts ANY number (floats TRUNCATE --
 * GMod's wrapper is a single cvttsd2si) and numeric strings.  5.4's
 * lua_tointegerx only accepts exact integers, so fall back to the float
 * conversion before giving up. */
LUA_API lua_Integer lua_tointeger( lua_State *L, int idx )
{
    int bIsNum = 0;
    lua_Integer ret = lua_tointegerx( L, idx, &bIsNum );
    if ( bIsNum )
        return ret;
    {
        lua_Number n = lua_tonumberx( L, idx, &bIsNum );
        if ( bIsNum )
            return ( lua_Integer )n;
    }
    return 0;
}

/* 5.1 manual: strings -> length, tables -> length operator, userdata ->
 * size of the allocated block, "Other values: it is 0".  Our lua_rawlen
 * already covers string/table/userdata (u->len) in one call. */
LUA_API size_t lua_objlen( lua_State *L, int idx )
{
    switch ( lua_type( L, idx ) )
    {
    case LUA_TSTRING:
    case LUA_TTABLE:
    case LUA_TUSERDATA:
        return ( size_t )lua_rawlen( L, idx );
    default:
        return 0;
    }
}

/* 5.1 comparison operators were functions; 5.4 folded them into
 * lua_compare with an operator argument. */
LUA_API int lua_equal( lua_State *L, int idx1, int idx2 )
{
    return lua_compare( L, idx1, idx2, LUA_OPEQ );
}

LUA_API int lua_lessthan( lua_State *L, int idx1, int idx2 )
{
    return lua_compare( L, idx1, idx2, LUA_OPLT );
}

/* 5.1 userdata has no uservalues, so the uv count is 0 -- the fenv
 * emulation keeps its table in the weak registry instead. */
LUA_API void *lua_newuserdata( lua_State *L, size_t size )
{
    return lua_newuserdatauv( L, size, 0 );
}

/* {==== per-object environments (5.1 fenv, emulated) ====
 *
 * 5.4 dropped function environments; a Lua function's environment is its
 * _ENV upvalue.  The emulation therefore:
 *   - Lua functions that HAVE an _ENV upvalue: swap the upvalue for real
 *     (subsequent global lookups inside the function use the new table --
 *     this is the sandbox pattern GMod modules use after luaL_loadbuffer).
 *   - everything else function-like (C closures, functions without _ENV,
 *     threads, userdata): a weak-keyed registry table, so getfenv/setfenv
 *     round-trip without leaking the object.
 *   - non-environment types: getfenv pushes nil, setfenv returns 0 --
 *     exactly what GMod's wrapper does (analysis of lua_getfenv /
 *     lua_setfenv: the default branch pushes nil, the unsupported branch
 *     restores the stack, returns 0 and always pops the table).
 *
 * Weak-key registry lives at registry["HL2SB_FENV51"]. */

#define HL2SB_FENV_KEY "HL2SB_FENV51"

static void hl2sb_pushfenvreg( lua_State *L )
{
    lua_getfield( L, LUA_REGISTRYINDEX, HL2SB_FENV_KEY );
    if ( lua_istable( L, -1 ) )
        return;
    lua_pop( L, 1 );
    lua_newtable( L );          /* reg */
    lua_newtable( L );          /* metatable */
    lua_pushliteral( L, "k" );
    lua_setfield( L, -2, "__mode" );
    lua_setmetatable( L, -2 );
    lua_pushvalue( L, -1 );
    lua_setfield( L, LUA_REGISTRYINDEX, HL2SB_FENV_KEY );
    lua_pop( L, 1 );            /* keep one reg on the stack */
    lua_getfield( L, LUA_REGISTRYINDEX, HL2SB_FENV_KEY );
}

/* Returns the upvalue index of _ENV (0 = none), leaving the stack alone. */
static int hl2sb_envupindex( lua_State *L, int fnidx )
{
    int n;
    for ( n = 1; ; n++ )
    {
        const char *szName = lua_getupvalue( L, fnidx, n );
        if ( szName == NULL )
            return 0;
        if ( strcmp( szName, "_ENV" ) == 0 )
        {
            lua_pop( L, 1 );
            return n;
        }
        lua_pop( L, 1 );
    }
}

LUA_API void lua_getfenv( lua_State *L, int idx )
{
    int nEnvUp;
    int nType;

    idx = lua_absindex( L, idx );
    nType = lua_type( L, idx );

    if ( nType == LUA_TFUNCTION )
    {
        nEnvUp = hl2sb_envupindex( L, idx );
        if ( nEnvUp != 0 )
        {
            lua_getupvalue( L, idx, nEnvUp ); /* pushes the live _ENV */
            return;
        }
    }

    if ( nType == LUA_TFUNCTION || nType == LUA_TTHREAD || nType == LUA_TUSERDATA )
    {
        hl2sb_pushfenvreg( L );       /* [reg] */
        lua_pushvalue( L, idx );      /* [reg obj] */
        lua_rawget( L, -2 );          /* [reg env|nil] */
        lua_remove( L, -2 );          /* [env|nil] */
        if ( !lua_isnil( L, -1 ) )
            return;
        lua_pop( L, 1 );
        lua_pushglobaltable( L );     /* 5.1 default: the globals table */
        return;
    }

    lua_pushnil( L );                 /* GMod: non-env types push nil */
}

LUA_API int lua_setfenv( lua_State *L, int idx )
{
    int nType;

    idx = lua_absindex( L, idx );
    nType = lua_type( L, idx );
    /* stack: [ ... newenv ] -- top */

    if ( nType == LUA_TFUNCTION )
    {
        int nEnvUp = hl2sb_envupindex( L, idx );
        if ( nEnvUp != 0 )
        {
            lua_getupvalue( L, idx, nEnvUp ); /* [newenv oldenv] */
            lua_pushvalue( L, -2 );           /* [newenv oldenv newenv] */
            if ( lua_setupvalue( L, idx, nEnvUp ) == NULL )
                lua_pop( L, 1 );              /* could not install: drop dup */
            lua_pop( L, 2 );                  /* oldenv + the caller's table */
            return 1;
        }
    }

    if ( nType == LUA_TFUNCTION || nType == LUA_TTHREAD || nType == LUA_TUSERDATA )
    {
        hl2sb_pushfenvreg( L );         /* [newenv reg] */
        lua_pushvalue( L, idx );        /* [newenv reg obj] */
        lua_pushvalue( L, -2 );         /* [newenv reg obj newenv] */
        lua_rawset( L, -3 );            /* reg[obj] = newenv */
        lua_pop( L, 2 );                /* reg + the caller's table */
        return 1;
    }

    lua_pop( L, 1 );                    /* always pop, like GMod's wrapper */
    return 0;
}

/* {==== Lua 5.1 lauxlib exports ====} */

LUALIB_API int luaL_typerror( lua_State *L, int narg, const char *tname )
{
    const char *msg = lua_pushfstring( L, "%s expected, got %s",
                                       tname, luaL_typename( L, narg ) );
    return luaL_argerror( L, narg, msg );
}

/* 5.1/LuaJIT flush any partial content and hand back the write cursor.
 * Our 5.4 prepbuffsize with the full capacity reproduces that: an empty
 * buffer returns its start pointer, a non-empty one pushes the partial
 * content aside (via the box) and returns fresh space. */
LUALIB_API char *luaL_prepbuffer( luaL_Buffer *B )
{
    return luaL_prepbuffsize( B, LUAL_BUFFERSIZE );
}

static int hl2sb_countfuncs( const luaL_Reg *l )
{
    int n = 0;
    if ( l )
        for ( ; l->name != NULL; l++ )
            n++;
    return n;
}

/* GMod's luaL_openlib, transcribed from the win64 analysis:
 *   pushmodule(libname, nfuncs) -> insert below the nup upvalues the CALLER
 *   already pushed (~(nup) == -(nup+1)) -> either register the functions
 *   with copies of those upvalues, or settop(-(nup+1)) when there is no
 *   function table.  With functions the library table stays on the stack. */
static void hl2sb_setfuncswithupvalues( lua_State *L, const luaL_Reg *l, int nup )
{
    luaL_checkstack( L, nup, "too many upvalues" );
    for ( ; l->name != NULL; l++ )
    {
        int i;
        for ( i = 0; i < nup; i++ )
            lua_pushvalue( L, -nup );
        lua_pushcclosure( L, l->func, nup );
        lua_setfield( L, -( nup + 2 ), l->name );
    }
    lua_pop( L, nup );
}

LUALIB_API void luaL_openlib( lua_State *L, const char *libname,
                              const luaL_Reg *l, int nup )
{
    if ( libname )
    {
        luaL_pushmodule( L, libname, hl2sb_countfuncs( l ) );
        lua_insert( L, -( nup + 1 ) );
    }
    if ( l )
        hl2sb_setfuncswithupvalues( L, l, nup );
    else
        lua_settop( L, -( nup + 1 ) );
}

/* GMod's luaL_register is openlib(L, libname, l, 0) plus its own body
 * (separate function in the binary): pushmodule, insert(-1), checkstack
 * with the "too many upvalues" message, setfield loop, then settop(-1) --
 * it always pops exactly one value (the library table, or the caller's
 * table when libname is NULL). */
LUALIB_API void luaL_register( lua_State *L, const char *libname,
                               const luaL_Reg *l )
{
    if ( libname )
    {
        luaL_pushmodule( L, libname, hl2sb_countfuncs( l ) );
        lua_insert( L, -1 );
    }
    if ( l )
    {
        luaL_checkstack( L, 0, "too many upvalues" );
        for ( ; l->name != NULL; l++ )
        {
            lua_pushcfunction( L, l->func );
            lua_setfield( L, -2, l->name );
        }
    }
    lua_settop( L, -1 );
}

/* 5.1 spellings over the 5.4 x-functions: mode NULL is the "bt" default,
 * which is exactly what GMod's wrappers pass (they zero the mode register
 * before jumping into their loadfilex-equivalent). */
LUALIB_API int luaL_loadbuffer( lua_State *L, const char *buff, size_t sz,
                                const char *name )
{
    return luaL_loadbufferx( L, buff, sz, name, NULL );
}

LUALIB_API int luaL_loadfile( lua_State *L, const char *fname )
{
    return luaL_loadfilex( L, fname, NULL );
}

/* GMod extension (lua_shared 0x184e0): stores an extra type id in a
 * module-global around luaL_newmetatable, then clears it -- the id feeds
 * GMod's own metatable type checking, which this fork does not have.
 * Modules only depend on the metatable existing, so the id is accepted
 * and ignored. */
LUALIB_API int luaL_newmetatable_type( lua_State *L, const char *tname, int type )
{
    ( void )type;
    return luaL_newmetatable( L, tname );
}

/* {==== LuaJIT / GMod glue ====} */

static int hl2sb_jit_on( lua_State *L )
{
    lua_pushboolean( L, 1 );
    return 1;
}

static int hl2sb_jit_off( lua_State *L )
{
    lua_pushboolean( L, 1 );
    return 1;
}

/* No JIT in this fork: jit.status() reports off so callers take their
 * interpreter-safe paths. */
static int hl2sb_jit_status( lua_State *L )
{
    lua_pushboolean( L, 0 );
    return 1;
}

/* jit.opt is a TABLE in LuaJIT (jit.opt.start("hotloop=5") etc.). */
static const luaL_Reg hl2sb_jit_opt[] = {
    { "start", hl2sb_jit_on },
    { "mcode", hl2sb_jit_on },
    { "flush", hl2sb_jit_on },
    { NULL, NULL }
};

#if defined( _WIN32 )
#define HL2SB_JIT_OS "Windows"
#elif defined( __ANDROID__ )
#define HL2SB_JIT_OS "Android"
#elif defined( __APPLE__ )
#define HL2SB_JIT_OS "OSX"
#else
#define HL2SB_JIT_OS "Linux"
#endif

#if defined( _M_X64 ) || defined( __x86_64__ )
#define HL2SB_JIT_ARCH "x64"
#elif defined( _M_IX86 ) || defined( __i386__ )
#define HL2SB_JIT_ARCH "x86"
#elif defined( __aarch64__ )
#define HL2SB_JIT_ARCH "arm64"
#elif defined( __arm__ )
#define HL2SB_JIT_ARCH "arm"
#else
#define HL2SB_JIT_ARCH "unknown"
#endif

/* require("jit") / the global jit table (registered from linit.c's
 * loadedlibs, which also makes _G.jit exist exactly like in GMod). */
LUALIB_API int luaopen_jit( lua_State *L )
{
    int nopt;
    lua_createtable( L, 0, 8 );

    lua_pushliteral( L, "LuaJIT 2.1.0-beta3" );
    lua_setfield( L, -2, "version" );
    lua_pushinteger( L, 20100 );
    lua_setfield( L, -2, "version_num" );
    lua_pushliteral( L, HL2SB_JIT_OS );
    lua_setfield( L, -2, "os" );
    lua_pushliteral( L, HL2SB_JIT_ARCH );
    lua_setfield( L, -2, "arch" );

    lua_pushcfunction( L, hl2sb_jit_status );
    lua_setfield( L, -2, "status" );
    lua_pushcfunction( L, hl2sb_jit_on );
    lua_setfield( L, -2, "on" );
    lua_pushcfunction( L, hl2sb_jit_off );
    lua_setfield( L, -2, "off" );
    lua_pushcfunction( L, hl2sb_jit_on );
    lua_setfield( L, -2, "flush" );

    /* jit.opt = { start = f, mcode = f, flush = f } */
    lua_newtable( L );
    for ( nopt = 0; hl2sb_jit_opt[nopt].name != NULL; nopt++ )
    {
        lua_pushcfunction( L, hl2sb_jit_opt[nopt].func );
        lua_setfield( L, -2, hl2sb_jit_opt[nopt].name );
    }
    lua_setfield( L, -2, "opt" );

    return 1;
}

/* JIT control is meaningless without a JIT; claim success so modules that
 * assert on the result continue instead of bailing. */
LUA_API int luaJIT_setmode( lua_State *L, int idx, int mode )
{
    ( void )L;
    ( void )idx;
    ( void )mode;
    return 1;
}

/* Profile stubs: arguments are ignored on purpose -- a caller that passes
 * more/different registers than we read can never trip us, and only
 * jit.profile users (rare) notice the no-op. */
LUA_API void luaJIT_profile_start( lua_State *L, const char *modes,
                                   lua_CFunction cb, void *data )
{
    ( void )L;
    ( void )modes;
    ( void )cb;
    ( void )data;
}

LUA_API void luaJIT_profile_stop( lua_State *L )
{
    ( void )L;
}

LUA_API void luaJIT_profile_dumpstack( lua_State *L, const char *fmt,
                                       int depth, char *out, size_t osize )
{
    ( void )L;
    ( void )fmt;
    ( void )depth;
    ( void )out;
    ( void )osize;
}

/* GMod's lua_shared exports this as a 3-byte "ret 0" function at 0x7100 --
 * an address marker modules can reference for a version check. */
LUA_API int luaJIT_version_2_1_0_beta3( void )
{
    return 0;
}

/* {==== GMod glue ====} */

/* Valve-style interface factory.  GMod's lua_shared keeps a small registry
 * of interfaces registered from inside the DLL; ours has none, so every
 * lookup misses -- *pReturn = 1 (interface failed), NULL, which is exactly
 * what GMod's wrapper does on a miss (analysis of CreateInterface at
 * 0x5bd00: found -> *ret = 0, jump through factory; miss -> *ret = 1). */
LUA_API void *CreateInterface( const char *pName, int *pReturn )
{
    ( void )pName;
    if ( pReturn != NULL )
        *pReturn = 1;
    return NULL;
}

/* GMod exports two engine interface pointers as DATA from lua_shared
 * (modules link them directly).  The game DLLs push the real pointers in
 * once per realm -- luasrc_setmodulepaths calls this after both
 * g_pFullFileSystem and g_pCVar are up.  Until then they read NULL, which
 * is also what GMod's BSS slots hold before its init runs.
 * Declaration carries the export attribute, definition stays clean (see
 * HL2SB_DATAEXPORT above -- "extern void *cvar = NULL" would warn on gcc). */
HL2SB_DATAEXPORT extern void *cvar;
void *cvar = NULL;

LUA_API void HL2SB_InitializeEngineInterfaces( void *pFileSystem, void *pCvar )
{
    if ( pFileSystem != NULL )
    {
#ifdef __cplusplus
        g_pFullFileSystem = ( IFileSystem * )pFileSystem;
#else
        g_pFullFileSystem = ( struct IFileSystem * )pFileSystem;
#endif
    }
    if ( pCvar != NULL )
        cvar = pCvar;
}

/* GMod's msgh helper: a printable error passes through untouched; anything
 * else (table/userdata/...) becomes "<type> was given as Lua error!" --
 * the exact suffix GMod's wrapper appends (its string table carries
 * " was given as Lua error!" next to the reporter).  As a message handler
 * the original error sits at index 1; returning 1 keeps it, pushing the
 * replacement makes the pcall pick the top value.
 *
 * Exported with C linkage (plain name) on purpose: GMod defines it in a
 * .cpp and exports the decorated C++ name, but no third-party module ever
 * imports the reporter -- it exists for the game DLLs -- and a plain name
 * keeps the import identical on every platform and toolchain. */
LUA_API int AdvancedLuaErrorReporter( lua_State *L )
{
    if ( lua_isstring( L, 1 ) ) /* strings AND numbers, like 5.1 lua_isstring */
        return 1;
    lua_pushfstring( L, "%s was given as Lua error!", luaL_typename( L, 1 ) );
    return 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

/* C++-linked data export: reproduces GMod's
 * ?g_pFullFileSystem@@3PEAVIFileSystem@@EA (MSVC decorates class-typed
 * globals; the name only depends on the type spelling, so the forward
 * declaration above is enough).  On a real C build it lands under the
 * plain name, same as GMod's C compilation units. */
#ifdef __cplusplus
IFileSystem *g_pFullFileSystem = NULL;
#else
struct IFileSystem *g_pFullFileSystem = NULL;
#endif
