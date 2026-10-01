/*
 * Compatibility with Lua 5.1
 */

#include "cbase.h"
#include "lua.hpp"

lua_State *lua_open()
{
    return luaL_newstate();
}

void luaL_register( lua_State *L, const char *libname, const luaL_Reg *l )
{
    if ( libname )
    {
        luaL_getsubtable( L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE );
        lua_getfield( L, -1, libname );  // get package.loaded[libname]
        if ( !lua_istable( L, -1 ) )
        {
            lua_pop( L, 1 );  // remove previous result
            lua_newtable( L );
            lua_pushvalue( L, -1 );
            lua_setfield( L, -3,
                        libname );  // package.loaded[libname] = new table
        }
        lua_remove( L, -2 );  // remove package.loaded
        lua_pushvalue( L, -1 );
        lua_setglobal( L, libname );  // _G[libname] = new table
    }

    for ( ; l->name != NULL; l++ )
    {
        lua_pushcfunction( L, l->func );
        lua_setfield( L, -2, l->name );
    }
}

int luaL_typerror( lua_State *L, int narg, const char *tname )
{
    const char *msg = lua_pushfstring( L, "%s expected, got %s", tname, luaL_typename( L, narg ) );
    return luaL_argerror( L, narg, msg );
}

void lua_getref( lua_State *L, int ref )
{
    // HL2SB: LUA_NOREF (-2) and LUA_REFNIL (-1) are not registry keys.  Passing
    // them to lua_rawgeti() reads a negative integer key out of the registry
    // table -- an arbitrary slot that may hold a NUMBER (that is what a freed
    // registry slot contains, see luaL_unref).  A caller that then does
    // lua_getfield() on the result raises "attempt to index a number value"
    // from an unprotected context, which kills the process.  An invalid
    // reference means "no value", so push nil and let the lua_isrefvalid() /
    // lua_istable() guards at the call sites skip the dispatch.
    if ( ref < 0 )
    {
        lua_pushnil( L );
        return;
    }

    lua_rawgeti( L, LUA_REGISTRYINDEX, ref );
}

void lua_unref( lua_State *L, int ref )
{
    luaL_unref( L, LUA_REGISTRYINDEX, ref );
}

bool lua_isrefvalid( lua_State *L, int ref )
{
    // ref being 0 indicates we forgot to set `m_nTableReference = LUA_NOREF`
    // in a constructor somewhere.
    Assert( ref != 0 );

    // HL2SB (2026-10-01): a positive ref alone is not enough.  luaL_unref
    // leaves a tombstone NUMBER in the freed registry slot until the next
    // luaL_ref recycles it, and cleanup paths that unref do not reset the
    // field, so a live holder of such a ref reads the tombstone back.
    // Indexing it is the per-tick "attempt to index a number value" spam
    // from entity field lookups.  Every caller of this helper guards an
    // instance TABLE reference, so validity means the slot currently holds
    // a table; anything else is treated as "no script table".
    if ( ref < 0 || L == NULL )
        return false;

    lua_rawgeti( L, LUA_REGISTRYINDEX, ref );
    bool bValid = ( lua_istable( L, -1 ) != 0 );
    lua_pop( L, 1 );
    return bValid;
}
