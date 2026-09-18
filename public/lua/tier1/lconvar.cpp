//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $NoKeywords: $
//===========================================================================//

#define lconvar_cpp

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lconvar.h"
#include "lbaseplayer_shared.h"
#include "datacache/imdlcache.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

/*
** access functions (stack -> C)
*/


LUA_API lua_ConCommand *lua_toconcommand (lua_State *L, int idx) {
  lua_ConCommand **ppConCommand = (lua_ConCommand **)lua_touserdata(L, idx);
  return *ppConCommand;
}


LUA_API lua_ConVar *lua_toconvar (lua_State *L, int idx) {
  lua_ConVar **ppConVar = (lua_ConVar **)lua_touserdata(L, idx);
  return *ppConVar;
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushconcommand (lua_State *L, lua_ConCommand *pConCommand) {
  if (pConCommand == NULL)
    lua_pushnil(L);
  else {
    lua_ConCommand **ppConCommand = (lua_ConCommand **)lua_newuserdata(L, sizeof(pConCommand));
    *ppConCommand = pConCommand;
    luaL_getmetatable(L, "ConCommand");
    lua_setmetatable(L, -2);
  }
}


LUA_API void lua_pushconvar (lua_State *L, lua_ConVar *pConVar) {
  if (pConVar == NULL)
    lua_pushnil(L);
  else {
    lua_ConVar **ppConVar = (lua_ConVar **)lua_newuserdata(L, sizeof(pConVar));
    *ppConVar = pConVar;
    luaL_getmetatable(L, "ConVar");
    lua_setmetatable(L, -2);
  }
}


LUALIB_API lua_ConCommand *luaL_checkconcommand (lua_State *L, int narg) {
  lua_ConCommand **d = (lua_ConCommand **)luaL_checkudata(L, narg, "ConCommand");
  return *d;
}


LUALIB_API lua_ConVar *luaL_checkconvar (lua_State *L, int narg) {
  lua_ConVar **d = (lua_ConVar **)luaL_checkudata(L, narg, "ConVar");
  return *d;
}


static int ConCommand_CanAutoComplete (lua_State *L) {
  lua_pushboolean(L, luaL_checkconcommand(L, 1)->CanAutoComplete());
  return 1;
}

static int ConCommand_IsCommand (lua_State *L) {
  lua_pushboolean(L, luaL_checkconcommand(L, 1)->IsCommand());
  return 1;
}

static int ConCommand___tostring (lua_State *L) {
  lua_pushfstring(L, "ConCommand: \"%s\"", luaL_checkconcommand(L, 1)->GetName());
  return 1;
}


static const luaL_Reg ConCommandmeta[] = {
  {"CanAutoComplete", ConCommand_CanAutoComplete},
  {"IsCommand", ConCommand_IsCommand},
  {"__tostring", ConCommand___tostring},
  {NULL, NULL}
};


#ifdef CLIENT_DLL
static CUtlDict< ConCommand*, unsigned short > m_GameUIConCommandDatabase;
#endif
static CUtlDict< ConCommand*, unsigned short > m_ConCommandDatabase;

// HL2SB: the two helpers below make Lua concommands speak GMod.
//
// GMod's engine calls concommand.Run( ply, cmd, arguments, argumentsStr ) with
// `arguments` a TABLE of everything after the command name (arguments[1] is the
// first argument) and argumentsStr the raw tail.  HL2SB only called
// Dispatch( ply, cmd, argString ), i.e. the raw string sat in the arguments
// slot, so every GMod-style callback that indexes arguments[1] silently did
// nothing:
//
//     lua/autorun/server/hl2sb_spawn_undo.lua  concommand.Add( "hl2sb_spawnprop", ... )
//         if ( args == nil or args[ 1 ] == nil ... ) then Dbg( "hl2sb_spawnprop <model>" ) end
//     lua/includes/modules/undo.lua            "gmod_undonum" -> tonumber( args[ 1 ] )
//     lua/includes/modules/cleanup.lua         args[ 1 ] (the filter name)
//
// All three just printed usage and returned.  The engine now asks for Run first
// and falls back to Dispatch (old signature) when a Lua state only defines that
// one, so an older lua/ tree keeps working with a newer client.dll.
static void lua_pushcommandarguments (lua_State *L, const CCommand& args) {
  lua_newtable(L);
  // arguments[1..n] -- NOT args[0], the command name is passed separately.
  for (int i = 1; i < args.ArgC(); ++i) {
    lua_pushstring(L, args.Arg(i));
    lua_rawseti(L, -2, i);
  }
}

// Leaves exactly one value on the stack: the dispatcher function to call, or
// nothing (returns false) when the Lua state has no usable concommand library.
// bRunMode reports which calling convention that function expects.
static bool lua_pushconcommanddispatcher (lua_State *L, bool &bRunMode) {
  lua_getglobal(L, "concommand");              // [concommand]
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  lua_getfield(L, -1, "Run");                  // [concommand, Run]
  if (lua_isfunction(L, -1)) {
    lua_remove(L, -2);                         // [Run]
    bRunMode = true;
    return true;
  }

  lua_pop(L, 1);                               // [concommand]
  lua_getfield(L, -1, "Dispatch");             // [concommand, Dispatch]
  bool bIsFunction = lua_isfunction(L, -1) != 0;
  lua_remove(L, -2);                           // [Dispatch]
  if (!bIsFunction) {
    lua_pop(L, 1);
    return false;
  }

  bRunMode = false;
  return true;
}

#ifdef CLIENT_DLL
// Andrew; ugh.
void CC_GameUIConCommand( const CCommand& args )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	const char *pCmd = args[0];

	// Is the client spawned yet?
	// if ( !pPlayer )
	// 	return;

	MDLCACHE_CRITICAL_SECTION();

	bool bRunMode = false;
	if ( lua_pushconcommanddispatcher( LGameUI, bRunMode ) )
	{
		lua_pushplayer( LGameUI, pPlayer );
		lua_pushstring( LGameUI, pCmd );
		if ( bRunMode )
		{
			lua_pushcommandarguments( LGameUI, args );
			lua_pushstring( LGameUI, args.ArgS() );
		}
		else
		{
			lua_pushstring( LGameUI, args.ArgS() );
		}
		luasrc_pcall( LGameUI, bRunMode ? 4 : 3, 1, 0 );
		if ( lua_isboolean( LGameUI, -1 ) )
		{
			bool res = (bool)luaL_checkboolean( LGameUI, -1 );
			lua_pop( LGameUI, 1 );
			if ( !res )
			{
			}
		}
		else
		{
			lua_pop( LGameUI, 1 );
		}
	}
}
#endif

