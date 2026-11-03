//=============================================================================//
//
// Purpose: Registrar for the Experiment-style Lua binding framework.
//          Ported from Experiment: Source (src/game/shared/lsrcinit.cpp,
//          luaL_register overload).
//
//          Upstream uses luaL_getsubtable() + LUA_LOADED_TABLE, which are Lua
//          5.2+ APIs.  HL2SB runs Lua 5.1, so the same behaviour is written out
//          with 5.1 primitives; it compiles and behaves identically on 5.4.
//
//=============================================================================//

#include "cbase.h"

#include "luabinding.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Builds a Lua table (or writes into the table on top of the stack when libname
// is NULL) from a CUtlVector registry.  Mirrors Lua 5.1's luaL_register:
//   * package.loaded[libname] and _G[libname] both end up pointing at the table,
//   * an existing table is reused rather than replaced,
//   * the library table is left on the stack.
void luaL_register( lua_State *L, const char *libname, CUtlVector< LuaRegEntry > &luaRegistry )
{
	if ( libname )
	{
#if LUA_VERSION_NUM < 502
		static const luaL_Reg empty[] = { { NULL, NULL } };
		luaL_register( L, libname, empty );
#else
		lua_getglobal( L, "package" );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "loaded" );
			if ( lua_istable( L, -1 ) )
			{
				lua_getfield( L, -1, libname );
				if ( !lua_istable( L, -1 ) )
				{
					lua_pop( L, 1 );
					lua_newtable( L );
					lua_pushvalue( L, -1 );
					lua_setfield( L, -3, libname );	// package.loaded[libname] = new table
				}
				lua_remove( L, -2 );				// remove package.loaded
			}
			else
			{
				lua_pop( L, 2 );					// no package.loaded: leave package
				lua_newtable( L );
			}
			lua_remove( L, -2 );					// remove package
		}
		else
		{
			lua_pop( L, 1 );						// no package table at all
			lua_newtable( L );
		}

		lua_pushvalue( L, -1 );
		lua_setglobal( L, libname );			// _G[libname] = table
#endif
	}

	for ( int i = 0; i < luaRegistry.Count(); i++ )
	{
		lua_pushcfunction( L, luaRegistry[i].function );
		lua_setfield( L, -2, luaRegistry[i].name );
	}
}

// See luabinding.h.  Merges into the existing entity table when there is one, so the
// two contributors (the ported Entities library and the CBaseFlex bindings) both end
// up in the same table no matter which library the engine opens first.
void luaL_register_entity_library( lua_State *L, CUtlVector< LuaRegEntry > &luaRegistry )
{
	lua_getglobal( L, "ents" );				// [ents]
	if ( !lua_istable( L, -1 ) )			// no table yet: make one
	{
		lua_pop( L, 1 );
		lua_newtable( L );					// [table]
	}

	for ( int i = 0; i < luaRegistry.Count(); i++ )
	{
		lua_pushcfunction( L, luaRegistry[i].function );
		lua_setfield( L, -2, luaRegistry[i].name );
	}

	lua_pushvalue( L, -1 );
	lua_setglobal( L, "ents" );				// _G.ents = table
	lua_pushvalue( L, -1 );
	lua_setglobal( L, "Entities" );			// _G.Entities = table

	lua_getfield( L, LUA_REGISTRYINDEX, "_LOADED" );
	if ( lua_istable( L, -1 ) )
	{
		lua_pushvalue( L, -2 );
		lua_setfield( L, -2, "Entities" );	// require( "Entities" )
	}
	lua_pop( L, 1 );

	// [table] left on the stack for the caller.
}
