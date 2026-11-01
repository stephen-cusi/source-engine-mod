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
#include "hl2sb_framestats_cat.h"
#ifdef CLIENT_DLL
#include "ienginevgui.h"
#endif

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

	// HL2SB (2026-09-24): this dispatch can re-enter a state that is already
	// executing Lua (a menu button's DoClick runs an engine command that lands
	// right back here).  The running C frame only reserved LUA_MINSTACK slots,
	// so the six pushes below overflowed ci->top and tripped the api-check
	// trap -- every Lua-driven menu item crashed.  Reserve space explicitly,
	// as the C API contract requires.
	lua_checkstack( LGameUI, 12 );

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

	// HL2SB (2026-09-24): same re-entrancy guard as CC_GameUIConCommand above.
	lua_checkstack( L, 12 );

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

static int HL2SB_CompleteLuaCommand( lua_State *state, const char *partial,
    char commands[ COMMAND_COMPLETION_MAXITEMS ][ COMMAND_COMPLETION_ITEM_LENGTH ] )
{
  if ( !state || !partial )
    return 0;
  lua_checkstack( state, 12 );
  const int top = lua_gettop( state );
  lua_getglobal( state, "concommand" );
  if ( !lua_istable( state, -1 ) )
  {
    lua_settop( state, top );
    return 0;
  }
  lua_getfield( state, -1, "AutoComplete" );
  lua_remove( state, -2 );
  if ( !lua_isfunction( state, -1 ) )
  {
    lua_settop( state, top );
    return 0;
  }

  char name[128];
  Q_strncpy( name, partial, sizeof( name ) );
  char *space = Q_strstr( name, " " );
  if ( space )
    *space = '\0';
  CCommand arguments;
  arguments.Tokenize( partial );
  lua_pushstring( state, name );
  lua_pushstring( state, partial + Q_strlen( name ) );
  lua_pushcommandarguments( state, arguments );
  int count = 0;
  if ( luasrc_pcall( state, 3, 1, 0 ) == 0 && lua_istable( state, -1 ) )
  {
    // This engine's completion buffer uses its own fixed ABI dimensions.
    for ( int i = 1; i <= COMMAND_COMPLETION_MAXITEMS; ++i )
    {
      lua_rawgeti( state, -1, i );
      const char *suggestion = lua_tostring( state, -1 );
      if ( !suggestion )
      {
        lua_pop( state, 1 );
        break;
      }
      Q_strncpy( commands[count++], suggestion, COMMAND_COMPLETION_ITEM_LENGTH );
      lua_pop( state, 1 );
    }
  }
  lua_settop( state, top );
  return count;
}

static int CC_ConCommandCompletion( const char *partial,
    char commands[ COMMAND_COMPLETION_MAXITEMS ][ COMMAND_COMPLETION_ITEM_LENGTH ] )
{
  return HL2SB_CompleteLuaCommand( L, partial, commands );
}

#ifdef CLIENT_DLL
static int CC_GameUIConCommandCompletion( const char *partial,
    char commands[ COMMAND_COMPLETION_MAXITEMS ][ COMMAND_COMPLETION_ITEM_LENGTH ] )
{
  return HL2SB_CompleteLuaCommand( LGameUI, partial, commands );
}
#endif

static int HL2SB_CommandFlags( lua_State *state, int index )
{
  if ( lua_isnoneornil( state, index ) )
    return 0;
  if ( !lua_istable( state, index ) )
    return luaL_checkint( state, index );

  int flags = 0;
  lua_pushnil( state );
  while ( lua_next( state, index ) != 0 )
  {
    flags |= luaL_checkint( state, -1 );
    lua_pop( state, 1 );
  }
  return flags;
}