void CC_ConCommand( const CCommand& args )
{
#ifdef CLIENT_DLL
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
#else
	CBasePlayer* pPlayer = UTIL_GetCommandClient();
#endif
	const char *pCmd = args[0];

	// Is the client spawned yet?
	// if ( !pPlayer )
	// 	return;

	MDLCACHE_CRITICAL_SECTION();

	bool bRunMode = false;
	if ( lua_pushconcommanddispatcher( L, bRunMode ) )
	{
		lua_pushplayer( L, pPlayer );
		lua_pushstring( L, pCmd );
		if ( bRunMode )
		{
			lua_pushcommandarguments( L, args );
			lua_pushstring( L, args.ArgS() );
		}
		else
		{
			lua_pushstring( L, args.ArgS() );
		}
		luasrc_pcall( L, bRunMode ? 4 : 3, 1, 0 );
		if ( lua_isboolean( L, -1 ) )
		{
			bool res = (bool)luaL_checkboolean( L, -1 );
			lua_pop( L, 1 );
			if ( !res )
			{
#ifndef CLIENT_DLL
				if ( Q_strlen( pCmd ) > 128 )
				{
					ClientPrint( pPlayer, HUD_PRINTCONSOLE, "Console command too long.\n" );
				}
				else
				{
					// tell the user they entered an unknown command
					ClientPrint( pPlayer, HUD_PRINTCONSOLE, UTIL_VarArgs( "Unknown command: %s\n", pCmd ) );
				}
#endif
			}
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
}

static int luasrc_ConCommand (lua_State *L) {
  const char *pName = luaL_checkstring(L, 1);
#ifdef CLIENT_DLL
  bool bIsGameUI = false;
  unsigned short lookup;
  lua_getglobal(L, "_GAMEUI");
  if (!lua_isnoneornil(L, -1) && lua_toboolean(L, -1)) {
    bIsGameUI = true;

    // Complain about duplicately defined ConCommand names...
    lookup = m_GameUIConCommandDatabase.Find( pName );
    if ( lookup != m_GameUIConCommandDatabase.InvalidIndex() || cvar->FindCommand(pName) )
    {
      lua_pushconcommand(L, cvar->FindCommand(pName));
      return 1;
    }
  } else {
#endif
    // Complain about duplicately defined ConCommand names...
    unsigned short lookup = m_ConCommandDatabase.Find( pName );
    if ( lookup != m_ConCommandDatabase.InvalidIndex() || cvar->FindCommand(pName) )
    {
      lua_pushconcommand(L, cvar->FindCommand(pName));
      return 1;
    }
#ifdef CLIENT_DLL
  }
#endif
  lua_pop(L, 1);

  ConCommand *pConCommand;
#ifdef CLIENT_DLL
  if (bIsGameUI)
#if 0
    pConCommand = new ConCommand(strdup(pName), CC_GameUIConCommand, strdup(luaL_optstring(L, 2, 0)), luaL_optint(L, 3, 0), NULL);
#else
    pConCommand = new ConCommand(strdup(pName), CC_GameUIConCommand, strdup(luaL_optstring(L, 2, 0)), 0, NULL);
#endif
  else
    pConCommand = new ConCommand(strdup(pName), CC_ConCommand, strdup(luaL_optstring(L, 2, 0)), FCVAR_CLIENTDLL | FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE, NULL);
#else
#if 0
    pConCommand = new ConCommand(strdup(pName), CC_ConCommand, strdup(luaL_optstring(L, 2, 0)), luaL_optint(L, 3, 0), NULL);
#else
    pConCommand = new ConCommand(strdup(pName), CC_ConCommand, strdup(luaL_optstring(L, 2, 0)), FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE, NULL);
#endif
#endif

#ifdef CLIENT_DLL
  if (bIsGameUI) {
    lookup = m_GameUIConCommandDatabase.Insert( pName, pConCommand );
    Assert( lookup != m_GameUIConCommandDatabase.InvalidIndex() );
  } else {
#endif
    lookup = m_ConCommandDatabase.Insert( pName, pConCommand );
    Assert( lookup != m_ConCommandDatabase.InvalidIndex() );
#ifdef CLIENT_DLL
  }
#endif
  lua_pushconcommand(L, pConCommand);
  return 1;
}

#ifdef CLIENT_DLL
void ResetGameUIConCommandDatabase( void )
{
	for ( int i=m_GameUIConCommandDatabase.First(); i != m_GameUIConCommandDatabase.InvalidIndex(); i=m_GameUIConCommandDatabase.Next( i ) )
	{
		ConCommand *pConCommand = m_GameUIConCommandDatabase[ i ];
		cvar->UnregisterConCommand(pConCommand);
		delete pConCommand;
	}
	m_GameUIConCommandDatabase.RemoveAll();
}
#endif

void ResetConCommandDatabase( void )
{
	for ( int i=m_ConCommandDatabase.First(); i != m_ConCommandDatabase.InvalidIndex(); i=m_ConCommandDatabase.Next( i ) )
	{
		ConCommand *pConCommand = m_ConCommandDatabase[ i ];
		cvar->UnregisterConCommand(pConCommand);
		delete pConCommand;
	}
	m_ConCommandDatabase.RemoveAll();
}


static const luaL_Reg ConCommand_funcs[] = {
  {"ConCommand", luasrc_ConCommand},
  {NULL, NULL}
};


/*
** Open ConCommand object
*/
LUALIB_API int luaopen_ConCommand (lua_State *L) {
  luaL_newmetatable(L, LUA_CONCOMMANDLIBNAME);
  luaL_register(L, NULL, ConCommandmeta);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  lua_pushstring(L, "concommand");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "concommand" */
  luaL_register(L, "_G", ConCommand_funcs);
  lua_pop(L, 1);
  return 1;
}


static int ConVar_AddFlags (lua_State *L) {
  luaL_checkconvar(L, 1)->AddFlags(luaL_checkint(L, 2));
  return 0;
}

static int ConVar_GetBool (lua_State *L) {
  lua_pushboolean(L, luaL_checkconvar(L, 1)->GetBool());
  return 1;
}

static int ConVar_GetDefault (lua_State *L) {
  lua_pushstring(L, luaL_checkconvar(L, 1)->GetDefault());
  return 1;
}

static int ConVar_GetFloat (lua_State *L) {
  lua_pushnumber(L, luaL_checkconvar(L, 1)->GetFloat());
  return 1;
}

static int ConVar_GetHelpText (lua_State *L) {
  lua_pushstring(L, luaL_checkconvar(L, 1)->GetHelpText());
  return 1;
}

static int ConVar_GetInt (lua_State *L) {
  lua_pushinteger(L, luaL_checkconvar(L, 1)->GetInt());
  return 1;
}

static int ConVar_GetMax (lua_State *L) {
  float maxVal;
  lua_pushboolean(L, luaL_checkconvar(L, 1)->GetMax(maxVal));
  lua_pushnumber(L, maxVal);
  return 2;
}

static int ConVar_GetMin (lua_State *L) {
  float minVal;
  lua_pushboolean(L, luaL_checkconvar(L, 1)->GetMin(minVal));
  lua_pushnumber(L, minVal);
  return 2;
}

static int ConVar_GetName (lua_State *L) {
  lua_pushstring(L, luaL_checkconvar(L, 1)->GetName());
  return 1;
}

static int ConVar_GetString (lua_State *L) {
  lua_pushstring(L, luaL_checkconvar(L, 1)->GetString());
  return 1;
}

static int ConVar_IsCommand (lua_State *L) {
  lua_pushboolean(L, luaL_checkconvar(L, 1)->IsCommand());
  return 1;
}

static int ConVar_IsFlagSet (lua_State *L) {
  lua_pushboolean(L, luaL_checkconvar(L, 1)->IsFlagSet(luaL_checkint(L, 2)));
  return 1;
}

static int ConVar_IsRegistered (lua_State *L) {
  lua_pushboolean(L, luaL_checkconvar(L, 1)->IsRegistered());
  return 1;
}

static int ConVar_Revert (lua_State *L) {
  luaL_checkconvar(L, 1)->Revert();
  return 0;
}

static int ConVar_SetValue (lua_State *L) {
  switch(lua_type(L, 2)) {
	case LUA_TNUMBER:
	  luaL_checkconvar(L, 1)->SetValue((float)luaL_checknumber(L, 2));
	  break;
	case LUA_TSTRING:
	default:
	  luaL_checkconvar(L, 1)->SetValue(luaL_checkstring(L, 2));
	  break;
  }
  return 0;
}

static int ConVar___tostring (lua_State *L) {
  lua_pushfstring(L, "ConVar: \"%s\" = \"%s\"", luaL_checkconvar(L, 1)->GetName(), luaL_checkconvar(L, 1)->GetString());
  return 1;
}

// HL2SB GMod compat: the wiki documents SetString/SetFloat/SetInt/SetBool on ConVar
// (only the polymorphic SetValue was bound). These are what the Derma ConVar binding
// (Panel:SetConVar) uses to write a control's value back to the console variable.
static int ConVar_SetString (lua_State *L) {
  luaL_checkconvar(L, 1)->SetValue( lua_tostring(L, 2) );
  return 0;
}

static int ConVar_SetFloat (lua_State *L) {
  luaL_checkconvar(L, 1)->SetValue( (float)luaL_checknumber(L, 2) );
  return 0;
}

static int ConVar_SetInt (lua_State *L) {
  luaL_checkconvar(L, 1)->SetValue( (int)luaL_checkinteger(L, 2) );
  return 0;
}

static int ConVar_SetBool (lua_State *L) {
  luaL_checkconvar(L, 1)->SetValue( luaL_checkboolean(L, 2) );
  return 0;
}


static const luaL_Reg ConVarmeta[] = {
  {"AddFlags", ConVar_AddFlags},
  {"GetBool", ConVar_GetBool},
  {"GetDefault", ConVar_GetDefault},
  {"GetFloat", ConVar_GetFloat},
  {"GetHelpText", ConVar_GetHelpText},
  {"GetInt", ConVar_GetInt},
  {"GetMax", ConVar_GetMax},
  {"GetMin", ConVar_GetMin},
  {"GetName", ConVar_GetName},
  {"GetString", ConVar_GetString},
  {"IsCommand", ConVar_IsCommand},
  {"IsFlagSet", ConVar_IsFlagSet},
  {"IsRegistered", ConVar_IsRegistered},
  {"Revert", ConVar_Revert},
  {"SetValue", ConVar_SetValue},
  {"SetString", ConVar_SetString},
  {"SetFloat", ConVar_SetFloat},
  {"SetInt", ConVar_SetInt},
  {"SetBool", ConVar_SetBool},
  {"__tostring", ConVar___tostring},
  {NULL, NULL}
};


static CUtlDict< ConVar*, unsigned short > m_ConVarDatabase;

static int luasrc_ConVar (lua_State *L) {
  const char *pName = luaL_checkstring(L, 1);

  // HL2SB: GMod's global ConVar( name ) is a LOOKUP when called with one
  // argument.  ConVarExists() (extensions/gmod_globals.lua) builds on exactly
  // that and cod_c4 gates every CreateConVar on ConVarExists -- with the old
  // unconditional luaL_checkstring(L, 2) the single-argument call raised
  // "bad argument #2 to 'ConVar' (string expected, got no value)", the whole
  // cod-c4 shared.lua failed to load, and the C4_* convars, the
  // "C4_Convars_Change" net string and the client callbacks never existed.
  if ( lua_gettop( L ) < 2 )
  {
    unsigned short existing = m_ConVarDatabase.Find( pName );
    ConVar *pFound = ( existing != m_ConVarDatabase.InvalidIndex() )
      ? m_ConVarDatabase[ existing ]
      : cvar->FindVar( pName );

    if ( pFound )
    {
      lua_pushconvar( L, pFound );
      return 1;
    }
    lua_pushnil( L );
    return 1;
  }

  // Complain about duplicately defined ConVar names...
  unsigned short lookup = m_ConVarDatabase.Find( pName );
  if ( lookup != m_ConVarDatabase.InvalidIndex() || cvar->FindVar(pName) )
  {
    lua_pushconvar(L, cvar->FindVar(pName));
    return 1;
  }

  // HL2SB: GMod accepts a number (or boolean) default -- cod_c4 writes
  // CreateConVar( "C4_Infinite", 0, ... ).  The engine ConVar takes a string.
  const char *pDefault;
  if ( lua_isboolean( L, 2 ) )
    pDefault = lua_toboolean( L, 2 ) ? "1" : "0";
  else
    pDefault = luaL_checkstring( L, 2 );  // numbers auto-convert

  ConVar *pConVar = new ConVar(strdup(pName), pDefault, luaL_optint(L, 3, 0), strdup(luaL_optstring(L, 4, 0)), luaL_optboolean(L, 5, 0), luaL_optnumber(L, 6, 0.0), luaL_optboolean(L, 7, 0), luaL_optnumber(L, 8, 0));

  lookup = m_ConVarDatabase.Insert( pName, pConVar );
  Assert( lookup != m_ConVarDatabase.InvalidIndex() );
  lua_pushconvar(L, pConVar);
  return 1;
}

// HL2SB: GMod's engine global GetConVar_Internal( name ).
//
// GMod's lua/includes/util.lua builds GetConVar / GetConVarNumber /
// GetConVarString / GetConVarBool on top of this, and it redefines them
// UNCONDITIONALLY -- so once that file runs, whatever the Lua side had before is
// gone.  Without this function every one of those became "attempt to call a nil
// value (global 'GetConVar_Internal')" and took the kill feed and undo with it.
//
// Contract differences from luasrc_ConVar() below, both deliberate:
//   * it never CREATES a convar -- an unknown name yields nil, which is what
//     GMod's GetConVar() promises (scripts write `if ( !GetConVar(x) ) then`);
//   * a name created from Lua is found through m_ConVarDatabase, because
//     luasrc_ConVar() does not go through cvar->RegisterConCommand().
static int luasrc_GetConVar_Internal (lua_State *L) {
  const char *pName = luaL_checkstring(L, 1);

  unsigned short lookup = m_ConVarDatabase.Find( pName );
  if ( lookup != m_ConVarDatabase.InvalidIndex() )
  {
    lua_pushconvar(L, m_ConVarDatabase[ lookup ] );
    return 1;
  }

  ConVar *pConVar = cvar->FindVar( pName );
  if ( !pConVar )
  {
    lua_pushnil(L);
    return 1;
  }

  lua_pushconvar(L, pConVar);
  return 1;
}

void ResetConVarDatabase( void )
{
	for ( int i=m_ConVarDatabase.First(); i != m_ConVarDatabase.InvalidIndex(); i=m_ConVarDatabase.Next( i ) )
	{
		ConVar *pConVar = m_ConVarDatabase[ i ];
		cvar->UnregisterConCommand(pConVar);
		delete pConVar;
	}
	m_ConVarDatabase.RemoveAll();
}


static const luaL_Reg ConVar_funcs[] = {
  {"ConVar", luasrc_ConVar},
  {"GetConVar_Internal", luasrc_GetConVar_Internal},
  {NULL, NULL}
};


/*
** Open ConVar object
*/
LUALIB_API int luaopen_ConVar (lua_State *L) {
  luaL_newmetatable(L, LUA_CONVARLIBNAME);
  luaL_register(L, NULL, ConVarmeta);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  lua_pushstring(L, "convar");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "convar" */
  luaL_register(L, "_G", ConVar_funcs);
  lua_pop(L, 1);
  return 1;
}

