#define leffects_cpp

#include "cbase.h"
#include "luamanager.h"
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"
#include "explode.h"
#include "EntityDissolve.h"
#include "beam_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static int Effect_Disslove( lua_State *L )
{
	CEntityDissolve::Create(luaL_checkentity(L, 1), luaL_checkstring(L, 2), luaL_checknumber(L, 3), luaL_checkint(L, 4), NULL );
	return 1;
}

static int Effect_Explosion( lua_State *L )
{
	ExplosionCreate(luaL_checkvector(L, 1), luaL_checkangle(L, 2), luaL_checkentity(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), 
	luaL_checkboolean(L, 6), luaL_optnumber(L, 7, 0.0f ), luaL_optboolean(L, 8, false), luaL_optboolean(L, 9, false), luaL_optint(L, 10, -1));
	return 0;
}

static const luaL_Reg effects_funcs[] = {
	{"Dissolve", Effect_Disslove},
	{"ExplosionCreate", Effect_Explosion},
	{NULL, NULL}
};

LUALIB_API int luaopen_Effects (lua_State *L ) {
	luaL_register( L, "effect", effects_funcs );
	return 1;
}