static int luasrc_ConCommand (lua_State *L) {
  const char *pName = luaL_checkstring(L, 1);
  const char *pHelp = luaL_optstring( L, 2, "" );
  const int requestedFlags = HL2SB_CommandFlags( L, 3 );
#ifdef CLIENT_DLL
  bool bIsGameUI = false;
  unsigned short lookup;
  lua_getglobal(L, "_GAMEUI");
  bIsGameUI = lua_toboolean( L, -1 ) != 0;
  lua_pop( L, 1 );
  if (bIsGameUI) {
    // Complain about duplicately defined ConCommand names...
    lookup = m_GameUIConCommandDatabase.Find( pName );
    if ( lookup != m_GameUIConCommandDatabase.InvalidIndex() )
    {
      // A previous Lua registration owns this name; re-adding just replaces
      // the Lua-side callback, the ConCommand object stays.
      lua_pushconcommand(L, cvar->FindCommand(pName));
      return 1;
    }
    if ( cvar->FindCommand(pName) )
    {
      Warning( "concommand.Add( \"%s\" ) - an engine command already owns this name; the Lua callback will never be executed\n", pName );
      lua_pushconcommand(L, cvar->FindCommand(pName));
      return 1;
    }
  } else {
#endif
    // Complain about duplicately defined ConCommand names...
    unsigned short lookup = m_ConCommandDatabase.Find( pName );
    if ( lookup != m_ConCommandDatabase.InvalidIndex() )
    {
      // A previous Lua registration owns this name; re-adding just replaces
      // the Lua-side callback, the ConCommand object stays.
      lua_pushconcommand(L, cvar->FindCommand(pName));
      return 1;
    }
    // HL2SB: an ENGINE command owns this name.  Console execution runs the
    // engine callback and never reaches the Lua concommand dispatcher, so
    // the Lua callback registered here is dead on arrival - say so instead
    // of failing silently.
    if ( cvar->FindCommand(pName) )
    {
      Warning( "concommand.Add( \"%s\" ) - an engine command already owns this name; the Lua callback will never be executed\n", pName );
      lua_pushconcommand(L, cvar->FindCommand(pName));
      return 1;
    }
#ifdef CLIENT_DLL
  }
#endif
  ConCommand *pConCommand;
#ifdef CLIENT_DLL
  if (bIsGameUI)
    pConCommand = new ConCommand(strdup(pName), CC_GameUIConCommand, strdup(pHelp), requestedFlags, CC_GameUIConCommandCompletion);
  else
    pConCommand = new ConCommand(strdup(pName), CC_ConCommand, strdup(pHelp), requestedFlags | FCVAR_CLIENTDLL | FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE, CC_ConCommandCompletion);
#else
    pConCommand = new ConCommand(strdup(pName), CC_ConCommand, strdup(pHelp), requestedFlags | FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE, CC_ConCommandCompletion);
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

// The original concommand module owns callbacks; native code only registers the name.
static int luasrc_AddConsoleCommand( lua_State *state )
{
  const char *name = luaL_checkstring( state, 1 );
  for ( const char *p = name; *p; ++p )
    if ( *p == ';' || *p < '!' )
      return luaL_error( state, "AddConsoleCommand: Invalid command name! (%s)", name );

  // Use the same native command policy as the command execution entry point.
  const int top = lua_gettop( state );
  lua_getglobal( state, "IsConCommandBlocked" );
  if ( lua_isfunction( state, -1 ) )
  {
    lua_pushstring( state, name );
    lua_call( state, 1, 1 );
    const bool blocked = lua_toboolean( state, -1 ) != 0;
    lua_settop( state, top );
    if ( blocked && Q_stricmp( name, "lua_cookieclear" ) )
    {
      Warning( "AddConsoleCommand: Command name is blocked! (%s)\n", name );
      return 0;
    }
  }
  else
    lua_settop( state, top );

  if ( cvar->FindCommand( name ) )
    return 0;

  luasrc_ConCommand( state );
  return 0;
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
  {"AddConsoleCommand", luasrc_AddConsoleCommand},
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
  HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_CONVAR_READ );
  lua_pushboolean(L, luaL_checkconvar(L, 1)->GetBool());
  return 1;
}

static int ConVar_GetDefault (lua_State *L) {
  lua_pushstring(L, luaL_checkconvar(L, 1)->GetDefault());
  return 1;
}

static int ConVar_GetFloat (lua_State *L) {
  HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_CONVAR_READ );
  lua_pushnumber(L, luaL_checkconvar(L, 1)->GetFloat());
  return 1;
}

static int ConVar_GetHelpText (lua_State *L) {
  lua_pushstring(L, luaL_checkconvar(L, 1)->GetHelpText());
  return 1;
}

static int ConVar_GetInt (lua_State *L) {
  HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_CONVAR_READ );
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
  HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_CONVAR_READ );
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

