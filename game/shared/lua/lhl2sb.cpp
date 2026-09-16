//========== HL2SB ===========//
//
// Purpose: Lua bindings for the HL2SB player model configuration system.
//
//          The GMod-style player model menu is written in Lua, so it needs the
//          same data the C++ context menu uses: the cfg/playermodel entries and
//          a way to apply one.  Exposing them here keeps the Lua menu entirely
//          data-driven - adding a model is still just dropping a .cfg file.
//
//===========================================================================//

#include "cbase.h"
#include "lua.hpp"
#include "luasrclib.h"
#include "hl2sb_model_config.h"

// HL2SB: global IsValid( ent ) needs the entity type-checking helper.
#include "lbaseentity_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#include "c_baseplayer.h"

// HL2SB: the Lua spawn menu's data sources - the client's class map and the
// server's published spawn list.  Both are engine-side, so they are reachable
// only from here (see hl2sb_GetSpawnableClasses below).
#include "iclassmap.h"
#include "networkstringtable_clientdll.h"
#endif

#include "lbaseplayer_shared.h"

//-----------------------------------------------------------------------------
// Purpose: global IsValid( ent ) - HL2SB
//
// GMod scripts call IsValid( ent ) as a free function.  HL2SB exposes the same
// via util.IsValid, but not as a global.  We must NOT use luaL_checkentity here
// because that raises a Lua error when the entity pointer is NULL - IsValid
// should return false for an invalid entity, not throw.  lua_toentity returns
// NULL for NULL/bad handles without raising.
//
// HL2SB: PLAYERS ARE A SEPARATE USERDATA TYPE and lua_toentity() returns NULL
// for them, so this answered false for a perfectly valid player.  Measured in
// game with hl2sb_hud_debug 1:
//
//     LocalPlayer()=CBasePlayer: 2 "hut"   IsValid=false   IsAlive=true
//
// That one wrong answer cost two GMod HUDs:
//   * lua/game/client/hl2sb_cl_hudpickup.lua refused every pickup (it asked
//     IsValid/ Alive of the local player), so the pickup list never filled;
//   * lua/includes/modules/undo.lua:396 is
//         if ( !IsValid( Current_Undo.Owner ) or ... ) then return false end
//     so undo.Finish() threw away EVERY undo, "undo" always answered
//     "no undo entry recorded", and no notice could ever be queued.
//
// Try the entity first (unchanged behaviour for entities), then the player.
//-----------------------------------------------------------------------------
static int hl2sb_GlobalIsValid( lua_State *L )
{
	if ( lua_gettop( L ) < 1 || lua_isnoneornil( L, 1 ) )
	{
		lua_pushboolean( L, false );
		return 1;
	}

	if ( lua_toentity( L, 1 ) != NULL )
	{
		lua_pushboolean( L, true );
		return 1;
	}

	lua_pushboolean( L, lua_toplayer( L, 1 ) != NULL );
	return 1;
}