// HL2SB (2026-09-24): GMod's global IsValid() tests `object.IsValid != nil`
// before calling it -- without this entry GetConVar_Internal's
// IsValid(consoleVariable) check rejected EVERY convar and GetConVar()
// returned nil for all of them (halo_draw/halo_debug invisible, and every
// script that trusted GetConVar silently ran on nil).
static int ConVar_IsValid (lua_State *L) {
  lua_pushboolean(L, 1);
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
  {"IsValid", ConVar_IsValid},
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
#ifdef CLIENT_DLL
static CUtlDict< ConVar*, unsigned short > m_GameUIConVarDatabase;

// A menu control may retain a variable across every gameplay state.
static void HL2SB_RetainMenuConVar( lua_State *state, const char *name )
{
    if ( state != LGameUI )
        return;
    unsigned short index = m_ConVarDatabase.Find( name );
    if ( index != m_ConVarDatabase.InvalidIndex() )
    {
        m_GameUIConVarDatabase.Insert( name, m_ConVarDatabase[index] );
        m_ConVarDatabase.RemoveAt( index );
    }
}
#endif

// HL2SB (2026-10-03): name -> ConVar* lookup accelerator for the read form
// of ConVar(name) / GetConVar_Internal.  cvar->FindVar walks the whole
// registered-command list with one Q_stricmp per entry, so an addon asking
// ConVarExists("some_other_addons_var") every frame repeated that O(n) scan
// each frame.  A NULL entry is a cached miss (the name was not found in the
// engine list); a hit is the engine- or Lua-owned pointer - the cache owns
// neither the names' meaning nor the convars, so ResetConVarDatabase just
// drops it.  Staleness is bounded by what can register a convar AFTER a
// cached miss: the create path below refreshes the cache entry, and C-side
// static ConVars register at DLL load, before any Lua state exists.  A lazy
// function-local static ConVar registered later would stay "missing" until
// map change - accepted, that pattern does not exist in this tree.
static CUtlDict< ConVar*, unsigned short > m_ConVarLookupCache;

static int luasrc_ConVar (lua_State *L) {
  const char *pName = luaL_checkstring(L, 1);
#ifdef CLIENT_DLL
  HL2SB_RetainMenuConVar( L, pName );
#endif

  // HL2SB: GMod's global ConVar( name ) is a LOOKUP when called with one
  // argument.  ConVarExists() (extensions/gmod_globals.lua) builds on exactly
  // that and cod_c4 gates every CreateConVar on ConVarExists -- with the old
  // unconditional luaL_checkstring(L, 2) the single-argument call raised
  // "bad argument #2 to 'ConVar' (string expected, got no value)", the whole
  // cod-c4 shared.lua failed to load, and the C4_* convars, the
  // "C4_Convars_Change" net string and the client callbacks never existed.
  if ( lua_gettop( L ) < 2 )
  {
    HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_CONVAR_LOOKUP );

    unsigned short cached = m_ConVarLookupCache.Find( pName );
    if ( cached != m_ConVarLookupCache.InvalidIndex() )
    {
      if ( m_ConVarLookupCache[ cached ] )
      {
        lua_pushconvar( L, m_ConVarLookupCache[ cached ] );
        return 1;
      }
      lua_pushnil( L );
      return 1;
    }

    unsigned short existing = m_ConVarDatabase.Find( pName );
    ConVar *pFound = ( existing != m_ConVarDatabase.InvalidIndex() )
      ? m_ConVarDatabase[ existing ]
      : cvar->FindVar( pName );

    // Cache both outcomes: the pointer, or the miss (NULL) so the next
    // per-frame lookup is a dict search instead of a full command-list scan.
    m_ConVarLookupCache.Insert( pName, pFound );

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

  const int luaClientFlag = 1 << 26;
  const int luaServerFlag = 1 << 27;
  int flags = luaL_optint( L, 3, 0 );
  if ( !( flags & FCVAR_DONTRECORD ) )
    flags |= FCVAR_DEMO;
#ifdef CLIENT_DLL
  if ( flags & FCVAR_REPLICATED )
    flags &= ~FCVAR_ARCHIVE;
#endif
  flags &= ~( FCVAR_CLIENTDLL | FCVAR_GAMEDLL | luaClientFlag | luaServerFlag );
#ifdef CLIENT_DLL
  if ( L == LGameUI )
    flags &= ~FCVAR_ARCHIVE;
  else
    flags |= FCVAR_CLIENTDLL | FCVAR_SERVER_CAN_EXECUTE | luaClientFlag;
#else
  flags |= FCVAR_GAMEDLL | luaServerFlag;
#endif
  const char *pHelp = luaL_optstring(L, 4, NULL);
  ConVar *pConVar = new ConVar(strdup(pName), pDefault, flags, pHelp ? strdup(pHelp) : NULL, luaL_optboolean(L, 5, 0), luaL_optnumber(L, 6, 0.0), luaL_optboolean(L, 7, 0), luaL_optnumber(L, 8, 0));

  // HL2SB (2026-09-24): register with the engine cvar system.  GMod's
  // Lua-created convars ARE console-visible/queryable ("halo_draw" etc.);
  // without this they were orphaned -- usable only through the very Lua
  // object that created them, Unknown command from the console, and
  // GetConVar_Internal's cvar->FindVar fallback could never see them.
  cvar->RegisterConCommand( pConVar );

  // HL2SB (2026-10-03): a ConVarExists() before this create cached a miss
  // for the name - refresh it so later lookups see the new variable.
  unsigned short cachedEntry = m_ConVarLookupCache.Find( pName );
  if ( cachedEntry != m_ConVarLookupCache.InvalidIndex() )
    m_ConVarLookupCache[ cachedEntry ] = pConVar;

#ifdef CLIENT_DLL
  if ( L == LGameUI )
    lookup = m_GameUIConVarDatabase.Insert( pName, pConVar );
  else
#endif
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
#ifdef CLIENT_DLL
  HL2SB_RetainMenuConVar( L, pName );
#endif

  HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_CONVAR_LOOKUP );

  unsigned short cached = m_ConVarLookupCache.Find( pName );
  if ( cached != m_ConVarLookupCache.InvalidIndex() )
  {
    if ( m_ConVarLookupCache[ cached ] )
    {
      lua_pushconvar(L, m_ConVarLookupCache[ cached ] );
      return 1;
    }
    lua_pushnil(L);
    return 1;
  }

  unsigned short lookup = m_ConVarDatabase.Find( pName );
  if ( lookup != m_ConVarDatabase.InvalidIndex() )
  {
    m_ConVarLookupCache.Insert( pName, m_ConVarDatabase[ lookup ] );
    lua_pushconvar(L, m_ConVarDatabase[ lookup ] );
    return 1;
  }

  ConVar *pConVar = cvar->FindVar( pName );
  if ( !pConVar )
  {
    m_ConVarLookupCache.Insert( pName, NULL );
    lua_pushnil(L);
    return 1;
  }

  m_ConVarLookupCache.Insert( pName, pConVar );
  lua_pushconvar(L, pConVar);
  return 1;
}

void ResetConVarDatabase( void )
{
	m_ConVarLookupCache.RemoveAll();  // owns nothing
	for ( int i=m_ConVarDatabase.First(); i != m_ConVarDatabase.InvalidIndex(); i=m_ConVarDatabase.Next( i ) )
	{
		ConVar *pConVar = m_ConVarDatabase[ i ];
		cvar->UnregisterConCommand(pConVar);
		delete pConVar;
	}
	m_ConVarDatabase.RemoveAll();
}


#ifdef CLIENT_DLL
void ResetGameUIConVarDatabase( void )
{
    m_ConVarLookupCache.RemoveAll();
    for ( int i = m_GameUIConVarDatabase.First(); i != m_GameUIConVarDatabase.InvalidIndex();
          i = m_GameUIConVarDatabase.Next( i ) )
    {
        ConVar *variable = m_GameUIConVarDatabase[i];
        if ( ::L != NULL )
        {
            // A menu reload must preserve values still referenced by gameplay.
            m_ConVarDatabase.Insert( m_GameUIConVarDatabase.GetElementName( i ), variable );
        }
        else
        {
            cvar->UnregisterConCommand( variable );
            delete variable;
        }
    }
    m_GameUIConVarDatabase.RemoveAll();
}
#endif

extern int lua_IsConCommandBlocked (lua_State *L);

static int HL2SB_ConVarFlagValue (lua_State *L, int index) {
  const float value = (float)lua_tonumber(L, index);
  return value >= -2147483648.0 && value < 2147483648.0 ? (int)value : (-2147483647 - 1);
}