//-----------------------------------------------------------------------------
// Purpose: hl2sb.GetPlayerModels()
//
// Returns an array of { name, model, hands, file } for every loaded
// cfg/playermodel entry, in load order.
//-----------------------------------------------------------------------------
static int hl2sb_GetPlayerModels( lua_State *L )
{
	HL2SB_EnsureModelConfigsLoaded();

	lua_newtable( L );

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		const HL2SB_ModelConfig_t &cfg = g_HL2SB_ModelConfigs[i];

		lua_newtable( L );

		lua_pushstring( L, cfg.szName );
		lua_setfield( L, -2, "name" );

		lua_pushstring( L, cfg.szPlayerModel );
		lua_setfield( L, -2, "model" );

		lua_pushstring( L, cfg.szHandsModel );
		lua_setfield( L, -2, "hands" );

		lua_pushstring( L, cfg.szConfigFile );
		lua_setfield( L, -2, "file" );

		lua_rawseti( L, -2, i + 1 );
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.GetPlayerModelCount()
//-----------------------------------------------------------------------------
static int hl2sb_GetPlayerModelCount( lua_State *L )
{
	HL2SB_EnsureModelConfigsLoaded();
	lua_pushinteger( L, g_nHL2SB_ModelConfigCount );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.FindPlayerModel( name )
// Returns the entry table, or nil when there is no such config.
//-----------------------------------------------------------------------------
static int hl2sb_FindPlayerModel( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );

	HL2SB_EnsureModelConfigsLoaded();

	const HL2SB_ModelConfig_t *pCfg = HL2SB_GetModelConfigByName( pszName );
	if ( !pCfg )
	{
		lua_pushnil( L );
		return 1;
	}

	lua_newtable( L );

	lua_pushstring( L, pCfg->szName );
	lua_setfield( L, -2, "name" );

	lua_pushstring( L, pCfg->szPlayerModel );
	lua_setfield( L, -2, "model" );

	lua_pushstring( L, pCfg->szHandsModel );
	lua_setfield( L, -2, "hands" );

	lua_pushstring( L, pCfg->szConfigFile );
	lua_setfield( L, -2, "file" );

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.SetPlayerModel( name )
//
// Applies a config by running the same console command the C++ menu uses, so
// both paths go through one implementation.  Returns false when the name is
// unknown, so the menu can report it instead of silently doing nothing.
//-----------------------------------------------------------------------------
static int hl2sb_SetPlayerModel( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );

	HL2SB_EnsureModelConfigsLoaded();

	if ( !HL2SB_GetModelConfigByName( pszName ) )
	{
		lua_pushboolean( L, false );
		return 1;
	}

#ifdef CLIENT_DLL
	char szCmd[256];
	Q_snprintf( szCmd, sizeof( szCmd ), "hl2sb_setmodel %s\n", pszName );
	engine->ClientCmd( szCmd );
#endif

	lua_pushboolean( L, true );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.GetCurrentPlayerModel()
// Full path of the local player's model, or "" when there is none.
//-----------------------------------------------------------------------------
static int hl2sb_GetCurrentPlayerModel( lua_State *L )
{
#ifdef CLIENT_DLL
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->GetModel() )
	{
		const char *pszName = modelinfo->GetModelName( pPlayer->GetModel() );
		lua_pushstring( L, pszName ? pszName : "" );
		return 1;
	}
#endif

	lua_pushstring( L, "" );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.IsModelPrecached( path )
//
// The server precaches every cfg/playermodel entry during
// CHL2MPRules::Precache(), but a model the client has not received cannot be
// rendered by vgui.ModelPanel.  The menu uses this to mark unusable entries
// instead of showing a blank preview.
//-----------------------------------------------------------------------------
static int hl2sb_IsModelPrecached( lua_State *L )
{
	const char *pszModel = luaL_checkstring( L, 1 );

#ifdef CLIENT_DLL
	lua_pushboolean( L, modelinfo->GetModelIndex( pszModel ) != -1 );
#else
	lua_pushboolean( L, true );
#endif

	return 1;
}

//-----------------------------------------------------------------------------
// HL2SB: hl2sb.AddPlayerModel( name, model [, hands] )
//
// The engine end of `player_manager.AddValidModel` / `AddValidHands`: a Garry's
// Mod playermodel addon registers from Lua and ships no cfg/playermodel entry,
// while the menu, the server precache and hl2sb.SetPlayerModel() all read the
// engine table - so those Lua calls land here (see
// HL2SB_AddRuntimeModelConfig for the update-in-place rules).
//
//   name  - the key, spelled exactly like a cfg entry's file name
//   model - model path (optional: nil/"" keeps the current path)
//   hands - hands model (optional: nil keeps, "" clears; the cfg encoding
//           "path|skin|bodygroups" is accepted)
//-----------------------------------------------------------------------------
static int hl2sb_AddPlayerModel( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );
	const char *pszModel = luaL_optstring( L, 2, NULL );
	const char *pszHands = luaL_optstring( L, 3, NULL );

	HL2SB_AddRuntimeModelConfig( pszName, pszModel, pszHands );

	return 0;
}

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Purpose: hl2sb.GetSpawnableClasses()
//
// The "what can I spawn" list, for the Lua spawn menu.  These are the same two
// sources the C++ menu read, and the only part of it that Lua cannot reach on
// its own:
//
//   * the client's own class map (iclassmap.h) - the scripted entities/weapons
//     this client registered, plus the engine classes it knows.
//   * the server's published "SMenuEntityList" network string table
//     (game/server/gameinterface.cpp: SMenu_PublishEntityList) - every class the
//     server's entity factory dictionary can build, which is what actually
//     decides whether `ent_create <class>` works.
//
// Returns an array of { class =, cpp =, scripted = }, de-duplicated.  Everything
// else the menu needs - PrintName, Category, icons, spawn commands - is normal
// Lua data (list.Get / scripted_ents / language / file.Exists).
//-----------------------------------------------------------------------------
#define HL2SB_SMENU_ENTITYLIST_TABLE "SMenuEntityList"

static int hl2sb_GetSpawnableClasses( lua_State *L )
{
	CUtlDict< int, unsigned short > seen;

	lua_newtable( L );

	int nOut = 0;

	// 1. the classes this client knows (scripted content included)
	const int nClasses = ClassMap_GetEntryCount();

	for ( int i = 0; i < nClasses; ++i )
	{
		const char *pszClass = ClassMap_GetEntryName( i );

		if ( pszClass == NULL || pszClass[0] == '\0' )
			continue;

		if ( seen.Find( pszClass ) != seen.InvalidIndex() )
			continue;

		seen.Insert( pszClass, 1 );

		lua_newtable( L );

		lua_pushstring( L, pszClass );
		lua_setfield( L, -2, "class" );

		const char *pszCPP = ClassMap_GetEntryCPPName( i );
		lua_pushstring( L, ( pszCPP != NULL ) ? pszCPP : "" );
		lua_setfield( L, -2, "cpp" );

		lua_pushboolean( L, ClassMap_IsEntryScripted( i ) ? 1 : 0 );
		lua_setfield( L, -2, "scripted" );

		lua_rawseti( L, -2, ++nOut );
	}

	// 2. what the server says its entity factory dictionary can build
	INetworkStringTable *pTable = networkstringtable ? networkstringtable->FindTable( HL2SB_SMENU_ENTITYLIST_TABLE ) : NULL;

	if ( pTable != NULL )
	{
		const int nStrings = pTable->GetNumStrings();

		for ( int i = 0; i < nStrings; ++i )
		{
			const char *pszClass = pTable->GetString( i );

			if ( pszClass == NULL || pszClass[0] == '\0' )
				continue;

			if ( seen.Find( pszClass ) != seen.InvalidIndex() )
				continue;

			seen.Insert( pszClass, 1 );

			lua_newtable( L );

			lua_pushstring( L, pszClass );
			lua_setfield( L, -2, "class" );

			lua_pushstring( L, "" );
			lua_setfield( L, -2, "cpp" );

			lua_pushboolean( L, 0 );
			lua_setfield( L, -2, "scripted" );

			lua_rawseti( L, -2, ++nOut );
		}
	}

	return 1;
}
#endif // CLIENT_DLL

static const luaL_Reg hl2sblib[] = {
	{"GetPlayerModels",			hl2sb_GetPlayerModels},
	{"GetPlayerModelCount",		hl2sb_GetPlayerModelCount},
	{"FindPlayerModel",			hl2sb_FindPlayerModel},
	{"SetPlayerModel",			hl2sb_SetPlayerModel},
	{"GetCurrentPlayerModel",	hl2sb_GetCurrentPlayerModel},
	{"IsModelPrecached",		hl2sb_IsModelPrecached},
	{"AddPlayerModel",			hl2sb_AddPlayerModel},
#ifdef CLIENT_DLL
	{"GetSpawnableClasses",		hl2sb_GetSpawnableClasses},
#endif
	{NULL, NULL}
};

/*
** Open hl2sb library
*/
LUALIB_API int luaopen_hl2sb( lua_State *L )
{
	luaL_register( L, LUA_HL2SBLIBNAME, hl2sblib );

	// HL2SB: expose a global IsValid( ent ) so GMod-style scripts (and the
	// Lua undo system) can test entities without the util. prefix.
	lua_pushcfunction( L, hl2sb_GlobalIsValid );
	lua_setglobal( L, "IsValid" );

	return 1;
}