static int luasrc_CreateConVar (lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  static const char *const blocked[] = {
    "rcon_password", "sv_password", "password", "tv_password",
    "tv_relaypassword", "rcon_address", "lua_error_url"
  };
  for ( int i = 0; i < ARRAYSIZE(blocked); ++i ) {
    if ( !Q_stricmp(name, blocked[i]) )
      return luaL_error(L, "CreateConVar: ConVar name is blocked! (%s)", name);
  }

#ifdef CLIENT_DLL
  if ( enginevgui && enginevgui->IsGameUIVisible() &&
       (!Q_stricmp(name, "screenshot") || !Q_stricmp(name, "jpeg")) )
    return luaL_error(L, "CreateConVar: ConVar name is blocked! (%s)", name);
#endif

  static const char *const additionalBlocked[] = {
    "envmap"
#ifdef CLIENT_DLL
    , "_restart", "map", "map_background", "map_edit", "maxplayers", "tv_record"
#endif
  };
  for ( int i = 0; i < ARRAYSIZE(additionalBlocked); ++i ) {
    if ( !Q_stricmp(name, additionalBlocked[i]) )
      return luaL_error(L, "CreateConVar: ConVar name is blocked! (%s)", name);
  }

  lua_pushcfunction(L, lua_IsConCommandBlocked);
  lua_pushvalue(L, 1);
  lua_call(L, 1, 1);
  const bool commandBlocked = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if ( commandBlocked )
    return luaL_error(L, "CreateConVar: ConVar name is blocked! (%s)", name);

  if ( cvar->FindCommand(name) )
    return luaL_error(L, "CreateConVar: Cannot override an existing console command! (%s)", name);
  if ( Q_strlen(name) < 2 )
    return luaL_error(L, "CreateConVar: ConVar name is too short! (%s)", name);
  for ( const unsigned char *p = (const unsigned char *)name; *p; ++p ) {
    const unsigned int c = *p;
    if ( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || strchr("!+-.^_~", c)) )
      return luaL_error(L, "CreateConVar: Invalid ConVar name! (%s)", name);
  }

  ConVar *existing = cvar->FindVar(name);
  if ( existing ) {
    const int luaOwned = (1 << 26) | (1 << 27);
    if ( existing->IsFlagSet(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY | FCVAR_UNREGISTERED) &&
         !existing->IsFlagSet(luaOwned) )
      return 0;
    lua_pushconvar(L, existing);
    return 1;
  }

  if ( !lua_isnoneornil(L, 4) && lua_type(L, 4) != LUA_TSTRING )
    return luaL_argerror(L, 4, "string expected");
  if ( !lua_isnoneornil(L, 5) && lua_type(L, 5) != LUA_TNUMBER )
    return luaL_argerror(L, 5, "number expected");
  if ( !lua_isnoneornil(L, 6) && lua_type(L, 6) != LUA_TNUMBER )
    return luaL_argerror(L, 6, "number expected");

  int flags = 0;
  if ( lua_istable(L, 3) ) {
    for ( lua_Integer i = 1; ; ++i ) {
      lua_geti(L, 3, i);
      if ( lua_isnil(L, -1) ) {
        lua_pop(L, 1);
        break;
      }
      flags |= HL2SB_ConVarFlagValue(L, -1);
      lua_pop(L, 1);
    }
  } else if ( !lua_isnoneornil(L, 3) )
    flags = HL2SB_ConVarFlagValue(L, 3);

  lua_settop(L, 6);
  lua_pushcfunction(L, luasrc_ConVar);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_pushinteger(L, flags);
  lua_pushvalue(L, 4);
  lua_pushboolean(L, !lua_isnil(L, 5));
  if ( lua_isnil(L, 5) ) lua_pushnumber(L, 0);
  else lua_pushnumber(L, luaL_checknumber(L, 5));
  lua_pushboolean(L, !lua_isnil(L, 6));
  if ( lua_isnil(L, 6) ) lua_pushnumber(L, 0);
  else lua_pushnumber(L, luaL_checknumber(L, 6));
  lua_call(L, 8, 1);
  return 1;
}

static const luaL_Reg ConVar_funcs[] = {
  {"ConVar", luasrc_ConVar},
  {"CreateConVar", luasrc_CreateConVar},
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

