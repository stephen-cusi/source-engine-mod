//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ===========//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//


#define lsrcinit_cpp

#include "cbase.h"
#include "lua.hpp"

#include "luasrclib.h"
#include "lauxlib.h"
#include "lsql.h"
#include "activitylist.h"

// HL2SB: game.AddParticles() below drives the particle system manager directly.
#include "particles/particles.h"
#include "filesystem.h"
#include "tier1/CommandBuffer.h"
#include "tier0/fasttimer.h"
#include "tier1/utlstring.h"
// HL2SB: luaL_checkentity (SuppressHostEvents), the GMod global helpers
// registered at the end, the NetworkVar shim (which hands a 0..1 Vector back for
// a Vector-declared variable, a 0..255 Color otherwise) and the vector/color
// accessors it and sent_ball need.
#include "luamanager.h"

// HL2SB GMod compat (2026-09-24): convars GMod (or older Source) always has
// that this fork never registered.  First Person Body reads gmod_language at
// LOAD time (nil ConVar aborted the whole addon file), and cl_drawownshadow in
// its shadow logic (GetBool(nil) aborted the body update each frame).  The
// gmod_compatibility suite that was meant to carry gmod_language stays
// switched off (GMOD_COMPATIBILITY=false), and creating them from Lua in
// gmod_globals.lua ran before the engine "ConVar" global exists (it only
// crashed the menu realm) - so register them engine-side instead: DLL load,
// both realms, ahead of every Lua.
static ConVar gmod_language("gmod_language", "en", FCVAR_ARCHIVE, "Language to use in GMod");
static ConVar cl_drawownshadow("cl_drawownshadow", "0", FCVAR_ARCHIVE, "Render the local player's shadow");
#include "lbaseentity_shared.h"
// HL2SB: lua_toanimating() for the NetworkVar shim's m_nSkin carrier.  Each
// realm has its own twin (game/*/lua/l[ c_ ]baseanimating.h); its
// dynamic_cast is the real-RTTI cast that keeps a CBaseEntity from being
// reinterpreted as an animating one.  Same pair lbaseentity_shared.cpp uses.
#ifdef CLIENT_DLL
#include "lc_baseanimating.h"
#else
#include "lbaseanimating.h"
#endif
#include <lColor.h>
#include <mathlib/lvector.h>
#include "ipredictionsystem.h"
#ifdef CLIENT_DLL
#include "prediction.h"			// CPrediction *prediction, for IsFirstTimePredicted
#endif
#ifndef CLIENT_DLL
// HL2SB: luaopen_ServerAIEnums below -- the server AI enum globals
// (HULL_* / SCHED_*) that GMod publishes and scripted SNPCs (npc_scp173) use.
#include "ai_default.h"			// SCHED_* enum
#include "ai_hull.h"			// Hull_t (HULL_*)
#include "util.h"				// HL2SB_NWResyncClient: UTIL_EntityByIndex
// HL2SB (mc_swep audit B2): resource.AddFile queues client downloads by
// writing into the engine's "downloadables" string table.  Relative includes
// on purpose: the engine headers are not on this project's include path, and
// a luasrclib.h touch would force a full-tree rebuild.
#include "../../../engine/networkstringtable.h"
#endif
// HL2SB (2026-09-21): Lua NetworkVar replication -- the shim's storage below
// is per-realm and local-only, so a server-side NetworkVar write (the Nuke
// Pack's BallColor random roll) never reached the client and every self-drawn
// entity showed its default color.  The setter broadcasts a user message, the
// client handler stores the payload, and the getter falls back to it.
#include "usermessages.h"
#ifdef CLIENT_DLL
#include "c_user_message_register.h"	// USER_MESSAGE_REGISTER (client hook)
#include "cliententitylist.h"			// SWEPCmd: entity index -> C_BaseEntity
#endif

// HL2SB (2026-10-02): game.GetAmmoName reads the shared ammo def.
#include "ammodef.h"
#include <string>
#include <vector>
#include <utility>
#include "tier1/KeyValues.h"
#if !defined(NO_STEAM) && !defined(DISABLE_STEAM)
#include "steam/steam_api.h"
extern CSteamAPIContext *steamapicontext;
#endif

void HL2SB_AddCustomDecal( const char *name, const char *const *materials, int count );

struct HL2SB_GameDepot {
  const char *folder;
  const char *title;
  int appID;
  const char *extraFolders;
};

static const HL2SB_GameDepot s_GameDepots[] = {
  { "hl2", "Half-Life 2 & Episodes", 220, "episodic;ep2;lostcoast" },
  { "cstrike", "Counter-Strike: Source", 240, "" },
  { "tf", "Team Fortress 2", 440, "" },
  { "dod", "Day of Defeat: Source", 300, "" },
  { "hl2mp", "Half-Life 2: Deathmatch", 320, "" },
  { "hl1", "Half-Life: Source", 280, "" },
  { "hl1mp", "Half-Life Deathmatch: Source", 360, "hl1" },
  { "zeno_clash", "Zeno Clash (Model Pack)", 22208, "" },
  { "portal", "Portal", 400, "" },
  { "diprip", "D.I.P.R.I.P.", 17530, "" },
  { "zps", "Zombie Panic! Source", 17500, "vpks" },
  { "pvkii", "Pirates, Vikings and Knights II", 17570, "" },
  { "dystopia", "Dystopia", 17580, "" },
  { "insurgency", "Insurgency (Source Mod)", 17700, "" },
  { "insurgency", "Insurgency (Standalone)", 222880, "" },
  { "ageofchivalry", "Age of Chivalry", 17510, "" },
  { "left4dead2", "Left 4 Dead 2", 550, "left4dead2_dlc1;left4dead2_dlc2;left4dead2_dlc3;update" },
  { "left4dead", "Left 4 Dead", 500, "left4dead_dlc3" },
  { "portal2", "Portal 2", 620, "portal2_dlc1;portal2_dlc2" },
  { "swarm", "Alien Swarm", 630, "" },
  { "nucleardawn", "Nuclear Dawn", 17710, "" },
  { "dinodday", "Dino D-Day", 70000, "" },
  { "csgo", "Counter-Strike: GO", 4465480, "" },
  { "berimbau", "Blade Symphony", 225600, "" },
  { "infra", "INFRA", 251110, "" },
  { "fof", "Fistful of Frags", 265630, "" },
  { "thestanleyparable", "The Stanley Parable", 221910, "" },
  { "gstringv2", "G String Game", 1224600, "" },
  { "treason", "Klaus Veen's Treason", 1786950, "" },
  { "vietnam", "Military Conflict: Vietnam", 1012110, "gmod" },
  { "bms", "Black Mesa", 362890, "" },
};

static std::vector<std::string> HL2SB_GameSearchPaths( const char *pathID ) {
  const int size = filesystem->GetSearchPath( pathID, true, NULL, 0 );
  std::vector<char> buffer( size > 0 ? size : 1, 0 );
  filesystem->GetSearchPath( pathID, true, &buffer[0], (int)buffer.size() );
  V_FixSlashes( &buffer[0], '/' );
  std::vector<std::string> paths;
  const char *position = &buffer[0];
  while ( *position ) {
    const char *end = strchr( position, ';' );
    if ( !end ) end = position + strlen( position );
    std::string path( position, end - position );
    while ( !path.empty() && path[path.size() - 1] == '/' ) path.resize( path.size() - 1 );
    if ( !path.empty() ) paths.push_back( path );
    position = *end ? end + 1 : end;
  }
  return paths;
}

static bool HL2SB_GameFolderMatches( const HL2SB_GameDepot &game, const char *folder ) {
  if ( !Q_stricmp( folder, game.folder ) ) return true;
  const char *position = game.extraFolders;
  while ( *position ) {
    const char *end = strchr( position, ';' );
    if ( !end ) end = position + strlen( position );
    const std::string extra( position, end - position );
    if ( !Q_stricmp( folder, extra.c_str() ) ) return true;
    position = *end ? end + 1 : end;
  }
  return false;
}

static bool HL2SB_IsDepotSearchPath( const HL2SB_GameDepot &game, const std::string &path ) {
  // Addon and download directory names do not identify mounted games.
  if ( Q_stristr( path.c_str(), "/addons/" ) || Q_stristr( path.c_str(), "/download/" ) ||
       Q_stristr( path.c_str(), "/workshop/" ) ) return false;
  const size_t slash = path.find_last_of( '/' );
  const std::string name = path.substr( slash == std::string::npos ? 0 : slash + 1 );
  const char *extension = V_GetFileExtension( name.c_str() );
  const bool packed = extension && !Q_stricmp( extension, "vpk" );
  const std::string directory = packed ? path.substr( 0, slash ) : path;
  const size_t parentSlash = directory.find_last_of( '/' );
  const std::string folder = directory.substr( parentSlash == std::string::npos ? 0 : parentSlash + 1 );
  if ( packed && !Q_stricmp( folder.c_str(), "sourceengine" ) ) {
    const std::string pack = std::string( "content_" ) + game.folder;
    return !Q_stricmp( name.c_str(), (pack + ".vpk").c_str() ) ||
           !Q_stricmp( name.c_str(), (pack + "_dir.vpk").c_str() );
  }
  if ( !HL2SB_GameFolderMatches( game, folder.c_str() ) ) return false;
  const std::string info = !Q_stricmp(folder.c_str(), game.folder) ?
    directory + "/gameinfo.txt" : directory.substr(0, parentSlash) + "/" + game.folder + "/gameinfo.txt";
  if ( !filesystem->FileExists( info.c_str(), NULL ) ) return false;
  // The two Insurgency releases use the same content folder.
  if ( !Q_stricmp( game.folder, "insurgency" ) ) {
    KeyValues *settings = new KeyValues( "GameInfo" );
    const bool loaded = settings->LoadFromFile( filesystem, info.c_str(), NULL );
    const int appID = settings->GetInt( "FileSystem/SteamAppId", 0 );
    settings->deleteThis();
    if ( !loaded || appID != game.appID ) return false;
  }
  return true;
}

// Register browsing aliases when mounts change, never from a Lua query.
void HL2SB_RefreshGameContentPaths() {
  typedef std::pair<std::string, std::string> GamePath;
  static std::vector<GamePath> aliases;
  const std::vector<std::string> paths = HL2SB_GameSearchPaths( "GAME" );
  std::vector<GamePath> next;
  for ( int i = 0; i < ARRAYSIZE(s_GameDepots); ++i ) {
    for ( size_t j = 0; j < paths.size(); ++j ) {
      if ( HL2SB_IsDepotSearchPath( s_GameDepots[i], paths[j] ) ) {
        const GamePath entry( paths[j], s_GameDepots[i].folder );
        bool duplicate = false;
        for ( size_t k = 0; k < next.size(); ++k ) if ( next[k] == entry ) duplicate = true;
        if ( !duplicate ) next.push_back( entry );
      }
    }
  }
  if ( next == aliases ) return;
  for ( size_t i = 0; i < aliases.size(); ++i )
    filesystem->RemoveSearchPath( aliases[i].first.c_str(), aliases[i].second.c_str() );
  for ( size_t i = 0; i < next.size(); ++i ) {
    filesystem->AddSearchPath( next[i].first.c_str(), next[i].second.c_str(), PATH_ADD_TO_TAIL );
    filesystem->MarkPathIDByRequestOnly( next[i].second.c_str(), true );
  }
  aliases.swap( next );
}

int HL2SB_EngineGetGames( lua_State *L ) {
  const std::vector<std::string> paths = HL2SB_GameSearchPaths( "GAME" );
  const std::vector<std::string> bases = HL2SB_GameSearchPaths( "BASE_PATH" );
  lua_newtable( L );
  for ( int i = 0; i < ARRAYSIZE(s_GameDepots); ++i ) {
    const HL2SB_GameDepot &game = s_GameDepots[i];
    bool mounted = false, installed = false, owned = false;
    for ( size_t j = 0; j < paths.size(); ++j ) {
      if ( HL2SB_IsDepotSearchPath( game, paths[j] ) ) mounted = true;
    }
    // Local installations can exist without being in the active search paths.
    for ( size_t j = 0; j < bases.size(); ++j ) {
      const std::string directory = bases[j] + "/" + game.folder;
      if ( HL2SB_IsDepotSearchPath( game, directory ) ) installed = true;
    }
    for ( size_t j = 0; j < paths.size(); ++j ) {
      if ( HL2SB_IsDepotSearchPath( game, paths[j] ) ) {
        const char *extension = V_GetFileExtension( paths[j].c_str() );
        const std::string directory = extension && !Q_stricmp(extension, "vpk") ?
          paths[j].substr(0, paths[j].find_last_of('/')) : paths[j];
        const size_t slash = directory.find_last_of('/');
        const std::string folder = directory.substr(slash == std::string::npos ? 0 : slash + 1);
        const std::string info = !Q_stricmp(folder.c_str(), game.folder) ? directory + "/gameinfo.txt" :
          directory.substr(0, slash) + "/" + game.folder + "/gameinfo.txt";
        if ( filesystem->FileExists( info.c_str(), NULL ) ) installed = true;
      }
    }
#if !defined(NO_STEAM) && !defined(DISABLE_STEAM)
    if ( steamapicontext && steamapicontext->SteamApps() ) {
      ISteamApps *apps = steamapicontext->SteamApps();
      char directory[MAX_PATH] = { 0 };
      owned = apps->BIsSubscribedApp( game.appID );
      installed = owned && apps->BIsAppInstalled( game.appID ) &&
        apps->GetAppInstallDir( game.appID, directory, sizeof(directory) ) != 0;
      mounted = mounted && owned && installed;
    }
#endif
    lua_newtable( L );
    lua_pushstring(L, game.folder); lua_setfield(L, -2, "folder");
    lua_pushstring(L, game.title); lua_setfield(L, -2, "title");
    lua_pushinteger(L, game.appID); lua_setfield(L, -2, "depot");
    lua_pushboolean(L, mounted); lua_setfield(L, -2, "mounted");
    lua_pushboolean(L, installed); lua_setfield(L, -2, "installed");
    lua_pushboolean(L, owned); lua_setfield(L, -2, "owned");
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

static int lua_game_GetWorld( lua_State *state )
{
#ifdef CLIENT_DLL
  lua_pushentity(state, cl_entitylist->GetEnt(0));
#else
  lua_pushentity(state, CBaseEntity::Instance(0));
#endif
  return 1;
}

static int lua_game_AddDecal( lua_State *state )
{
  const char *name = luaL_checkstring( state, 1 );
  if ( lua_istable( state, 2 ) )
  {
    lua_pushnil( state );
    while ( lua_next( state, 2 ) )
    {
      if ( !lua_isstring( state, -1 ) )
        return luaL_error( state, "decal materials must be strings" );
      lua_pop( state, 1 );
    }
  }
  else
  {
    luaL_checkstring( state, 2 );
  }
  std::vector<std::string> materials;
  if ( lua_istable( state, 2 ) )
  {
    lua_pushnil( state );
    while ( lua_next( state, 2 ) )
    {
      materials.push_back( lua_tostring( state, -1 ) );
      lua_pop( state, 1 );
    }
  }
  else
  {
    materials.push_back( luaL_checkstring( state, 2 ) );
  }

  std::vector<const char *> paths;
  for ( size_t i = 0; i < materials.size(); ++i )
    paths.push_back( materials[i].c_str() );
  if ( !paths.empty() )
    HL2SB_AddCustomDecal( name, &paths[0], (int)paths.size() );
  return 0;
}


// HL2SB: local prototype -- deliberately NOT added to luasrclib.h: a header
// touch there would force a full-tree rebuild (waf has no header dependency
// propagation), and only lsrcinit.cpp needs to see this.
LUALIB_API int (luaopen_CSEmitter) (lua_State *L);
#ifdef CLIENT_DLL
// HL2SB: GMod's .pcf engine-particle surface (game/client/lua/lnewparticle.cpp).
LUALIB_API int (luaopen_CNewParticleEffect) (lua_State *L);
// HL2SB: the C half of GMod's effects library (game/client/lua/leffects.cpp) --
// Bubbles / BubbleTrail / BeamRingPoint.  Same local-prototype rule as
// luaopen_CSEmitter above: a luasrclib.h touch would force a full-tree rebuild.
LUALIB_API int (luaopen_HL2SBClientEffects) (lua_State *L);
#else
// HL2SB: server AI enum globals (bottom of this file).
LUALIB_API int luaopen_ServerAIEnums( lua_State *L );
#endif
// HL2SB: the engine timer library (game/shared/lua/ltimer.cpp), both realms.
LUALIB_API int luaopen_timer ( lua_State *L );
// HL2SB: GMod's NPC / NextBot Lua metatables (game/shared/lua/lnpc_shared.cpp).
// Same local-prototype rule as luaopen_CSEmitter above -- only this file needs
// to see them, so luasrclib.h stays untouched.
LUALIB_API int (luaopen_NPC_shared) (lua_State *L);
LUALIB_API int (luaopen_NextBot_shared) (lua_State *L);

#ifndef CLIENT_DLL
// HL2SB (mc_swep audit B2): GMod's server-only `resource` library --
// resource.AddFile / resource.AddSingleFile queue a file for client download
// by writing its (backslash-normalised) path into the engine's
// "downloadables" network string table, which connecting clients already
// watch.  The reference also skips the call entirely on single-player
// servers: with no second client there is nobody to download anything.
// Local to this file on purpose (no header touch, no .vpc edit) -- the
// minecraft wall-sign's init.lua calls resource.AddFile at load time and the
// missing global aborted the whole script, taking the SENT down with it.
// the engine string-table container; defined in game/server/gameinterface.cpp
extern INetworkStringTableContainer *networkstringtable;

static void HL2SB_ResourceQueueFile (INetworkStringTable *pTable, const char *pszPath)
{
	for ( int i = 0; i < pTable->GetNumStrings(); i++ )
	{
		if ( Q_stricmp( pTable->GetString( i ), pszPath ) == 0 )
			return;
	}

	if ( !g_pFullFileSystem->FileExists( pszPath ) )
	{
		Warning( "resource.AddFile: file %s does not exist, not queued\n", pszPath );
		return;
	}

	pTable->AddString( true, pszPath, -1 );
}

// A model download is useless without its compiled siblings -- a client that
// only got the .mdl cannot load it.  Queue every standard studio-family
// extension that exists next to it (the reference errors per missing file;
// warning once here instead, some packs ship without .phy/.ani on purpose).
static void HL2SB_ResourceQueueModelFamily (INetworkStringTable *pTable, const char *pszModelPath)
{
	static const char *const s_pSiblings[] = {
		"vvd", "ani", "dx80.vtx", "dx90.vtx", "sw.vtx", "phy"
	};

	const char *pszDot = strrchr( pszModelPath, '.' );
	if ( pszDot == NULL )
		return;

	char szPath[260];
	for ( int i = 0; i < ARRAYSIZE( s_pSiblings ); i++ )
	{
		Q_snprintf( szPath, sizeof( szPath ), "%.*s.%s", (int)( pszDot - pszModelPath ), pszModelPath, s_pSiblings[ i ] );
		HL2SB_ResourceQueueFile( pTable, szPath );
	}
}

static int luasrc_resource_AddFile (lua_State *L)
{
	const char *pszPath = luaL_checkstring( L, 1 );

	if ( pszPath == NULL || pszPath[0] == '\0' || gpGlobals->maxClients <= 1 )
		return 0;

	INetworkStringTable *pTable = networkstringtable->FindTable( "downloadables" );
	if ( pTable == NULL )
		return 0;

	char szPath[260];
	Q_strncpy( szPath, pszPath, sizeof( szPath ) );
	Q_FixSlashes( szPath );

	HL2SB_ResourceQueueFile( pTable, szPath );

	// the reference pulls in a model's compiled siblings with AddFile;
	// AddSingleFile opts out of that expansion.
	if ( V_stristr( szPath, ".mdl" ) != NULL )
	{
		HL2SB_ResourceQueueModelFamily( pTable, szPath );
	}

	return 0;
}

// GMod's folder-skipping variant: queue exactly the named file, no model
// sibling expansion.  The second internal argument above stays unused here.
static int luasrc_resource_AddSingleFile (lua_State *L)
{
	const char *pszPath = luaL_checkstring( L, 1 );

	if ( pszPath == NULL || pszPath[0] == '\0' || gpGlobals->maxClients <= 1 )
		return 0;

	INetworkStringTable *pTable = networkstringtable->FindTable( "downloadables" );
	if ( pTable == NULL )
		return 0;

	char szPath[260];
	Q_strncpy( szPath, pszPath, sizeof( szPath ) );
	Q_FixSlashes( szPath );

	HL2SB_ResourceQueueFile( pTable, szPath );
	return 0;
}

// GMod's workshop variant: reference just queues "<id>.gma" into the same
// downloadables table (multiplayer gate, and ids shorter than 3 characters
// raise "Invalid input to resource.AddWorkshop").  Whether the receiving
// client can mount the archive afterwards is that client's business -- here
// it matters for local-listen play only, where the workshop copy is already
// mounted from disk.
static int luasrc_resource_AddWorkshop (lua_State *L)
{
	const char *pszId = luaL_checkstring( L, 1 );

	if ( pszId == NULL || gpGlobals->maxClients <= 1 )
		return 0;

	if ( strlen( pszId ) < 3 )
		luaL_error( L, "Invalid input to resource.AddWorkshop" );

	INetworkStringTable *pTable = networkstringtable->FindTable( "downloadables" );
	if ( pTable == NULL )
		return 0;

	char szPath[260];
	Q_snprintf( szPath, sizeof( szPath ), "%s.gma", pszId );

	for ( int i = 0; i < pTable->GetNumStrings(); i++ )
	{
		if ( Q_stricmp( pTable->GetString( i ), szPath ) == 0 )
			return 0;
	}

	pTable->AddString( true, szPath, -1 );
	return 0;
}

static const luaL_Reg luasrc_resourcelib[] = {
	{ "AddFile", luasrc_resource_AddFile },
	{ "AddSingleFile", luasrc_resource_AddSingleFile },
	{ "AddWorkshop", luasrc_resource_AddWorkshop },
	{ NULL, NULL }
};

LUALIB_API int luaopen_resource (lua_State *L)
{
	luaL_register( L, "resource", luasrc_resourcelib );
	return 1;
}
#endif

static const luaL_Reg luasrclibs[] = {
  // HL2SB: ported from Experiment: Source.  Fills _E with the shared enums.
  {LUA_SHAREDENUMNAME, luaopen_SharedEnumerations},
  // HL2SB: ported from Experiment: Source.  The rest of the _E tables.  They are
  // opened right after luaopen_SharedEnumerations because each one does
  // lua_getglobal("_E") + lua_setfield, and Experiment's gmod_compatibility shim
  // reads them -- sh_enumerations.lua error()s on a missing key.  luaopen_ACTIVITY
  // additionally takes ownership of the activity list away from the world entity
  // (see the LUA_SDK branch of REGISTER_SHARED_ACTIVITY in activitylist.h).
  {LUA_ACTIVITYENUMNAME, luaopen_ACTIVITY},
  {LUA_BUTTONENUMNAME, luaopen_BUTTON},
  {LUA_EFLIBNAME, luaopen_EF},
  {LUA_ENGINEFLAGSENUMLIBNAME, luaopen_FL},
  {LUA_FLEDICTLIBNAME, luaopen_FL_EDICT},
  {LUA_GESTURESLOTLIBNAME, luaopen_GESTURE_SLOT},
  {LUA_LIFELIBNAME, luaopen_LIFE},
  {LUA_MOVECOLLIDELIBNAME, luaopen_MOVECOLLIDE},
  {LUA_MOVETYPELIBNAME, luaopen_MOVETYPE},
  {LUA_OBSMODELIBNAME, luaopen_OBS_MODE},
  {LUA_SOLIDFLAGLIBNAME, luaopen_SOLIDFLAG},
  {LUA_SOLIDLIBNAME, luaopen_SOLID},
  {LUA_BASEANIMATINGLIBNAME, luaopen_CBaseAnimating},
  {LUA_BASEANIMATINGLIBNAME, luaopen_CBaseAnimating_shared},
  {LUA_BASECOMBATWEAPONLIBNAME, luaopen_CBaseCombatWeapon},
  {LUA_BASEENTITYLIBNAME, luaopen_CBaseEntity},
  {LUA_BASEENTITYLIBNAME, luaopen_CBaseEntity_shared},
  // HL2SB: ported from Experiment: Source.  Registered after luaopen_Entities so it
  // merges its Entities.CreateClientEntity into the same `ents` table.
  {LUA_CBASEFLEXLIBNAME, luaopen_CBaseFlex_shared},
  {LUA_BASEPLAYERLIBNAME, luaopen_CBasePlayer},
  {LUA_BASEPLAYERLIBNAME, luaopen_CBasePlayer_shared},
  {LUA_EFFECTDATALIBNAME, luaopen_CEffectData},
  {LUA_GAMETRACELIBNAME, luaopen_CGameTrace},
#ifndef CLIENT_DLL
  {LUA_EFFECTSLIBNAME, luaopen_Effects},
  {LUA_HL2MPPLAYERLIBNAME, luaopen_CHL2MP_Player},
  {"ServerAIEnums", luaopen_ServerAIEnums},
#endif
  // HL2SB: the GMod timer library, engine-side (game/shared/lua/ltimer.cpp),
  // pumped per frame from CHL2MPRules::Think / the scripted viewport paint.
  {"timer", luaopen_timer},
  {LUA_HL2MPPLAYERLIBNAME, luaopen_CHL2MP_Player_shared},
  // HL2SB: ported from Garry's Mod.  Registers the "Vehicle" metatable
  // (game/shared/lua/lvehicle_shared.cpp) that lua_pushentity() installs for a
  // drivable vehicle.  One library for both realms; the realm split lives inside
  // its methods.
  {LUA_VEHICLELIBNAME, luaopen_Vehicle_shared},
  // HL2SB: ported from Garry's Mod.  Register the "NPC" / "NextBot" metatables
  // that lua_pushentity() installs for NPCs and bots.  One library per realm;
  // the realm split lives inside the file.
  {"NPC", luaopen_NPC_shared},
  {"NextBot", luaopen_NextBot_shared},
  {LUA_COLORLIBNAME, luaopen_Color},
  {LUA_CONCOMMANDLIBNAME, luaopen_ConCommand},
  {LUA_CONTENTSLIBNAME, luaopen_CONTENTS},
  {LUA_CONVARLIBNAME, luaopen_ConVar},
  {LUA_PASFILTERLIBNAME, luaopen_CPASFilter},
  {LUA_RECIPIENTFILTERLIBNAME, luaopen_CRecipientFilter},
  {LUA_TAKEDAMAGEINFOLIBNAME, luaopen_CTakeDamageInfo},
  {LUA_CVARLIBNAME, luaopen_cvar},
  {LUA_DBGLIBNAME, luaopen_dbg},
  {LUA_DEBUGOVERLAYLIBNAME, luaopen_debugoverlay},
  {LUA_ENGINELIBNAME, luaopen_engine},
#ifdef CLIENT_DLL
  // FIXME: obsolete? should be passing VPANELs, but passes Panel instead,
  // which always ends up being invalid (we can't access them by pointer)
  {LUA_ENGINEVGUILIBNAME, luaopen_enginevgui},
#endif
  {LUA_FCVARLIBNAME, luaopen_FCVAR},
  {LUA_FILESYSTEMLIBNAME, luaopen_filesystem},
#ifdef CLIENT_DLL
  {LUA_FONTFLAGLIBNAME, luaopen_FONTFLAG},
#endif
#ifndef CLIENT_DLL
  {LUA_ENTLISTLIBNAME, luaopen_gEntList},
#endif
  {LUA_GLOBALSLIBNAME, luaopen_gpGlobals},
  // HL2SB: ported from Experiment: Source.  gameevent.Listen.
  {LUA_GAMEEVENTSLIBNAME, luaopen_GameEvents},
  {LUA_HL2SBLIBNAME, luaopen_hl2sb},
#ifdef CLIENT_DLL
  {LUA_CLIENTSHADOWMGRLIBNAME, luaopen_g_pClientShadowMgr},
  {LUA_FONTLIBNAME, luaopen_HFont},
  {LUA_HSCHEMELIBNAME, luaopen_HScheme},
#endif
  {LUA_MATERIALLIBNAME, luaopen_IMaterial},
  {LUA_MOVEHELPERLIBNAME, luaopen_IMoveHelper},
  {LUA_INLIBNAME, luaopen_IN},
#ifndef CLIENT_DLL
  {LUA_NETCHANNELINFOLIBNAME, luaopen_INetChannelInfo},
#endif
  {LUA_INETWORKSTRINGTABLELIBNAME, luaopen_INetworkStringTable},
#ifdef CLIENT_DLL
  {LUA_INPUTLIBNAME, luaopen_input},
#endif
  {LUA_PHYSICSOBJECTLIBNAME, luaopen_IPhysicsObject},
  {LUA_PHYSICSSURFACEPROPSLIBNAME, luaopen_IPhysicsSurfaceProps},
  {LUA_PREDICTIONSYSTEMLIBNAME, luaopen_IPredictionSystem},
#ifdef CLIENT_DLL
  {LUA_ISCHEMELIBNAME, luaopen_IScheme},
#endif
//  {LUA_STEAMFRIENDSLIBNAME, luaopen_ISteamFriends},
  {LUA_KEYVALUESLIBNAME, luaopen_KeyValues},
  // HL2SB: ported from Experiment: Source
  {LUA_LOCALIZATIONLIBNAME, luaopen_Localizations},
  // HL2SB: ported from Experiment: Source.
  {LUA_PARTICLESYSTEMLIBNAME, luaopen_ParticleSystem},
  {LUA_SYSTEMSLIBNAME, luaopen_Systems},
  // HL2SB: SQLite-backed sql library (C side: sql.Query / sql.QueryTyped in
  // lsql.cpp, which also maintains sql.m_strError).  The rest of GMod's sql
  // surface -- SQLStr, TableExists, IndexExists, QueryRow, QueryValue, Begin,
  // Commit, LastError -- is Lua in lua/includes/util/sql.lua, which
  // includes/init.lua loads right after util.lua, so this must open first.
  {"sql", luaopen_Sql},
  // HL2SB: ported from Experiment: Source.  `Entities` is merged onto the same
  // global table as the Team Sandbox era `ents` (Create/GetByIndex).
  {LUA_ENTITIESLIBNAME, luaopen_Entities},
  // HL2SB: ported from Experiment: Source.  AudioChannel and the URL/file
  // streaming bindings were dropped -- they depend on their BASS manager.
  {LUA_SOUNDSLIBNAME, luaopen_Sounds},
  // TODO(port): Files / FileHandle still need a decision on how they coexist with
  // the Team Sandbox era `filesystem` library (LUA_FILESYSTEMLIBNAME) before they
  // can be registered.
  // HL2SB: GMod's file library.  This registration was commented out and there
  // was no implementation, so the lib alias ("file" -> LUA_FILESLIBNAME) at the
  // bottom of this file silently skipped and every imported GMod file that uses
  // file.* failed at load:
  //     extensions/file.lua:2: attempt to index a nil value (global 'file')
  //     extensions/player_auth.lua:77: ... (global 'file')
  // Implemented in public/lua/lfilesystem.cpp, which BOTH game vpcs already list,
  // so no .vpc needed touching and both realms get it.
  {LUA_FILESLIBNAME, luaopen_Files},
  // {LUA_FILEHANDLEMETANAME, luaopen_FileHandle},
#ifdef CLIENT_DLL
  {LUA_CLIENTENUMNAME, luaopen_ClientEnumerations},
#else
  {LUA_SERVERENUMNAME, luaopen_ServerEnumerations},
  // HL2SB (mc_swep audit B2): GMod's server-only downloadables library.
  { "resource", luaopen_resource },
#endif
  {LUA_MASKLIBNAME, luaopen_MASK},
  {LUA_MATHLIBLIBNAME, luaopen_mathlib},
  {LUA_MATRIXLIBNAME, luaopen_matrix3x4_t},
  {LUA_NETLIBNAME, luaopen_net},
  {LUA_NETWORKSTRINGTABLELIBNAME, luaopen_networkstringtable},
#ifdef CLIENT_DLL
  {LUA_PANELLIBNAME, luaopen_Panel},
  // HL2SB: ported from Experiment: Source.  Label is the base every Derma text
  // control sits on (DLabel, DButton, DTextEntry), so it has to be opened after
  // Panel, whose metatable it extends.
  {LUA_LABELMETANAME, luaopen_Label},
  // HL2SB: ported from Experiment: Source.  TextEntry extends Label,
  // so it opens after it.
  {LUA_TEXTENTRYMETANAME, luaopen_TextEntry},
#endif
  {LUA_PHYSENVLIBNAME, luaopen_physenv},
#ifdef CLIENT_DLL
  {LUA_PREDICTIONLIBNAME, luaopen_prediction},
#endif
  {LUA_QANGLELIBNAME, luaopen_QAngle},
  {LUA_RANDOMLIBNAME, luaopen_random},
#ifdef CLIENT_DLL
  // HL2SB: ported from Experiment: Source.  `Renders` carries GMod's render.*
  // (render.SetColorModulation, render.DrawSprite, render.PushRenderTarget, ...);
  // the Lua content aliases the global `render` onto it.  ITexture is the userdata
  // those bindings hand back, so its metatable is installed first.
  {LUA_ITEXTUREMETANAME, luaopen_ITexture},
  {LUA_RENDERSLIBNAME, luaopen_render},
  // HL2SB: ported from Experiment: Source.  GMod's chat.* lives here.
  {LUA_CHATSLIBNAME, luaopen_Chats},
#endif
#ifdef CLIENT_DLL
  {LUA_SCHEMELIBNAME, luaopen_scheme},
#endif
//  {LUA_STEAMAPICONTEXTLIBNAME, luaopen_steamapicontext},
  {LUA_SURFLIBNAME, luaopen_SURF},
#ifdef CLIENT_DLL
  {LUA_SURFACELIBNAME, luaopen_surface},
  // HL2SB: GMod's standalone Lua particle system (ParticleEmitter / CSEmitter /
  // CLuaParticle) -- Nuke Pack and every effect script that spawns its own
  // sprites (game/client/lua/luaparticle.cpp).
  {"CSEmitter", luaopen_CSEmitter},
  // HL2SB: GMod's .pcf engine-particle surface (CNewParticleEffect metatable +
  // CreateParticleSystem / CreateParticleSystemNoEntity globals) -- the other
  // half of the particle story, for effects that come from .pcf files
  // (game/client/lua/lnewparticle.cpp).
  {"CNewParticleEffect", luaopen_CNewParticleEffect},
  // HL2SB: GMod's effects.* C functions.  Lowercase "effects" -- the global
  // table lua/includes/modules/effects.lua's module("effects") merges onto
  // (package.loaded path), exactly how GMod layers its C + Lua halves.
  {"effects", luaopen_HL2SBClientEffects},
#endif
  {LUA_UTILLIBNAME, luaopen_UTIL},
  {LUA_UTILLIBNAME, luaopen_UTIL_shared},
  // HL2SB: there is no C++ `hl2sb_undo` library any more.  Undo is Garry's
  // Mod's Lua module (lua/includes/modules/undo.lua), which is loaded by
  // luasrc_dofolder() and owns the global `undo` table.
  {LUA_VECTORLIBNAME, luaopen_Vector},
#ifdef CLIENT_DLL
  {LUA_VGUILIBNAME, luaopen_vgui},
#endif
  {LUA_VMATRIXLIBNAME, luaopen_VMatrix},
  {NULL, NULL}
};


/*
** ===========================================================================
** HL2SB: metatable names.
**
** Garry's Mod exposes two C globals that all of its Lua framework (and the
** Experiment: Source gmod_compatibility shim) is built on:
**
**   FindMetaTable( name )          -> the metatable registered under `name`
**   RegisterMetaTable( name, tbl ) -> register one (Derma does this)
**
** GMod registers its metatables under the bare class names ("Entity", "Player",
** "Weapon", "Angle", ...); the Team Sandbox era code HL2SB inherited registers
** them under the C++ class names ("CBaseEntity", "CBasePlayer", ...).
** This table bridges the two, and the aliases are mirrored into the registry so
** that a plain `_R.Entity` works as well -- base_open already publishes the
** registry as `_R`, and Experiment's own Lua reads `_R.Entity` directly instead
** of going through FindMetaTable.
**
** A name with no entry falls through to a literal registry lookup, so classes
** HL2SB registers under the GMod name already (Panel, Frame, Button, ...) keep
** working, and RegisterMetaTable can add new ones ("DPanel", "DLabel", ...).
** ===========================================================================
*/
struct LuaMetatableAlias_t
{
  const char *pszGModName;   // name GMod code asks for
  const char *pszNativeName; // name HL2SB's libs registered it under
};

static const LuaMetatableAlias_t s_LuaMetatableAliases[] = {
  { "Entity",            LUA_BASEENTITYLIBNAME },
  { "Player",            LUA_BASEPLAYERLIBNAME },
  { "Weapon",            LUA_BASECOMBATWEAPONLIBNAME },
  { "Angle",             LUA_QANGLELIBNAME },
  { "Vector",            LUA_VECTORLIBNAME },
  { "Matrix",            LUA_MATRIXLIBNAME },
  { "Color",             LUA_COLORLIBNAME },
  { "EffectData",        LUA_EFFECTDATALIBNAME },
  { "Trace",             LUA_GAMETRACELIBNAME },
  { "ConsoleVariable",   LUA_CONVARLIBNAME },
  { "ConsoleCommand",    LUA_CONCOMMANDLIBNAME },
  { "RecipientFilter",   LUA_RECIPIENTFILTERLIBNAME },
  { "TakeDamageInfo",    LUA_TAKEDAMAGEINFOLIBNAME },
  { "Material",          LUA_MATERIALLIBNAME },
  { "MoveHelper",        LUA_MOVEHELPERLIBNAME },
  { "Texture",           LUA_ITEXTUREMETANAME },
  { "KeyValuesHandle",   LUA_KEYVALUESLIBNAME },
  { "FileHandle",        "FileHandle_t" },
  { "PhysicsObject",     LUA_PHYSICSOBJECTLIBNAME },
  { "PhysicsSurfacePropertiesHandle", LUA_PHYSICSSURFACEPROPSLIBNAME },
  { "NetChannelInfo",    LUA_NETCHANNELINFOLIBNAME },
  { "Panel",             "Panel" },
  { "Frame",             "Frame" },
  { "Button",            "Button" },
  { "CheckButton",       "CheckButton" },
  { "EditablePanel",     "EditablePanel" },
  { "ModelPanel",        "ModelPanel" },
  { "ProjectedTexture",  "ProjectedTexture" },  // TODO(port): lc_projected_texture
  { "AudioChannel",      "AudioChannel" },      // TODO(port): needs BASS
  { "MoveData",          "MoveData" },          // HL2SB: native CMoveData metatable
  { "CMoveData",         "MoveData" },          // GMod spelling used by FindMetaTable
  // GMod's name for the user command metatable, confirmed by dumping its registry:
  // "CUserCmd", with MetaID 19 (TYPE_USERCMD).
  { "CUserCmd",          "CUserCmd" },          // TODO(port): lusercmd
  { "MessageReader",     "MessageReader" },     // TODO(port): needs bf_read
  { "MessageWriter",     "MessageWriter" },     // TODO(port): needs bf_write
  { "Label",             "Label" },             // TODO(port): scripted_controls
  { "Html",              "Html" },              // TODO(port): scripted_controls
  { "TextEntry",         "TextEntry" },         // TODO(port): scripted_controls
  { "ModelImagePanel",   "ModelImagePanel" },   // TODO(port): scripted_controls
  { "SteamFriendsHandle", "SteamFriendsHandle" },
  // Confirmed by the registry dump: these all exist in GMod.  They resolve to nil
  // in HL2SB until the owning class is ported, which is the same thing GMod does
  // before the relevant library is loaded.
  { "NPC",               "NPC" },
  { "Vehicle",           "Vehicle" },
  { "NextBot",           "NextBot" },
  { "CSEnt",             "CSEnt" },
  { "Tool",              "Tool" },
  { "ISave",             "ISave" },
  { "IRestore",          "IRestore" },
  { "IMesh",             "IMesh" },
  { "CLuaEmitter",       "CLuaEmitter" },
  { "CLuaParticle",      "CLuaParticle" },
  { "CNewParticleEffect", "CNewParticleEffect" },
  { "SurfaceInfo",       "SurfaceInfo" },
  { "PhysCollide",       "PhysCollide" },
  { "IGModAudioChannel", "IGModAudioChannel" },
  { "IVideoWriter",      "IVideoWriter" },
  { "pixelvis_handle_t", "pixelvis_handle_t" },
  { "MarkupObject",      "MarkupObject" },
  { NULL, NULL }
};

static const char *LuaNativeMetatableName (const char *pszName) {
  for (int i = 0; s_LuaMetatableAliases[i].pszGModName; ++i) {
    if (!Q_stricmp(s_LuaMetatableAliases[i].pszGModName, pszName))
      return s_LuaMetatableAliases[i].pszNativeName;
  }
  return pszName;
}

/*
** GMod returns nil for a metatable that is not registered yet, which is what
** Derma relies on while it is still defining its controls.
**
** It has to be an explicit nil and not "no results": in Lua a call that returns
** zero values vanishes when it is the last argument of another call, so
** `print( FindMetaTable( "Label" ) )` collapsed to a bare `print()` and printed
** an empty line instead of "nil".
*/
static int lua_FindMetaTable (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);
  luaL_getmetatable(L, LuaNativeMetatableName(pszName));
  if (lua_isnil(L, -1))
    lua_pushnil(L);  /* leave exactly one value: nil */
  return 1;
}

static int lua_RegisterMetaTable (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_pushvalue(L, 2);
  lua_setfield(L, LUA_REGISTRYINDEX, pszName);
  return 0;
}

static const luaL_Reg lua_metatable_funcs[] = {
  {"FindMetaTable", lua_FindMetaTable},
  {"RegisterMetaTable", lua_RegisterMetaTable},
  {NULL, NULL}
};

/*
** HL2SB: the GameUI realm needs FindMetaTable/RegisterMetaTable too -- the
** derma control files (lua/vgui/DFrame.lua) capture the Panel metatable
** through FindMetaTable at load time, and the main menu now builds on that
** stack.  Same registration the game-state init runs above.
*/
void luasrc_register_metatable_globals (lua_State *L) {
  luaL_register(L, "_G", lua_metatable_funcs);
  lua_pop(L, 1);

}

/* Publish registry[gmodName] = the native metatable, so `_R.Entity` resolves. */
// Original entity-family accessors read native methods from their metatable before
// consulting Entity and instance fields. Publish inherited animation methods
// here so it does not need a second native field-lookup path.
static void luasrc_install_animation_methods (lua_State *L, const char *metatable) {
  luaL_getmetatable(L, "CBaseAnimating");
  luaL_getmetatable(L, metatable);
  if (lua_istable(L, -2) && lua_istable(L, -1)) {
    const int source = lua_gettop(L) - 1;
    const int target = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, source) != 0) {
      const char *name = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : NULL;
      if (name && !(name[0] == '_' && name[1] == '_') && lua_iscfunction(L, -1)) {
        lua_pushvalue(L, -2);
        lua_rawget(L, target);
        const bool missing = lua_isnil(L, -1);
        lua_pop(L, 1);
        if (missing) {
          lua_pushvalue(L, -2);
          lua_pushvalue(L, -2);
          lua_rawset(L, target);
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 2);
}

static void luasrc_install_metatable_aliases (lua_State *L) {
  for (int i = 0; s_LuaMetatableAliases[i].pszGModName; ++i) {
    const char *pszGModName = s_LuaMetatableAliases[i].pszGModName;
    const char *pszNativeName = s_LuaMetatableAliases[i].pszNativeName;

    if (!Q_stricmp(pszGModName, pszNativeName))
      continue;

    luaL_getmetatable(L, pszNativeName);
    if (lua_istable(L, -1)) {
      lua_pushvalue(L, -1);
      lua_setfield(L, LUA_REGISTRYINDEX, pszGModName);
    }
    lua_pop(L, 1);
  }
}

/*
** ===========================================================================
** HL2SB: GMod type names.
**
** GMod's Lua type system is not the raw Lua one.  garrysmod/lua/includes/util.lua
** *replaces* the global `type` with:
**
**   function type( v )
**     local v_type = C_type( v )
**     if ( v_type ~= "userdata" ) then return v_type end
**     local metatable = getmetatable( v )
**     local metaName = metatable and metatable.MetaName
**     return C_type( metaName ) == "string" and metaName or "UserData"
**   end
**
** and TypeID() reads metatable.MetaID, and isentity() walks
** metatable.MetaBaseClass to find "Entity".  So a metatable without MetaName
** makes type() report "UserData" for everything the moment GMod's util.lua is
** loaded, which would break all of Derma and the spawnmenu.
**
** HL2SB's own type() (luamanager.cpp) reads __type instead, and its values are
** the Team Sandbox era lowercase ones ("entity", "vector", "panel", ...).  The
** names below are stamped as both __type and MetaName so the two agree, and they
** follow Garry's Mod rather than Experiment: Source, which disagrees in three
** places (it calls a player "Entity", a VMatrix "Matrix", and a physics object
** "PhysicsObject").  Values are Garry's Mod's TYPE_* enum
** (https://wiki.facepunch.com/gmod/Enums/TYPE).
**
** Stamping happens here, once, rather than editing the ~30 binding files that
** create the metatables: every luaopen_* has already run by this point, so this
** covers both realms, and the table stays the single place to audit.
** ===========================================================================
*/
#define LUA_TYPE_ENTITY        9
#define LUA_TYPE_VECTOR       10
#define LUA_TYPE_ANGLE        11
#define LUA_TYPE_PHYSOBJ      12
#define LUA_TYPE_DAMAGEINFO   15
#define LUA_TYPE_EFFECTDATA   16
#define LUA_TYPE_MOVEDATA     17
#define LUA_TYPE_RECIPFILTER  18
#define LUA_TYPE_USERCMD      19
#define LUA_TYPE_MATERIAL     21
#define LUA_TYPE_PANEL        22
#define LUA_TYPE_TEXTURE      25
#define LUA_TYPE_CONVAR       27
#define LUA_TYPE_MATRIX       29
#define LUA_TYPE_FILE         34
#define LUA_TYPE_PROJTEX      41
#define LUA_TYPE_USERDATA      7
// GMod's Color metatable reports MetaID 44, not the TYPE_COLOR = 255 constant --
// 255 is a networking hack for net.WriteType (see the TYPE enum), while the
// metatable itself carries 44, which equals TYPE_COUNT in that enum.  Taken from
// a dump of GMod's own registry, not from the wiki.
#define LUA_TYPE_COLOR        44
// Not in the TYPE enum of the dumped build (TYPE_COUNT is 44 there), but GMod
// does register a MarkupObject metatable and reports 45 for it.
#define LUA_TYPE_MARKUP       45

struct LuaTypeInfo_t
{
  const char *pszMetatable;   // registry name the metatable was created under
  const char *pszTypeName;    // MetaName / __type
  int iTypeID;                // MetaID
  const char *pszBaseMetatable; // MetaBaseClass, or NULL
  bool bIsTableType;          // value is a Lua table: stamp MetaName/MetaID but NOT __type
};

static const LuaTypeInfo_t s_LuaTypeInfo[] = {
  // Entities.  GMod reports Player and Weapon separately and chains them onto
  // Entity, which is what isentity() walks.
  { LUA_BASEENTITYLIBNAME,      "Entity",          LUA_TYPE_ENTITY,     NULL },
  { LUA_BASEPLAYERLIBNAME,      "Player",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CHL2MP_Player",            "Player",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  // HL2SB: GMod reports TYPE_ENTITY for a vehicle and chains it onto Entity, so
  // isentity() walks it.  type( <vehicle> ) therefore answers "Vehicle".
  { LUA_VEHICLELIBNAME,         "Vehicle",         LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  // HL2SB: GMod reports NPC and NextBot as their own type names chained onto
  // Entity (MetaID 9, the same TYPE_ENTITY family) -- its registry carries
  // both under those names.  FindMetaTable / isentity() / type() all read
  // these.
  { "NPC",                      "NPC",             LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "NextBot",                  "NextBot",         LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { LUA_BASECOMBATWEAPONLIBNAME,"Weapon",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseAnimating",           "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseFlex",                "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseCombatCharacter",     "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },

  // Value types.
  { LUA_VECTORLIBNAME,          "Vector",          LUA_TYPE_VECTOR,     NULL },
  { LUA_QANGLELIBNAME,          "Angle",           LUA_TYPE_ANGLE,      NULL },
  { LUA_COLORLIBNAME,           "Color",           LUA_TYPE_COLOR,      NULL, /*bIsTableType*/ true },
  { LUA_VMATRIXLIBNAME,         "VMatrix",         LUA_TYPE_MATRIX,     NULL },
  { LUA_MATRIXLIBNAME,          "VMatrix",         LUA_TYPE_MATRIX,     NULL },
  { LUA_GAMETRACELIBNAME,       "Trace",           LUA_TYPE_USERDATA,   NULL },
  { LUA_KEYVALUESLIBNAME,       "KeyValues",       LUA_TYPE_USERDATA,   NULL },

  // engine objects
  { LUA_TAKEDAMAGEINFOLIBNAME,  "CTakeDamageInfo", LUA_TYPE_DAMAGEINFO, NULL },
  { LUA_EFFECTDATALIBNAME,      "CEffectData",     LUA_TYPE_EFFECTDATA, NULL },
  { LUA_RECIPIENTFILTERLIBNAME, "CRecipientFilter",LUA_TYPE_RECIPFILTER,NULL },
  { LUA_PASFILTERLIBNAME,       "CRecipientFilter",LUA_TYPE_RECIPFILTER,LUA_RECIPIENTFILTERLIBNAME },
  { LUA_MATERIALLIBNAME,        "IMaterial",       LUA_TYPE_MATERIAL,   NULL },
  { LUA_ITEXTUREMETANAME,       "ITexture",        LUA_TYPE_TEXTURE,    NULL },
  { LUA_CONVARLIBNAME,          "ConVar",          LUA_TYPE_CONVAR,     NULL },
  { LUA_CONCOMMANDLIBNAME,      "ConCommand",      LUA_TYPE_CONVAR,     NULL },
  { LUA_PHYSICSOBJECTLIBNAME,   "PhysObj",         LUA_TYPE_PHYSOBJ,    NULL },
  // The metatable is registered as "FileHandle_t" (lfilesystem.cpp);
  // LUA_FILEHANDLEMETANAME is the *library* name, not the metatable name.
  { "FileHandle_t",             "File",            LUA_TYPE_FILE,       NULL },
  { LUA_MOVEHELPERLIBNAME,      "MoveHelper",      LUA_TYPE_MOVEDATA,   NULL },

  // All vgui controls report "Panel" in GMod, with the class chain in
  // MetaBaseClass -- this is what derma's panels all rely on.
  { "Panel",                    "Panel",           LUA_TYPE_PANEL,      NULL },
  { "EditablePanel",            "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "Frame",                    "Panel",           LUA_TYPE_PANEL,      "EditablePanel" },
  { "Button",                   "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "CheckButton",              "Panel",           LUA_TYPE_PANEL,      "Button" },
  { "ModelPanel",               "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "PropertyDialog",           "Panel",           LUA_TYPE_PANEL,      "Frame" },
  { "PropertyPage",             "Panel",           LUA_TYPE_PANEL,      "EditablePanel" },

  { NULL, NULL, 0, NULL }
};

static void luasrc_install_type_names (lua_State *L) {
  for (int i = 0; s_LuaTypeInfo[i].pszMetatable; ++i) {
    luaL_getmetatable(L, s_LuaTypeInfo[i].pszMetatable);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;  // not opened in this realm
    }

    // metatable.MetaName is what GMod's type() reads for userdata.
    lua_pushstring(L, s_LuaTypeInfo[i].pszTypeName);
    lua_setfield(L, -2, "MetaName");

    // metatable.__type is what HL2SB's own type() (luamanager.cpp) reads, and it is
    // skipped for the types GMod implements as plain Lua tables (Color): HL2SB's
    // type() reports __type for anything carrying a metatable, while GMod's type()
    // short-circuits on the raw Lua type and answers "table" there.  Verified by
    // dumping GMod: `Color() -> type = table`, and its Color metatable still has
    // MetaName "Color" / MetaID 44.
    if (!s_LuaTypeInfo[i].bIsTableType) {
      lua_pushstring(L, s_LuaTypeInfo[i].pszTypeName);
      lua_setfield(L, -2, "__type");
    }

    lua_pushinteger(L, s_LuaTypeInfo[i].iTypeID);
    lua_setfield(L, -2, "MetaID");

    if (s_LuaTypeInfo[i].pszBaseMetatable) {
      luaL_getmetatable(L, s_LuaTypeInfo[i].pszBaseMetatable);
      if (lua_istable(L, -1))
        lua_setfield(L, -2, "MetaBaseClass");
      else
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
  }
}

/*
** ===========================================================================
** HL2SB: GMod's lowercase library globals.
**
** HL2SB inherited Experiment: Source's capitalised library names for a handful
** of libraries -- Systems, Files, UTIL, Renders, Sounds, Chats,
** ParticleSystems, ScriptedEntities -- while Garry's Mod spells them
** system, file, util, render, sound, chat, particle, scripted_ents.
**
** GMod code and every addon use the lowercase spelling exclusively; GMod's own
** lua/derma/init.lua opens with system.IsLinux(), and its lua/vgui controls are
** full of render.*, surface.*, draw.*.
**
** The alias MERGES the Experiment table into the GMod name (see the function
** below): entries the GMod name already has are kept, the rest are added.  It
** must not replace the table, because the two realms register some of these
** under different spellings and replacing one with the other silently deletes a
** library -- that is exactly how util.PrecacheModel went missing.  A missing
** library (several are realm-gated) is skipped rather than aliased to nil, so the
** name simply stays undefined in the realm that does not have it -- which is what
** GMod does too.
**
** Entities / player / gameevent are already aliased where they are opened
** (luabinding.cpp:88, lplayer.cpp:652, lgameevents.cpp:92); this table is only
** for the ones that had no GMod spelling at all.
**
** Stamped here, once, rather than in each binding file: every luaopen_* has
** already run by the time luasrc_openlibs() reaches this, so both realms are
** covered and the table stays the single place to audit.
** ===========================================================================
*/
static const char *s_pGModLibAliases[][2] = {
  { "system",         LUA_SYSTEMSLIBNAME },
  { "util",           LUA_UTILLIBNAME },
  { "file",           LUA_FILESLIBNAME },
  { "render",         LUA_RENDERSLIBNAME },
  { "sound",          LUA_SOUNDSLIBNAME },
  { "chat",           LUA_CHATSLIBNAME },
  { "particle",       LUA_PARTICLESYSTEMLIBNAME },
  { "scripted_ents",  LUA_SCRIPTEDENTITIESLIBNAME },
  { NULL, NULL }
};

static void luasrc_install_lib_aliases (lua_State *L) {
  for (int i = 0; s_pGModLibAliases[i][0]; ++i) {
    const char *pszGModName = s_pGModLibAliases[i][0];
    const char *pszExpName  = s_pGModLibAliases[i][1];

    lua_getglobal(L, pszExpName);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    const int nSrc = lua_gettop(L);

    lua_getglobal(L, pszGModName);
    if (!lua_istable(L, -1)) {
      /* Nothing under the GMod name yet -- publish the source table itself. */
      lua_pop(L, 1);
      lua_pushvalue(L, nSrc);
      lua_setglobal(L, pszGModName);
      lua_pop(L, 1);
      continue;
    }
    const int nDst = lua_gettop(L);

    /* The GMod name already exists, so MERGE instead of overwriting.
    **
    ** lua_setglobal() here used to clobber a real library.  Concretely: on the
    ** client luaopen_UTIL() (game/client/lua/lcdll_util.cpp) registers its
    ** functions under "UTIL" while luaopen_UTIL_shared()
    ** (game/shared/lua/lutil_shared.cpp) registers under "util" -- and GMod's
    ** spelling is "util".  So `util = UTIL` threw away everything the shared
    ** library had put there: util.PrecacheModel, util.PrecacheSound,
    ** util.TraceLine, util.DecalTrace ...  The visible breakage was
    **
    **   lua/includes/util.lua:225: attempt to call a nil value
    **                             (field 'PrecacheModel')
    **
    ** from Model() in GMod's own util.lua, which weapons/gmod_camera/shared.lua
    ** calls at load time.  Same shape for every realm-gated pair in the table
    ** above, so the rule is: never destroy an existing library, only fill it in.
    ** Existing entries win -- a name the GMod spelling already owns is left
    ** alone, and package.loaded[ name ] stays the same table as _G[ name ]. */
    lua_pushnil(L);
    while (lua_next(L, nSrc) != 0) {
      lua_pushvalue(L, -2);              /* key */
      lua_rawget(L, nDst);               /* dst[ key ] */
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushvalue(L, -2);            /* key */
        lua_pushvalue(L, -2);            /* value */
        lua_rawset(L, nDst);             /* dst[ key ] = value */
      } else {
        lua_pop(L, 1);
      }
      lua_pop(L, 1);                     /* pop the value, keep the key */
    }

    lua_settop(L, nSrc - 1);             /* drop src and dst */
  }
}

/*
** HL2SB: the same alias table, for a state that did not go through
** luasrc_openlibs().
**
** The main menu state (luasrc_init_gameui, game/shared/lua/luamanager.cpp)
** opens a hand-picked library list instead of calling luasrc_openlibs, so it
** never reached the loop above and had no GMod spellings at all: no `file`, no
** `system`, no `render`, no `util`.  That is not cosmetic -- every GMod script
** written against those names, and our own lua/gameui/*.lua, fail on them:
** contentsubgames.lua guarded itself with `if ( not file ) then return end` and
** went quiet, which is why the Content dialog listed nothing.
**
** Exposed rather than re-listed there on purpose: s_pGModLibAliases has to stay
** the single place to audit, because several of these libraries are realm-gated
** and the merge rule above (never clobber an existing table) matters for each.
*/
void luasrc_install_gmod_lib_aliases (lua_State *L) {
  luasrc_install_lib_aliases(L);
}

//-----------------------------------------------------------------------------
// HL2SB: the engine half of GMod's `game` library.
//
// Only the members the ported content actually calls are here.  The ported
// flechette gun's SWEP body runs
//     game.AddParticles( "particles/hunter_flechette.pcf" )
// and threw "attempt to call a nil value (field 'AddParticles')" six times per
// session, because the table created below was empty.
//-----------------------------------------------------------------------------
static int lua_game_AddParticles (lua_State *L) {
  const char *pszParticleFile = luaL_checkstring( L, 1 );
  bool bLoaded = false;

  // GMod's game.AddParticles( file ) loads a .pcf so that the particle systems
  // inside it can be precached by name afterwards.  That is what GMod SWEPs rely
  // on: the ported flechette gun runs
  //     game.AddParticles( "particles/hunter_flechette.pcf" )
  // and the weapon then precaches "hunter_muzzle_flash", "flechette_halo", ...
  //
  // This used to hand the *file name* to PrecacheParticleSystem(), which takes a
  // system name, so every call produced
  //     Attemped to precache unknown particle system "particles/....pcf"!
  // and the weapon had no effects at all.  ReadParticleConfigFile() is the same
  // entry the engine's own particles_manifest.txt loader uses.
  if ( pszParticleFile[0] == '\0' ) {
    lua_pushboolean( L, false );
    return 1;
  }

  if ( g_pParticleSystemMgr == NULL ) {
    luasrc_LuaWarnMsgF( "[HL2SB] game.AddParticles: particle system manager not ready, '%s' not loaded\n", pszParticleFile );
  }
  else if ( !filesystem->FileExists( pszParticleFile, "GAME" ) ) {
    luasrc_LuaWarnMsgF( "[HL2SB] game.AddParticles: '%s' is not in the search paths - its particle systems stay unknown\n", pszParticleFile );
  }
  else {
    bLoaded = g_pParticleSystemMgr->ReadParticleConfigFile( pszParticleFile, true, false );

    if ( !bLoaded ) {
      luasrc_LuaWarnMsgF( "[HL2SB] game.AddParticles: '%s' exists but could not be parsed\n", pszParticleFile );
    }
  }

  lua_pushboolean( L, bLoaded );
  return 1;
}

// HL2SB (2026-10-16): ShutDown-time fallback.  The ShutDown hook is raised
// from luasrc_shutdown (luamanager.cpp), whose server call site is the tail of
// CServerGameDLL::LevelShutdown (gameinterface.cpp) - by then the level is
// already torn down and gpGlobals->mapname / engine->GetLevelName() answer an
// empty string.  Sandbox persistence builds its save path as
// game.GetMap() .. "_" .. page .. ".txt" right inside ShutDown
// (gamemodes/sandbox/gamemode/persistence.lua:38), and PersistenceLoad looks
// the same name up on the next map load.  Reference behavior: GMod raises
// ShutDown while the map is still loaded, so the reference-side
// game.GetMap() answers with the live map name there.  Cache the last
// non-empty level name seen and fall back to it whenever the live source is
// empty, so the name observed through Lua stays stable across the shutdown.
static char s_szLuaLastMap[MAX_PATH] = "";

static int lua_game_GetMap (lua_State *L) {
  // The realms disagree on where the map name lives: the client has
  // IVEngineClient::GetLevelName() (CGlobalVarsBase has no mapname), the server
  // has CGlobalVars::mapname (IVEngineServer has no GetLevelName()).
#ifdef CLIENT_DLL
  const char *pszLevel = engine->GetLevelName();
#else
  const char *pszLevel = STRING( gpGlobals->mapname );
#endif
  char szPath[MAX_PATH];
  char szMap[MAX_PATH];
  const char *pszBase = szPath;
  char *pDot;

  Q_strncpy( szPath, pszLevel ? pszLevel : "", sizeof( szPath ) );

  // "maps/foo.bsp" -> "foo"
  for ( const char *p = szPath; *p; ++p ) {
    if ( *p == '/' || *p == '\\' ) {
      pszBase = p + 1;
    }
  }

  Q_strncpy( szMap, pszBase, sizeof( szMap ) );

  pDot = Q_strrchr( szMap, '.' );
  if ( pDot ) {
    *pDot = '\0';
  }

  if ( szMap[0] != '\0' ) {
    Q_strncpy( s_szLuaLastMap, szMap, sizeof( s_szLuaLastMap ) );
  } else if ( s_szLuaLastMap[0] != '\0' ) {
    // Level already gone (ShutDown / state teardown): answer with the last map
    // this realm actually had loaded.
    Q_strncpy( szMap, s_szLuaLastMap, sizeof( szMap ) );
  }

  lua_pushstring( L, szMap );
  return 1;
}

static int lua_game_SinglePlayer (lua_State *L) {
  lua_pushboolean( L, gpGlobals->maxClients <= 1 );
  return 1;
}

static int lua_global_RealTime (lua_State *L) {
  lua_pushnumber( L, gpGlobals ? gpGlobals->realtime : 0.0f );
  return 1;
}

static int lua_game_MaxPlayers (lua_State *L) {
  lua_pushinteger( L, gpGlobals->maxClients );
  return 1;
}

static int lua_game_IsDedicated (lua_State *L) {
#ifdef CLIENT_DLL
  lua_pushboolean( L, false );
#else
  lua_pushboolean( L, engine->IsDedicatedServer() != 0 );
#endif
  return 1;
}

// HL2SB (2026-10-02) GMod compat: game.GetAmmoName( ammoID ) -> string|nil.
// Wiki: "Returns the ammo name for given ammo type ID ... or nil if ammo type
// ID is invalid."  Complements Player:GiveAmmo's numeric form, whose pickup
// notification now resolves through the same ammo def.
static int lua_game_GetAmmoName (lua_State *L) {
  const int iAmmoType = luaL_checkint( L, 1 );
  CAmmoDef *pAmmoDef = GetAmmoDef();
  Ammo_t *pAmmo = ( pAmmoDef != NULL && iAmmoType >= 0 )
    ? pAmmoDef->GetAmmoOfIndex( iAmmoType ) : NULL;
  if ( pAmmo == NULL || pAmmo->pName == NULL || pAmmo->pName[ 0 ] == '\0' ) {
    lua_pushnil( L );
  } else {
    lua_pushstring( L, pAmmo->pName );
  }
  return 1;
}

// HL2SB (2026-10-07): GMod's game.GetAmmoID( string ammoName ) -> number,
// the inverse of GetAmmoName; hl1sweps' Equip() asks for
// game.GetAmmoID( "hornet" ) to clamp the hornet gun's ammo pool.
// Unknown names answer -1 (CAmmoDef::Index's own miss value).
static int lua_game_GetAmmoID (lua_State *L) {
  const char *pszName = luaL_checkstring( L, 1 );
  CAmmoDef *pAmmoDef = GetAmmoDef();
  lua_pushinteger( L, ( pAmmoDef != NULL && pszName != NULL && pszName[ 0 ] != '\0' )
    ? pAmmoDef->Index( pszName ) : -1 );
  return 1;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: the console print globals.
//
// GMod has Msg() (no newline) and MsgN() (newline) on both realms, and its own
// lua/includes files use them - extensions/gmod_isvalid.lua:115 and
// extensions/player_auth.lua:78 both failed with
//     attempt to call a nil value (global 'Msg')
//     attempt to call a nil value (global 'MsgN')
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// HL2SB: Msg/MsgN/Warning/ErrorNoHalt all funnel into luasrc_LuaConsoleMsg, so
// a script's output is coloured by severity and (with hl2sb_lua_log on) lands
// in hl2sb_lua.log exactly like the engine's own Lua diagnostics.
//-----------------------------------------------------------------------------
static void LuaConcatArgs (lua_State *L, char *pOut, int nOutSize) {
  int nOut = 0;
  pOut[0] = '\0';

  int nArgs = lua_gettop( L );
  for ( int i = 1; i <= nArgs; ++i ) {
    size_t nLength = 0;
    const char *pszText = luaL_tolstring( L, i, &nLength );
    nOut += Q_snprintf( pOut + nOut, nOutSize - nOut, "%s", pszText ? pszText : "" );
    lua_pop( L, 1 );
    if ( nOut >= nOutSize - 1 )
      break;
  }
}

static int lua_Msg (lua_State *L) {
  char szText[2048];
  LuaConcatArgs( L, szText, sizeof( szText ) );
  luasrc_LuaConsoleMsg( szText, 'I', false );
  return 0;
}

static int lua_MsgN (lua_State *L) {
  char szText[2048];
  LuaConcatArgs( L, szText, sizeof( szText ) );
  luasrc_LuaConsoleMsg( szText, 'I', true );
  return 0;
}

// HL2SB (sbrust): implemented in luamanager.cpp (local prototype, no header
// churn -- same pattern as HL2SB_CollectLuaError below).
void luasrc_LuaConsoleMsgColor( const char *pszText, const Color &clr, bool bNewline );

//-----------------------------------------------------------------------------
// HL2SB (sbrust): GMod 的 MsgC( Color, ... )。契约（wiki + GMod 行为）：
//   * Color 参数切换"当前颜色"，后续文本用它打印，直到下一个 Color；
//     本 fork 的 Color 是带 r/g/b/a 字段的普通表（lColor.cpp lua_pushcolor），
//     r/g/b 至少存在即按 Color 处理；
//   * 其余参数按 tostring 直接连拼（无分隔符 -- 与 print 的 \t 不同，
//     `MsgC( c, "HP: ", hp )` 输出 "HP: 100"）；
//   * 结尾补换行（MsgC 是整行语义；半行续行走 Msg/MsgN）；
//   * 颜色经 luasrc_LuaConsoleMsgColor 直达控制台，整行照旧进 hl2sb_lua.log。
// 首 Color 之前的文本用白色。缓冲写满时按当前颜色提前分片续行，视觉不变。
//-----------------------------------------------------------------------------
static int lua_MsgC (lua_State *L) {
  Color curColor( 255, 255, 255, 255 );
  char szSegment[2048];
  int nSeg = 0;
  szSegment[0] = '\0';

  int nArgs = lua_gettop( L );
  for ( int i = 1; i <= nArgs; ++i ) {
    if ( lua_istable( L, i ) ) {
      lua_getfield( L, i, "r" );
      lua_getfield( L, i, "g" );
      lua_getfield( L, i, "b" );
      lua_getfield( L, i, "a" );
      if ( !lua_isnil( L, -4 ) && !lua_isnil( L, -3 ) && !lua_isnil( L, -2 ) ) {
        if ( nSeg > 0 ) {
          luasrc_LuaConsoleMsgColor( szSegment, curColor, false );
          nSeg = 0;
        }
        curColor = Color( (int)lua_tointeger( L, -4 ), (int)lua_tointeger( L, -3 ),
                          (int)lua_tointeger( L, -2 ), (int)lua_tointeger( L, -1 ) );
      }
      lua_pop( L, 4 );
      continue;
    }

    size_t nLength = 0;
    const char *pszText = luaL_tolstring( L, i, &nLength );
    if ( pszText != NULL && nLength > 0 ) {
      if ( nSeg + (int)nLength >= (int)sizeof( szSegment ) - 1 ) {
        luasrc_LuaConsoleMsgColor( szSegment, curColor, false );
        nSeg = 0;
        szSegment[0] = '\0';
      }
      nSeg += Q_snprintf( szSegment + nSeg, sizeof( szSegment ) - nSeg, "%s", pszText );
    }
    lua_pop( L, 1 );
  }

  if ( nSeg > 0 || nArgs == 0 )
    luasrc_LuaConsoleMsgColor( szSegment, curColor, true );
  return 0;
}

// GMod's bare Warning(): orange, and collected in the log like everything else.
static int lua_Warning (lua_State *L) {
  char szText[2048];
  LuaConcatArgs( L, szText, sizeof( szText ) );
  luasrc_LuaConsoleMsg( szText, 'W', true );
  return 0;
}

// HL2SB (2026-09-21): local prototype on purpose (no header churn) -- the
// collector lives in luamanager.cpp; ErrorNoHalt feeds it from here.
void HL2SB_CollectLuaError( const char *pszError, const char *pszTraceback );

//-----------------------------------------------------------------------------
// HL2SB GMod compat: ErrorNoHalt( ... ) and ErrorNoHaltWithStack( ... ).
//
// GMod's non-fatal error print.  Neither existed in this fork (the only
// definitions were in lua/includes/modules/gmod_compatibility/sh_init.lua,
// which is inert behind GMOD_COMPATIBILITY = false), so every GMod file that
// reports a soft failure died on the report itself:
//
//   gamemodes/sandbox/gamemode/spawnmenu/creationmenu/content/contentsearch.lua:118
//       ErrorNoHalt( "..." )
//   lua/includes/modules/spawnmenu.lua:292
//       ErrorNoHaltWithStack( "spawnmenu.GetContentType got an invalid value\n" )
//
// and several pre-existing fork files call them too.  Behaviour matches GMod /
// this fork's Msg: the arguments are concatenated with tostring() and written to
// the console, and nothing halts.  ErrorNoHaltWithStack additionally writes a
// Lua traceback, which is the reason GMod scripts reach for it.
//-----------------------------------------------------------------------------
static int lua_ErrorNoHalt (lua_State *L) {
  char szText[2048];
  LuaConcatArgs( L, szText, sizeof( szText ) );
  luasrc_LuaConsoleMsg( szText, 'E', true );

  // HL2SB (2026-09-21): ErrorNoHalt is how GMod addons report soft failures;
  // those belong in the error viewer as much as hard errors do.  Collect with
  // a traceback so the entry names its origin.
  luaL_traceback( L, L, szText, 1 );
  const char *pszReport = lua_tostring( L, -1 );
  HL2SB_CollectLuaError( szText, pszReport );
  lua_pop( L, 1 );
  return 0;
}

static int lua_ErrorNoHaltWithStack (lua_State *L) {
  lua_ErrorNoHalt( L );

  // luaL_traceback( L, L, NULL, 1 ) writes "stack traceback:" plus the frames
  // into a new string on the stack.  NULL for the message is legal in 5.4.
  luaL_traceback( L, L, NULL, 1 );
  const char *pszTrace = lua_tostring( L, -1 );
  if ( pszTrace != NULL )
    luasrc_LuaConsoleMsg( pszTrace, 'E', true );
  lua_pop( L, 1 );

  return 0;
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB GMod compat: SuppressHostEvents( ent ).
//
// GMod's server-side helper for "do not filter this player's own prediction out
// of the temp entities I am about to send" - it is exactly
// IPredictionSystem::SuppressHostEvents(), and the ported flechette gun calls it
// (with NULL) before spawning its projectile.  Without the global the weapon
// aborted on that line and never fired:
//     weapon_flechettegun/shared.lua:58: attempt to call a nil value (global
//     'SuppressHostEvents')
//-----------------------------------------------------------------------------
static int lua_SuppressHostEvents (lua_State *L) {
  // GMod passes its NULL *sentinel* (a userdata, not Lua nil) to clear the
  // suppress host, so this has to go through lua_toentity() - the converter that
  // yields NULL for both - instead of luaL_checkentity(), which rejects the
  // sentinel with "CBaseEntity expected, got NULL entity" and aborted the caller:
  //   weapon_flechettegun/shared.lua:58 (before ents.Create, so the gun never
  //   fired) and weapon_fists/shared.lua:135 (before TakeDamageInfo, so the fists
  //   did no damage).
  IPredictionSystem::SuppressHostEvents( lua_toentity( L, 1 ) );
  return 0;
}
#endif

//-----------------------------------------------------------------------------
// HL2SB GMod compat: the global IsFirstTimePredicted().
//
// GMod exposes it as a global on both realms; this fork only had the client-side
// library method prediction.IsFirstTimePredicted.  The Nyan Gun's PrimaryAttack
// opens with it (weapon_nyangun.lua:95) and threw 65 times in one run - which
// also left its firing sound looping forever, because the code that stops the
// sound comes after that line and never ran.
//-----------------------------------------------------------------------------
static int lua_IsFirstTimePredicted (lua_State *L) {
#ifdef CLIENT_DLL
  lua_pushboolean( L, prediction->IsFirstTimePredicted() );
#else
  // The server is the authority; GMod's server-side answer is "yes, go ahead".
  lua_pushboolean( L, true );
#endif
  return 1;
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB GMod compat (2026-10-03): AddOriginToPVS( vector ) - the server global
// GMod addons call from SetupPlayerVisibility to widen an entity's transmit
// set.  One line over the engine's own PVS accumulator.
//-----------------------------------------------------------------------------
static int lua_AddOriginToPVS (lua_State *L) {
  engine->AddOriginToPVS( luaL_checkvector( L, 1 ) );
  return 0;
}
#endif

//-----------------------------------------------------------------------------
// HL2SB GMod compat: the scripted-entity network variable shim.
//
// GMod's ENT:SetupDataTables() declares network variables:
//
//     self:NetworkVar( "Float", 0, "BallSize" )
//     self:NetworkVarNotify( "BallSize", self.OnBallSizeChanged )
//
// and GMod's engine turns each declaration into the Set<name>/Get<name> pair the
// script then calls.  This fork has no DataTable slots for Lua entities, so the
// accessors are defined here -- installed by CBaseScripted::InitScriptedEntity()
// as the entity table's NetworkVar / NetworkVarNotify fields (basescripted.cpp:
// 206-238), which is why a stock GMod entity script can call them without being
// modified.
//
// The pair is LARGELY the same shim weapons/weapon_base/shared.lua already builds
// for SWEPs, with one difference that matters: a SWEP stores per-table and each
// realm reads its own copy, but a network var that the SERVER sets and the CLIENT
// reads must cross the wire (sent_ball's SpawnFunction sets BallSize/BallColor on
// the server, while its ENT:Draw reads them on the client).  A plain Lua table
// would leave the client on the defaults -- size 0, black -- so the two variables
// sent_ball declares are ALSO carried by fields the entity already networks:
//
//   BallColor -> SetRenderColor / GetRenderColor   (m_clrRender; the right type,
//               the right semantics, already replicated)
//   BallSize  -> m_nSkin                           (CNetworkVar( int ) on
//               CBaseAnimating, already replicated)
//
// BALLCOLOR UNITS.  The script-visible shape is GMod's 0..1 Vector
// (NetworkVar( "Vector", 0, "BallColor" ); ENT:Initialize does
// SetBallColor( Vector( 1, 0.3, 0.3 ) )) while m_clrRender is a 0..255 color32,
// so the setter scales by 255 and the getter divides by 255, and both clamp.
// The getter returns a 0..1 Vector for a Vector-declared variable and the usual
// 0..255 Color for any other declared type, because the declared type is what
// GMod's own Get<name>() would have returned.
//
// BALLSIZE CARRIER.  BallSize is a Float but m_nSkin is an int, so the carrier
// is lossy: the exact value always lives in the entity's own Lua table (the
// realm that set it reads it back exactly, fractional or not) and m_nSkin is
// only the cross-realm copy the other realm falls back to.  Reading therefore
// consults the entity table FIRST and uses the replicated value only when this
// realm never set the variable -- which also removes the old "0 means unset"
// ambiguity.
//
// Known collision (documented, not hidden): m_nSkin is CBaseAnimating's own
// "skin" INPUT -- DEFINE_INPUT( m_nSkin, FIELD_INTEGER, "skin" ),
// game/server/baseanimating.cpp:164 -- so `ent_fire <ball> skin <n>` (or a menu
// that feeds that input) changes the ball size.  It is an INPUT, not a
// DEFINE_KEYFIELD, so a raw `skin` key in a BSP entity lump is not applied at
// spawn; the collision needs something to fire the input.
//
// THERE IS NO BETTER REPLICATED INT CARRIER on this class:
//   m_nSkin     10 bits signed  (ANIMATION_SKIN_BITS, baseanimating.h:527)  input "skin"
//   m_nBody     32 bits         (ANIMATION_BODY_BITS)          keyfield "body" + input "SetBodyGroup"
//   m_nHitboxSet 2 bits unsigned (ANIMATION_HITBOXSET_BITS)    keyfield "hitboxset" -- too small anyway
//   m_nForceBone 8 bits signed  (-128..127)                    no keyfield, but sent_ball's own
//                                                              MaxSize is 128, so it does not fit
// m_nSkin is the best fit (covers sent_ball's 4..128 and is already proven
// replicated for this entity), so it stays, with the read order above.
//
// Everything else -- and every other type -- keeps the per-table behaviour, so
// this is a superset of the SWEP shim, not a replacement.  Self-designed: GMod
// uses real engine network vars and there is no public implementation to copy.
//-----------------------------------------------------------------------------
enum HL2SB_NWStorage_t
{
  HL2SB_NW_TABLE = 0,   // entity's own Lua table (the SWEP shim's behaviour)
  HL2SB_NW_SKIN,        // m_nSkin        (int,   CNetworkVar)
  HL2SB_NW_RENDERCOLOR, // m_clrRender    (color32, CNetworkVar)
};

static HL2SB_NWStorage_t HL2SB_NWStorageForName (const char *pszName) {
  if ( V_stricmp( pszName, "BallSize" ) == 0 )
    return HL2SB_NW_SKIN;

  // HL2SB (2026-09-21): BallColor USED to map to HL2SB_NW_RENDERCOLOR
  // (m_clrRender) so the engine's CNetworkColor32 would carry it to the
  // client.  That broke the stock script's own color roll:
  //     if ( !self:GetBallColor():IsZero() ) then return end
  //     self:SetBallColor( table.Random( {...} ) )
  // m_clrRender's default is 255,255,255, so GetBallColor() read (1,1,1) --
  // never "zero" -- the random roll was skipped on every spawn and every ball
  // stayed white.  GMod's NetworkVars default to (0,0,0), which is exactly
  // what the HL2SB_NW_TABLE storage + HL2SB_NWPushValue push.  BallColor now
  // uses the table storage and reaches the client through the HL2SB_NW user
  // message replication (see HL2SB_Lua_EntityNetworkVarSet).
  return HL2SB_NW_TABLE;
}

// True for the declared types whose values are numeric (GMod's Float and Int),
// which is what decides whether a value is normalised through lua_tonumber()
// before it is stored.  ENT:KeyValue hands sent_ball's SetBallSize() the string
// value of the key ("32"), and GMod's own network-var setter coerces that to a
// number -- without this, GetBallSize() would hand a string back to
// math.Clamp().
static bool HL2SB_NWIsNumericType (const char *pszType) {
  return V_stricmp( pszType, "Float" ) == 0 || V_stricmp( pszType, "Int" ) == 0;
}

// Push the script-visible default for a declared variable.
static void HL2SB_NWPushValue (lua_State *L, const char *pszType) {
  if ( V_stricmp( pszType, "Vector" ) == 0 ) {
    lua_pushvector( L, Vector( 0, 0, 0 ) );
    return;
  }

  if ( V_stricmp( pszType, "Angle" ) == 0 ) {
    lua_pushangle( L, QAngle( 0, 0, 0 ) );
    return;
  }

  if ( V_stricmp( pszType, "Bool" ) == 0 ) {
    lua_pushboolean( L, false );
    return;
  }

  if ( V_stricmp( pszType, "String" ) == 0 ) {
    lua_pushstring( L, "" );
    return;
  }

  if ( V_stricmp( pszType, "Entity" ) == 0 ) {
    // HL2SB (2026-10-07): GMod's Entity-declared network var answers a NULL
    // entity until something is set.  This used to fall through to the numeric
    // default below, so Get<name>() handed scripts the number 0 and the first
    // IsValid() on it died with "attempt to index a number value" (the hl1sweps
    // RPG laser spot errored on every think and never spawned).
    CBaseEntity::PushLuaInstanceSafe( L, NULL );
    return;
  }

  // Float / Int: GMod's numeric default.
  lua_pushnumber( L, 0 );
}

// Push the table that holds this entity's scripted variables and return its
// stack index; return 0 (pushing nothing) when there is none.
//
// This matters because the two halves of the shim are invoked with DIFFERENT
// kinds of `self`, and the first version assumed they were the same:
//
//   NetworkVar / NetworkVarNotify   self = the entity's Lua TABLE
//       (CBaseScripted::InitScriptedEntity() pushes the table it got from
//        entity.get() -- basescripted.cpp:231 -- and refs that same table into
//        m_nTableReference a few lines later)
//   Set<name> / Get<name>           self = the entity USERDATA
//       (BEGIN_LUA_CALL_ENTITY_METHOD pushes lua_pushanimating() as `self`, so
//        ENT:Initialize()'s self:SetBallSize() hands this closure a userdata)
//
// lua_rawget()/lua_rawset() need a real table: called with a userdata they go
// through Lua's internal api_check (compiled out in release builds) and treat
// the userdata's memory as a Table header.  So the accessors resolve the
// entity's table through the same member CBaseEntity___index uses
// (lbaseentity_shared.cpp:2196).  A table self is used as-is, which keeps
// NetworkVar/NetworkVarNotify and a script that calls Set<name> on the table
// directly working.
static int HL2SB_NWPushStorageTable (lua_State *L, int iSelf) {
  if ( lua_istable( L, iSelf ) ) {
    lua_pushvalue( L, iSelf );
    return lua_gettop( L );
  }

  CBaseEntity *pEntity = lua_toentity( L, iSelf );
  if ( pEntity != NULL && pEntity->m_nTableReference >= 0 &&
       lua_isrefvalid( L, pEntity->m_nTableReference ) ) {
    lua_getref( L, pEntity->m_nTableReference );
    if ( lua_istable( L, -1 ) ) {
      return lua_gettop( L );
    }
    lua_pop( L, 1 );
  }

  return 0;
}

// table[ pszStorageKey ] -> pushed value, true; nothing pushed, false.
static bool HL2SB_NWRawGet (lua_State *L, int iTable, const char *pszStorageKey) {
  lua_pushstring( L, pszStorageKey );
  lua_rawget( L, iTable );
  if ( !lua_isnil( L, -1 ) ) {
    return true;
  }

  lua_pop( L, 1 );
  return false;
}

// table[ pszStorageKey ] = the value at stack index iValue (raw, so a variable
// named like an existing field cannot be hijacked by the table's metatable).
static void HL2SB_NWRawSet (lua_State *L, int iTable, const char *pszStorageKey, int iValue) {
  lua_pushstring( L, pszStorageKey );
  lua_pushvalue( L, iValue );
  lua_rawset( L, iTable );
}

// table[ pszStorageKey ] = flValue (raw).
static void HL2SB_NWRawSetNumber (lua_State *L, int iTable, const char *pszStorageKey, double flValue) {
  lua_pushstring( L, pszStorageKey );
  lua_pushnumber( L, flValue );
  lua_rawset( L, iTable );
}

// Read the current value of a declared variable and push it.  iTable is the
// storage table (or 0 when the entity has none).  The value being replaced stays
// on the stack underneath, so the wrapper can still use it as
// NetworkVarNotify's "old" argument.
static void HL2SB_NWReadValue (lua_State *L, int iSelf, int iTable, const char *pszStorageKey,
                               HL2SB_NWStorage_t storage, const char *pszType) {
  if ( storage == HL2SB_NW_TABLE ) {
    if ( iTable != 0 && HL2SB_NWRawGet( L, iTable, pszStorageKey ) ) {
      return;
    }
    HL2SB_NWPushValue( L, pszType );
    return;
  }

  if ( storage == HL2SB_NW_SKIN ) {
    // The entity's own table wins: it carries the exact value the script set on
    // this realm (m_nSkin truncates a fractional BallSize, and 0 there means
    // "nothing networked" rather than "unset").
    if ( iTable != 0 && HL2SB_NWRawGet( L, iTable, pszStorageKey ) ) {
      return;
    }

    // This realm never set it, so the replicated copy is all there is.  It is
    // read through lua_toanimating()'s real-RTTI cast, not reinterpreted from a
    // CBaseEntity: an entity that is not a CBaseAnimating has no m_nSkin.
    lua_CBaseAnimating *pAnimating = lua_toanimating( L, iSelf );
    if ( pAnimating != NULL && pAnimating->m_nSkin != 0 ) {
      lua_pushnumber( L, pAnimating->m_nSkin );
      return;
    }

    HL2SB_NWPushValue( L, pszType );
    return;
  }

  // m_clrRender.  0..1 Vector when the declared type is Vector -- which is what
  // sent_ball's ENT:Draw reads (c.r / c.g / c.b, then x255 for DrawSprite) --
  // and the engine's 0..255 Color otherwise.  The bytes are already clamped by
  // their type, so the division cannot overflow anything.
  CBaseEntity *pEntity = lua_toentity( L, iSelf );

  if ( pEntity == NULL ) {
    HL2SB_NWPushValue( L, pszType );
    return;
  }

  const color32 clr = pEntity->GetRenderColor();

  if ( V_stricmp( pszType, "Vector" ) == 0 ) {
    lua_pushvector( L, Vector( clr.r / 255.0f, clr.g / 255.0f, clr.b / 255.0f ) );
    return;
  }

  lua_pushcolor( L, Color( clr.r, clr.g, clr.b, clr.a ) );
}

// GMod's m_clrRender is a 0..255 color32, so a script-visible colour has to be
// scaled explicitly.  Everything is clamped: DrawSprite narrows int -> unsigned
// char with no clamp of its own, so an out-of-range value wraps mod 256 instead
// of saturating.
static byte HL2SB_NWColorByte (double flValue) {
  if ( flValue <= 0.0 ) {
    return 0;
  }

  if ( flValue >= 255.0 ) {
    return 255;
  }

  return ( byte )( flValue + 0.5 );
}

// Store the value the script passed and leave the value Get<name>() will hand
// back on top of the stack, so the notify callback can be given the same
// (normalised) "new" value.
static void HL2SB_NWStoreValue (lua_State *L, int iSelf, int iTable, const char *pszStorageKey,
                                HL2SB_NWStorage_t storage, const char *pszType) {
  if ( storage == HL2SB_NW_RENDERCOLOR ) {
    // Two accepted shapes.  A 0..1 Vector is GMod's colour shape
    // (SetBallColor( Vector( 1, 0.3, 0.3 ) )); a 0..255 Color table comes from
    // ENT:KeyValue's "rendercolor" path and from hand-written addons.
    byte r = 0, g = 0, b = 0, a = 255;

    if ( luaL_testudata( L, 2, LUA_VECTORLIBNAME ) != NULL ) {
      const Vector vec = luaL_checkvector( L, 2 );
      r = HL2SB_NWColorByte( ( double )vec.x * 255.0 );
      g = HL2SB_NWColorByte( ( double )vec.y * 255.0 );
      b = HL2SB_NWColorByte( ( double )vec.z * 255.0 );
    } else if ( lua_iscolor( L, 2 ) ) {
      const Color clr = luaL_checkcolor( L, 2 );
      r = HL2SB_NWColorByte( clr.r() );
      g = HL2SB_NWColorByte( clr.g() );
      b = HL2SB_NWColorByte( clr.b() );
      a = HL2SB_NWColorByte( clr.a() );
    } else {
      luaL_argerror( L, 2, "Vector or Color expected" );
    }

    // The script-visible value goes into the entity's table (so a re-read here
    // is exact and cheap), the wire copy into m_clrRender.
    if ( iTable != 0 ) {
      HL2SB_NWRawSet( L, iTable, pszStorageKey, 2 );
    }

    CBaseEntity *pEntity = lua_toentity( L, iSelf );
    if ( pEntity != NULL ) {
      pEntity->SetRenderColor( r, g, b, a );
    }

    HL2SB_NWReadValue( L, iSelf, iTable, pszStorageKey, storage, pszType );  // [.., new]
    return;
  }

  if ( storage == HL2SB_NW_SKIN ) {
    // The exact value is what this realm keeps; m_nSkin is only the int copy the
    // other realm gets.  A numeric string (ENT:KeyValue: "ball_size" "32") is
    // coerced here, the way GMod's own Float network var would.
    const double flValue = lua_tonumber( L, 2 );

    if ( iTable != 0 ) {
      HL2SB_NWRawSetNumber( L, iTable, pszStorageKey, flValue );
    }

    // m_nSkin is a 10-bit signed SendProp (ANIMATION_SKIN_BITS,
    // game/server/baseanimating.h:527), so clamp into what the wire can carry
    // (sent_ball's own sizes are 4..128).  Not an animating entity -> no
    // m_nSkin at all; the variable degrades to the per-table behaviour, which
    // is what the SWEP shim does, instead of throwing inside ENT:Initialize.
    lua_CBaseAnimating *pAnimating = lua_toanimating( L, iSelf );
    if ( pAnimating != NULL ) {
      int iValue = ( int )flValue;
      if ( iValue < 0 ) {
        iValue = 0;
      } else if ( iValue > 511 ) {
        iValue = 511;
      }
      pAnimating->m_nSkin = iValue;
    }

    HL2SB_NWReadValue( L, iSelf, iTable, pszStorageKey, storage, pszType );  // [.., new]
    return;
  }

  // Plain per-table storage.
  if ( iTable != 0 ) {
    if ( HL2SB_NWIsNumericType( pszType ) ) {
      HL2SB_NWRawSetNumber( L, iTable, pszStorageKey, lua_tonumber( L, 2 ) );
    } else {
      HL2SB_NWRawSet( L, iTable, pszStorageKey, 2 );
    }
  }

  HL2SB_NWReadValue( L, iSelf, iTable, pszStorageKey, storage, pszType );  // [.., new]
}

// Set<name>( value ) for one entity.  Upvalue 1 is the type ("Float"), upvalue 2
// the storage key ("__hl2sb_nw_BallSize"), upvalue 3 the slot, upvalue 4 the
// storage kind.  Mirrors the SWEP shim, plus the cross-realm stores above and
// GMod's NetworkVarNotify callback.
//
// Stack discipline (the first version pushed a *stack* slot that did not exist
// and then rawset() a nil key, so every Set<name>() threw "table index is nil";
// the second resolved no table at all, because `self` here is a USERDATA):
//   entry                      [self, value]
//   storage table pushed       [self, value, table]
//   read + store               [self, value, table, old, new]
//   notify (if any)            above, with the callback's frame pushed and popped
//   return                     the extra slots are discarded by the VM
//
// HL2SB (2026-09-21): REPLICATION.  The storage above is per-realm, so a
// server-side NetworkVar write (sent_ball rolls a random BallColor in
// Initialize) never reached the client -- every self-drawing entity showed
// its default color.  The setter now serializes the value and broadcasts it
// in the "HL2SB_NW" user message; the client handler stores the payload in
// s_NWReplicated and the getter falls back to it whenever the local storage
// has no value.  Pure user message, no engine table changes.

// Wire tags (HL2SB_NWTag_t) and the family enumeration (HL2SB_NWFamily_t)
// live in lbaseentity_shared.h -- the SetNW*/SetNW2* setters share them.

static char HL2SB_NWTagForType( const char *pszType )
{
	if ( !pszType )
		return 0;
	if ( !Q_stricmp( pszType, "Float" ) )
		return HL2SB_NWTAG_FLOAT;
	if ( !Q_stricmp( pszType, "Vector" ) )
		return HL2SB_NWTAG_VECTOR;
	if ( !Q_stricmp( pszType, "Angle" ) )
		return HL2SB_NWTAG_ANGLE;
	if ( !Q_stricmp( pszType, "Bool" ) )
		return HL2SB_NWTAG_BOOL;
	if ( !Q_stricmp( pszType, "Int" ) )
		return HL2SB_NWTAG_INT;
	if ( !Q_stricmp( pszType, "String" ) )
		return HL2SB_NWTAG_STRING;
	if ( !Q_stricmp( pszType, "Entity" ) )
		return HL2SB_NWTAG_ENTITY;
	return 0;
}

// Serialize the Lua value at iValue into pOut.  Returns the payload length,
// or -1 when the type is unknown (the caller then skips replication).
static int HL2SB_NWSerialize( lua_State *L, int iValue, char tag, unsigned char *pOut, int nMax )
{
	bf_write w( "HL2SB_NW", pOut, nMax );
	switch ( tag )
	{
		case 'N':
			break;
		case 'D':
		{
			double number = luaL_checknumber(L, iValue);
			w.WriteBytes(&number, sizeof(number));
			break;
		}
		case HL2SB_NWTAG_FLOAT:
			w.WriteFloat( ( float )luaL_checknumber( L, iValue ) );
			break;
		case HL2SB_NWTAG_VECTOR:
		{
			Vector v = luaL_checkvector( L, iValue );
			w.WriteFloat( v.x );
			w.WriteFloat( v.y );
			w.WriteFloat( v.z );
			break;
		}
		case HL2SB_NWTAG_ANGLE:
		{
			QAngle a = luaL_checkangle( L, iValue );
			w.WriteFloat( a.x );
			w.WriteFloat( a.y );
			w.WriteFloat( a.z );
			break;
		}
		case HL2SB_NWTAG_BOOL:
			w.WriteByte( lua_toboolean( L, iValue ) ? 1 : 0 );
			break;
		case HL2SB_NWTAG_INT:
			w.WriteLong( ( int )luaL_checknumber( L, iValue ) );
			break;
		case HL2SB_NWTAG_STRING:
		{
			const char *psz = luaL_checkstring( L, iValue );
			int n = ( int )Q_strlen( psz );
			if ( n > 255 )
				n = 255;
			w.WriteByte( n );
			w.WriteBytes( psz, n );
			break;
		}
		case HL2SB_NWTAG_ENTITY:
		{
			CBaseEntity *pEnt = lua_toentity( L, iValue );
			w.WriteShort( pEnt ? pEnt->entindex() : 0 );
			break;
		}
		default:
			return -1;
	}
	return w.GetNumBytesWritten();
}

#ifndef CLIENT_DLL
static bool HL2SB_GlobalSend(lua_State *state, const char *name, int value, CBasePlayer *player)
{
  char tag = 0;
  switch (lua_type(state, value)) {
    case LUA_TNIL: tag = 'N'; break;
    case LUA_TNUMBER: tag = 'D'; break;
    case LUA_TBOOLEAN: tag = HL2SB_NWTAG_BOOL; break;
    case LUA_TSTRING: tag = HL2SB_NWTAG_STRING; break;
    case LUA_TUSERDATA:
      if (luaL_testudata(state, value, "Vector")) tag = HL2SB_NWTAG_VECTOR;
      else if (luaL_testudata(state, value, "Angle")) tag = HL2SB_NWTAG_ANGLE;
      else tag = HL2SB_NWTAG_ENTITY;
      break;
    default: return false;
  }
  unsigned char payload[512];
  int len = HL2SB_NWSerialize(state, value, tag, payload, sizeof(payload));
  if (len < 0 || len + (int)Q_strlen(name) + 7 > MAX_USER_MSG_DATA) return false;
  CRecipientFilter filter;
  filter.MakeReliable();
  if (player) filter.AddRecipient(player); else filter.AddAllPlayers();
  UserMessageBegin(filter, "HL2SB_NW");
    WRITE_SHORT(0);
    WRITE_BYTE(3);
    WRITE_STRING(name);
    WRITE_BYTE(tag);
    WRITE_BYTE(len);
    for (int i = 0; i < len; ++i) WRITE_BYTE(payload[i]);
  MessageEnd();
  return true;
}

// HL2SB (2026-10-11, issue #47): broadcast one SetNW*/SetNW2* value.  The
// wire adds the family byte ahead of the name; the client receiver stores it
// into the family-namespaced replicated copy and drops the realm-local entry.
// GMod's own transports differ (NW2 rides the entity snapshot delta, NW1 a
// periodically re-sent user message); a reliable broadcast on change plus the
// sign-on resync below is this fork's equivalent contract -- not PVS gated,
// which makes a value strictly more available than GMod's, at the cost of
// sending to clients that cannot currently see the entity.
void HL2SB_NWReplicateValue( int iFamily, CBaseEntity *pEntity, const char *pszName,
                             char tag, lua_State *L, int iValue )
{
	if ( pEntity == NULL || pEntity->entindex() <= 0 || pszName == NULL || pszName[0] == '\0' )
		return;

	unsigned char payload[ 512 ];
	int len = HL2SB_NWSerialize( L, iValue, tag, payload, sizeof( payload ) );
	if ( len < 0 )
		return;

	// (Registered at openlibs on both realms -- see the client tail of
	// luasrc_openlibs.)
	CReliableBroadcastRecipientFilter filter;
	UserMessageBegin( filter, "HL2SB_NW" );
		WRITE_SHORT( pEntity->entindex() );
		WRITE_BYTE( ( unsigned char )iFamily );
		WRITE_STRING( pszName );
		WRITE_BYTE( ( unsigned char )tag );
		WRITE_BYTE( ( unsigned char )len );
		for ( int i = 0; i < len; ++i )
			WRITE_BYTE( payload[ i ] );
	MessageEnd();
}
#endif

#ifdef CLIENT_DLL
// The client-side replicated store: key "entindex_name" -> tag byte + payload.
// Entries accumulate for the life of the process; each is 300 bytes at most
// and only exists for variables a script actually declared, so this stays small.
// CUtlString here would be wrong: this fork's CUtlString::Length() is strlen,
// and NW payloads are binary -- float 32.0f serializes to bytes 00 00 48 42,
// whose leading 0x00 cut the stored length to 1, so every later read
// overflowed and came back as 0 (the tiny-ball bug, logged as
// "NW get float: 0.000000" while the wire carried 32.0).  Use a POD blob with
// an explicit byte count instead.
struct HL2SB_NWBlob_t
{
	int nBytes;
	unsigned char buf[ 300 ];	// 1 tag byte + up to 256 payload + slack
};
static CUtlDict<HL2SB_NWBlob_t, unsigned short> s_NWReplicated;

// Native render state must be readable without entering the Lua VM.
bool HL2SB_NWReplicatedString( int iFamily, int entindex, const char *pszName, CUtlString &value )
{
	char key[192];
	Q_snprintf( key, sizeof( key ), "%d|%d|%s", iFamily, entindex, pszName );
	unsigned short idx = s_NWReplicated.Find( key );
	if ( !s_NWReplicated.IsValidIndex( idx ) )
		return false;
	const HL2SB_NWBlob_t &blob = s_NWReplicated[idx];
	if ( blob.nBytes < 2 || blob.buf[0] != HL2SB_NWTAG_STRING || blob.buf[1] > blob.nBytes - 2 )
		return false;
	value.SetDirect( (const char *)blob.buf + 2, blob.buf[1] );
	return true;
}

void HL2SB_NWReplicatedFreeEntityVars( int entindex )
{
	for ( unsigned short idx = s_NWReplicated.First(); s_NWReplicated.IsValidIndex( idx ); )
	{
		unsigned short next = s_NWReplicated.Next( idx );
		int family, entity;
		if ( sscanf( s_NWReplicated.GetElementName( idx ), "%d|%d|", &family, &entity ) == 2 && entity == entindex )
			s_NWReplicated.RemoveAt( idx );
		idx = next;
	}
}

static void HL2SB_NWReplicatedStore( int iFamily, int entindex, const char *pszName, char tag,
									const unsigned char *pPayload, int nPayload )
{
	// Family is part of the key: the legacy Lua-NetworkVar channel (0) and the
	// SetNW*/SetNW2* families (1/2) are independent namespaces, exactly like
	// GMod's three tables.  '|' cannot appear in an entindex/name pair.
	char key[ 192 ];
	Q_snprintf( key, sizeof( key ), "%d|%d|%s", iFamily, entindex, pszName );

	// Readers treat byte 0 as the tag and the rest as the payload, but the
	// hook hands those over as two separate arguments -- rejoin them.  (The
	// first build stored the bare payload: val[0] was the first float's low
	// byte, the tag switch fell through to nil, and sent_ball indexing the
	// nil color cascaded into an unprotected error and a process abort.)
	HL2SB_NWBlob_t blob;
	blob.nBytes = 0;
	if ( nPayload < 0 )
		nPayload = 0;
	if ( nPayload > ( int )sizeof( blob.buf ) - 1 )
		nPayload = ( int )sizeof( blob.buf ) - 1;
	blob.buf[ 0 ] = ( unsigned char )tag;
	if ( nPayload > 0 )
		Q_memcpy( blob.buf + 1, pPayload, nPayload );
	blob.nBytes = nPayload + 1;

	// HL2SB (2026-09-25): OVERWRITE an existing entry.  CUtlDict::Insert does
	// NOT replace -- it inserts a DUPLICATE node next to the old one, and
	// Find() kept answering the FIRST value ever stored.  Every NetworkVar
	// update after the first (the lemon's ChangeState(1) at detonation) was
	// therefore invisible to the client: the replicated dictionary answered
	// the spawn-time 0 forever, ENT:Draw kept drawing the model through its
	// own fire, and the amount on an ammo-pickup card never grew.
	unsigned short idx = s_NWReplicated.Find( key );
	if ( s_NWReplicated.IsValidIndex( idx ) )
		s_NWReplicated[ idx ] = blob;
	else
		s_NWReplicated.Insert( key, blob );
}

// Shared reader for generated NetworkVar and SetNW*/SetNW2* getters. Entity
// values resolve on every read so a target that arrives later becomes visible;
// a target that does not resolve answers the NULL sentinel.
bool HL2SB_NWReplicatedPushFamily( lua_State *L, int iFamily, int entindex,
								   const char *pszName )
{
	char key[ 192 ];
	Q_snprintf( key, sizeof( key ), "%d|%d|%s", iFamily, entindex, pszName );

	unsigned short idx = s_NWReplicated.Find( key );
	if ( !s_NWReplicated.IsValidIndex( idx ) )
		return false;

	HL2SB_NWBlob_t &val = s_NWReplicated[ idx ];
	if ( val.nBytes < 1 )
		return false;

	char tag = ( char )val.buf[ 0 ];
	bf_read r( "HL2SB_NW", val.buf + 1, val.nBytes - 1 );

	switch ( tag )
	{
		case 'D':
		{
			double number = 0;
			if (val.nBytes != 1 + sizeof(number)) return false;
			r.ReadBytes(&number, sizeof(number));
			lua_pushnumber(L, number);
			return true;
		}
		case HL2SB_NWTAG_FLOAT:
			lua_pushnumber( L, ( lua_Number )r.ReadFloat() );
			return true;
		case HL2SB_NWTAG_VECTOR:
		{
			Vector v;
			v.x = r.ReadFloat();
			v.y = r.ReadFloat();
			v.z = r.ReadFloat();
			lua_pushvector( L, v );
			return true;
		}
		case HL2SB_NWTAG_ANGLE:
		{
			QAngle a;
			a.x = r.ReadFloat();
			a.y = r.ReadFloat();
			a.z = r.ReadFloat();
			lua_pushangle( L, a );
			return true;
		}
		case HL2SB_NWTAG_BOOL:
			lua_pushboolean( L, r.ReadByte() != 0 );
			return true;
		case HL2SB_NWTAG_INT:
			lua_pushinteger( L, ( lua_Integer )r.ReadLong() );
			return true;
		case HL2SB_NWTAG_STRING:
		{
			int n = r.ReadByte();
			char sz[ 256 ];
			if ( n > 0 )
				r.ReadBytes( sz, n );
			sz[ n ] = 0;
			lua_pushstring( L, sz );
			return true;
		}
		case HL2SB_NWTAG_ENTITY:
		{
			int iTarget = r.ReadShort();
			C_BaseEntity *pTarget = NULL;
			if ( iTarget > 0 ) {
				IClientEntity *pIClient = cl_entitylist->GetClientEntity( iTarget );
				pTarget = pIClient ? pIClient->GetBaseEntity() : NULL;
			}
			// lua_pushentity( NULL ) pushes the NULL sentinel.
			lua_pushentity( L, pTarget );
			return true;
		}
		default:
			return false;
	}
}

void HL2SB_NWPushReplicatedTable(lua_State *L, int entindex, int tableIndex)
{
  tableIndex = lua_absindex(L, tableIndex);
  char prefix[48];
  Q_snprintf(prefix, sizeof(prefix), "%d|%d|", HL2SB_NWFAMILY_NW, entindex);
  const size_t length = Q_strlen(prefix);
  for (unsigned short i = s_NWReplicated.First(); s_NWReplicated.IsValidIndex(i); i = s_NWReplicated.Next(i)) {
    const char *key = s_NWReplicated.GetElementName(i);
    if (Q_strncmp(key, prefix, length) != 0) continue;
    const char *name = key + length;
    if (HL2SB_NWReplicatedPushFamily(L, HL2SB_NWFAMILY_NW, entindex, name))
      lua_setfield(L, tableIndex, name);
  }
}

// The "HL2SB_NW" user message: SHORT entindex, BYTE family, STRING name,
// BYTE tag, BYTE len, BYTE[len] payload.  Family 0 is the legacy Lua
// NetworkVar channel, 1/2 are the SetNW*/SetNW2* families (issue #47).
static void __MsgFunc_HL2SB_NW( bf_read &read )
{
	int entindex = read.ReadShort();
	int iFamily = read.ReadByte();
	char szName[ 128 ];
	read.ReadString( szName, sizeof( szName ) );
	char tag = ( char )read.ReadByte();
	int len = read.ReadByte();

	unsigned char payload[ 256 ];
	if ( len > 0 && len <= ( int )sizeof( payload ) )
		read.ReadBytes( payload, len );
	else
		len = 0;

	if ( read.IsOverflowed() )
		return;
	if (iFamily == 3) {
		if (!L) return;
		int top = lua_gettop(L);
		lua_getglobal(L, "__HL2SB_GLOBALS");
		if (lua_istable(L, -1)) {
			HL2SB_NWReplicatedStore(iFamily, 0, szName, tag, payload, len);
			lua_pushstring(L, szName);
			if (tag == 'N' || !HL2SB_NWReplicatedPushFamily(L, iFamily, 0, szName)) lua_pushnil(L);
			lua_rawset(L, -3);
		}
		lua_settop(L, top);
		return;
	}

	// HL2SB GMod compat (2026-10-21): the reference client raises the
	// "EntityNetworkedVarChanged" hook when a networked value really changes.
	// The hook is answered with the entity, the name, the old value and the
	// new one.  The old value is read before the update is stored and the new
	// one after, and the reference skips the hook when the two are equal.
	int iBase = 0;
	int iOldSlot = 0;
	if ( L != NULL )
	{
		iBase = lua_gettop( L );
		if ( !HL2SB_NWReplicatedPushFamily( L, iFamily, entindex, szName ) )
			lua_pushnil( L );
		iOldSlot = lua_gettop( L );
	}

	HL2SB_NWReplicatedStore( iFamily, entindex, szName, tag, payload, len );

	if ( L != NULL )
	{
		if ( !HL2SB_NWReplicatedPushFamily( L, iFamily, entindex, szName ) )
			lua_pushnil( L );
		const int iNewSlot = lua_gettop( L );

		// Only a real change is worth a hook: the reference compares the two
		// values and stays quiet when they match.
		bool bChanged = true;
		if ( lua_isnumber( L, iOldSlot ) && lua_isnumber( L, iNewSlot ) )
			bChanged = ( lua_tonumber( L, iOldSlot ) != lua_tonumber( L, iNewSlot ) );
		else if ( lua_isstring( L, iOldSlot ) && lua_isstring( L, iNewSlot ) )
			bChanged = ( Q_strcmp( lua_tostring( L, iOldSlot ), lua_tostring( L, iNewSlot ) ) != 0 );
		else if ( lua_isnil( L, iOldSlot ) && lua_isnil( L, iNewSlot ) )
			bChanged = false;

		IClientEntity *pIClient = cl_entitylist->GetClientEntity( entindex );
		C_BaseEntity *pEnt = pIClient ? pIClient->GetBaseEntity() : NULL;
		if ( bChanged && pEnt != NULL )
		{
			BEGIN_LUA_CALL_HOOK( "EntityNetworkedVarChanged" );
				lua_pushentity( L, pEnt );
				lua_pushstring( L, szName );
				lua_pushvalue( L, iOldSlot );
				lua_pushvalue( L, iNewSlot );
			END_LUA_CALL_HOOK( 4, 0 );
		}
		lua_settop( L, iBase );
	}

	// HL2SB (2026-09-25): the server update OVERWRITES the realm-local value.
	// A scripted entity's client-side ENT:Initialize writes its defaults into
	// the local storage table (the lemon opens with ChangeState(0)) and the
	// getter prefers a locally-written entry -- so the stale local 0 shadowed
	// every later server update (state 1/2/3) and the grenade kept its model
	// drawn through its own fire.  Drop the local key here; the getter falls
	// through to the replicated payload just stored, and the next LOCAL write
	// (prediction) re-shadows until the following update -- exactly how GMod's
	// DT vars behave.
	if ( L != NULL ) {
		const int iBase = lua_gettop( L );
		IClientEntity *pIClient = cl_entitylist->GetClientEntity( entindex );
		C_BaseEntity *pEnt = pIClient ? pIClient->GetBaseEntity() : NULL;
		if ( pEnt != NULL ) {
			CBaseEntity::PushLuaInstanceSafe( L, pEnt );
			const int iTable = HL2SB_NWPushStorageTable( L, -1 );
			if ( iTable != 0 ) {
				char szStorageKey[ 160 ];
				Q_snprintf( szStorageKey, sizeof( szStorageKey ), "__hl2sb_nw_%s", szName );
				lua_pushstring( L, szStorageKey );
			lua_pushnil( L );
				lua_rawset( L, iTable );
			}
		}
		lua_settop( L, iBase );		// unconditional restore
	}

	// SetNW*/SetNW2*: same overwrite contract -- the server update wins, so
	// drop the realm-local copy the same way the legacy path above does.  This
	// runs unconditionally (not only when L is NULL): an else-if here made the
	// drop dead code on every normal client, where L is always live, so a
	// locally written SetNW* default kept shadowing the server value.
	if ( iFamily == HL2SB_NWFAMILY_NW || iFamily == HL2SB_NWFAMILY_NW2 )
		HL2SB_NWDropLocalVar( iFamily, entindex, szName );
}

// HL2SB (2026-09-25): "HL2SB_AMMO" user message -- STRING ammoName,
// SHORT amount.  The server's Player:GiveAmmo binding sends it whenever GMod
// would show the pickup popup (hidePopup == false); the client turns it into
// the shared HUDAmmoPickedUp hook, which the GMod base gamemode normally draws
// (wiki GM:HUDAmmoPickedUp -- "Called when the client has picked up ammo").
//
// HL2SB (2026-10-02): dispatch through hook.Run, not hook.call.  hook.lua's
// call() signature is ( eventName, gamemodeTable, ... ) -- the old three-value
// push put the ammo NAME into the gamemode slot and left the amount as the
// only vararg, so every registered callback received ( amount, nil ) and any
// addon reading the second argument raised "concatenate a nil value" on each
// pickup.  hook.Run( name, ... ) forwards ( ammoName, amount ) to registered
// hooks and then falls back to GM:HUDAmmoPickedUp, which is exactly the GMod
// contract.  Stack discipline matches __MsgFunc_SWEPCmd below: snapshot the
// top and restore unconditionally instead of hand-counting pops.
static void __MsgFunc_HL2SB_AMMO( bf_read &read )
{
	char szName[ 128 ];
	read.ReadString( szName, sizeof( szName ) );
	const int nAmount = read.ReadShort();

	if ( read.IsOverflowed() || szName[ 0 ] == '\0' )
		return;

	if ( L == NULL )
		return;

	const int iBase = lua_gettop( L );

	lua_getglobal( L, "hook" );
	if ( lua_istable( L, -1 ) ) {
		lua_getfield( L, -1, "Run" );
		if ( lua_isfunction( L, -1 ) ) {
			lua_remove( L, -2 );
			lua_pushstring( L, "HUDAmmoPickedUp" );
			lua_pushstring( L, szName );
			lua_pushinteger( L, nAmount );
			luasrc_pcall( L, 3, 0, 0 );
		}
	}

	lua_settop( L, iBase );		// unconditional restore
}

// Owner-only weapon method message, retaining the entity serial number.
static void __MsgFunc_SWEPCmd(bf_read &read) {
  const unsigned int encoded = (unsigned int)read.ReadLong();
  char method[32], data[256];
  read.ReadString(method,sizeof(method));
  read.ReadString(data,sizeof(data));
  if (read.IsOverflowed() || !L || !g_bLuaInitialized || !method[0] ||
      encoded == INVALID_NETWORKED_EHANDLE_VALUE) return;
  const int index = encoded & ((1 << MAX_EDICT_BITS) - 1);
  const int serial = encoded >> MAX_EDICT_BITS;
  CBaseEntity *entity = cl_entitylist->GetBaseEntity(index);
  if (!entity || (entity->GetRefEHandle().GetSerialNumber() &
      ((1 << NUM_NETWORKED_EHANDLE_SERIAL_NUMBER_BITS) - 1)) != serial || !entity->IsWeapon()) return;
  const int top = lua_gettop(L);
  CBaseEntity::PushLuaInstanceSafe(L,entity);
  lua_getfield(L,-1,method);
  if (lua_isfunction(L,-1)) {
    lua_pushvalue(L,-2);
    lua_pushstring(L,data);
    luasrc_pcall(L,2,0,0);
  }
  lua_settop(L,top);
}
// Hooked in luasrc_openlibs' client tail (usermessages->HookMessage).
#endif // CLIENT_DLL

#ifndef CLIENT_DLL
// HL2SB (2026-10-08): sign-on resync for the Lua NetworkVar replication.
//
// The setter broadcasts a WRITE.  A scripted entity spawned from a map writes
// its defaults (ENT:SetupDataTables) and its map keyvalues (ENT:KeyValue)
// while the SERVER parses the level -- long before a connecting client's Lua
// state exists and hooks the "HL2SB_NW" receiver, so those messages reach no
// handler and the client keeps the type default.  GMod never has this window:
// its data tables transmit the CURRENT state of every networked variable with
// the entity snapshots.  The re-send below restores that contract on top of
// the user-message transport: when a player finishes activating, every
// entity that ever declared a NetworkVar re-transmits its current values to
// that one player.  (A client joining a running dedicated server needs the
// same re-transmission -- its signon is the first moment it can receive.)
//
// The declared type of each variable is recorded beside the accessors as
// "__hl2sb_nwt_<name>" (HL2SB_Lua_EntityNetworkVar) -- a stored Lua number
// alone cannot separate Float from Int on the walk out.
static CUtlVector< EHANDLE > s_NWEntityRegistry;

// Value guards for the resend: the serializers raise on a type mismatch and
// this runs outside any pcall, so test before serialising.
static bool HL2SB_NWValueMatchesTag (lua_State *L, int iValue, char tag) {
  switch ( tag ) {
    case HL2SB_NWTAG_FLOAT:
    case HL2SB_NWTAG_INT:		return lua_isnumber( L, iValue ) ? true : false;
    case HL2SB_NWTAG_VECTOR:	return luaL_testudata( L, iValue, "Vector" ) != NULL;
    case HL2SB_NWTAG_ANGLE:		return luaL_testudata( L, iValue, "Angle" ) != NULL;
    case HL2SB_NWTAG_BOOL:		return lua_isboolean( L, iValue ) ? true : false;
    case HL2SB_NWTAG_STRING:	return ( lua_isstring( L, iValue ) ) ? true : false;
    case HL2SB_NWTAG_ENTITY:	return ( lua_toentity( L, iValue ) != NULL );
    default:					return false;
  }
}

// Append "<name>=<value>" to szDetail for the painted-sky precision line.
static void HL2SB_NWDetailAppend (char *szDetail, size_t nMax, lua_State *L,
				  const char *pszName, int iValue, char tag) {
  char szOne[ 160 ];
  szOne[ 0 ] = '\0';
  switch ( tag ) {
    case HL2SB_NWTAG_VECTOR:
    {
      Vector *v = ( Vector * )luaL_testudata( L, iValue, "Vector" );
      if ( v != NULL ) {
        Q_snprintf( szOne, sizeof( szOne ), "%s=(%.3f %.3f %.3f) ", pszName, v->x, v->y, v->z );
      }
      break;
    }
    case HL2SB_NWTAG_ANGLE:
    {
			QAngle *a = ( QAngle * )luaL_testudata( L, iValue, "Angle" );
      if ( a != NULL ) {
        Q_snprintf( szOne, sizeof( szOne ), "%s=(%.2f %.2f %.2f) ", pszName, a->x, a->y, a->z );
      }
      break;
    }
    case HL2SB_NWTAG_FLOAT:
      Q_snprintf( szOne, sizeof( szOne ), "%s=%.3f ", pszName, ( float )lua_tonumber( L, iValue ) );
      break;
    case HL2SB_NWTAG_INT:
      Q_snprintf( szOne, sizeof( szOne ), "%s=%d ", pszName, ( int )lua_tonumber( L, iValue ) );
      break;
    case HL2SB_NWTAG_BOOL:
      Q_snprintf( szOne, sizeof( szOne ), "%s=%d ", pszName, lua_toboolean( L, iValue ) ? 1 : 0 );
      break;
    case HL2SB_NWTAG_STRING:
    {
      const char *psz = lua_tostring( L, iValue );
      Q_snprintf( szOne, sizeof( szOne ), "%s='%s' ", pszName, ( psz != NULL ) ? psz : "" );
      break;
    }
    default:
      Q_snprintf( szOne, sizeof( szOne ), "%s ", pszName );
      break;
  }
  if ( szOne[ 0 ] != '\0' )
    Q_strncat( szDetail, szOne, nMax, COPY_ALL_CHARACTERS );
}

void HL2SB_NWResyncClient (CBasePlayer *pPlayer)
{
  if ( pPlayer == NULL || L == NULL )
    return;

  const int iBase = lua_gettop( L );

  lua_getglobal(L, "__HL2SB_GLOBALS");
  if (lua_istable(L, -1)) {
    lua_pushnil(L);
    while (lua_next(L, -2)) {
      if (lua_type(L, -2) == LUA_TSTRING) HL2SB_GlobalSend(L, lua_tostring(L, -2), -1, pPlayer);
      lua_pop(L, 1);
    }
  }
  lua_settop(L, iBase);

  for ( int i = 0; i < s_NWEntityRegistry.Count(); ++i ) {
    CBaseEntity *pEnt = s_NWEntityRegistry[ i ];
    if ( pEnt == NULL || pEnt->entindex() <= 0 )
      continue;

    CBaseEntity::PushLuaInstanceSafe( L, pEnt );              // [inst]
    const int iTable = HL2SB_NWPushStorageTable( L, -1 );     // [inst, table]
    if ( iTable == 0 ) {
      lua_settop( L, iBase );
      continue;
    }

    // The painted-sky driver shows its palette (the decisive signal that the
    // map keyvalues reached the server-side store); every other entity just
    // reports a count.
    const bool bDetail = ( Q_stristr( pEnt->GetClassname(), "skypaint" ) != NULL );
    char szDetail[ 1024 ];
    szDetail[ 0 ] = '\0';
    int nSent = 0;

    lua_pushnil( L );                                         // [inst, table, key]
    while ( lua_next( L, iTable ) != 0 ) {                    // [.., key, value]
      const char *pszKey = lua_tostring( L, -2 );
      if ( pszKey != NULL && Q_strnicmp( pszKey, "__hl2sb_nw_", 11 ) == 0 ) {
        const char *pszName = pszKey + 11;
        char szTypeKey[ 160 ];
        Q_snprintf( szTypeKey, sizeof( szTypeKey ), "__hl2sb_nwt_%s", pszName );
        lua_pushstring( L, szTypeKey );
        lua_rawget( L, iTable );                              // [.., key, value, type]
        const char *pszType = lua_tostring( L, -1 );
        const char tag = ( pszType != NULL ) ? HL2SB_NWTagForType( pszType ) : 0;
        const int iValue = lua_gettop( L ) - 1;               // absolute, under the type
        if ( tag != 0 && HL2SB_NWValueMatchesTag( L, iValue, tag ) ) {
          unsigned char payload[ 512 ];
          int len = HL2SB_NWSerialize( L, iValue, tag, payload, sizeof( payload ) );
          if ( len >= 0 ) {
            CSingleUserRecipientFilter filter( pPlayer );
            filter.MakeReliable();
            UserMessageBegin( filter, "HL2SB_NW" );
              WRITE_SHORT( pEnt->entindex() );
              WRITE_BYTE( ( unsigned char )HL2SB_NWFAMILY_NETWORKVAR );
              WRITE_STRING( pszName );
              WRITE_BYTE( ( unsigned char )tag );
              WRITE_BYTE( ( unsigned char )len );
              for ( int b = 0; b < len; ++b )
                WRITE_BYTE( payload[ b ] );
            MessageEnd();
            ++nSent;
            if ( bDetail )
              HL2SB_NWDetailAppend( szDetail, sizeof( szDetail ), L, pszName, iValue, tag );
          }
        }
        lua_pop( L, 1 );                                      // type
      }
      lua_pop( L, 1 );                                        // value; key stays for lua_next
    }

    lua_settop( L, iBase );

    Msg( "[HL2SB] NW resync %s (ent %d): %d variable(s) %s\n",
         pEnt->GetClassname(), pEnt->entindex(), nSent, szDetail );
  }

  // HL2SB (2026-10-11, issue #47): the SetNW*/SetNW2* families live in the two
  // C++ dicts, not in entity Lua tables, so they need their own walk.  Only
  // entries whose tag was ever Set go out (tag 0 = never written).
  for ( int iFamily = HL2SB_NWFAMILY_NW; iFamily <= HL2SB_NWFAMILY_NW2; ++iFamily ) {
    CUtlDict<HL2SB_NWValue, unsigned short> &dict =
        ( iFamily == HL2SB_NWFAMILY_NW2 ) ? g_HL2SB_NW2Vars : g_HL2SB_NWVars;

    int nSent = 0;
    for ( unsigned short idx = dict.First(); dict.IsValidIndex( idx ); idx = dict.Next( idx ) ) {
      const HL2SB_NWValue &v = dict[ idx ];
      if ( v.tag == 0 )
        continue;

      // Key is "<entindex>_<name>" -- the name is everything after the FIRST
      // underscore (names may contain underscores themselves).
      char szScratch[ 256 ];
      Q_strncpy( szScratch, dict.GetElementName( idx ), sizeof( szScratch ) );
      char *pszUnderscore = strchr( szScratch, '_' );
      if ( pszUnderscore == NULL )
        continue;
      *pszUnderscore = '\0';
      const int entindex = atoi( szScratch );
      const char *pszName = pszUnderscore + 1;
      if ( entindex <= 0 || pszName[ 0 ] == '\0' )
        continue;

      CBaseEntity *pEnt = UTIL_EntityByIndex( entindex );
      if ( pEnt == NULL )
        continue;

      // Same payload layout HL2SB_NWSerialize puts on the wire.
      unsigned char payload[ 512 ];
      int len = -1;
      bf_write w( "HL2SB_NW", payload, sizeof( payload ) );
      switch ( v.tag ) {
        case 'N':
          len = 0;
          break;
        case HL2SB_NWTAG_FLOAT:
          w.WriteFloat( v.f );
          len = w.GetNumBytesWritten();
          break;
        case HL2SB_NWTAG_VECTOR:
          w.WriteFloat( v.v.x ); w.WriteFloat( v.v.y ); w.WriteFloat( v.v.z );
          len = w.GetNumBytesWritten();
          break;
        case HL2SB_NWTAG_ANGLE:
          w.WriteFloat( v.a.x ); w.WriteFloat( v.a.y ); w.WriteFloat( v.a.z );
          len = w.GetNumBytesWritten();
          break;
        case HL2SB_NWTAG_BOOL:
          w.WriteByte( v.b ? 1 : 0 );
          len = w.GetNumBytesWritten();
          break;
        case HL2SB_NWTAG_INT:
          w.WriteLong( v.i );
          len = w.GetNumBytesWritten();
          break;
        case HL2SB_NWTAG_STRING:
        {
          int n = v.s.Length();
          if ( n > 255 )
            n = 255;
          w.WriteByte( n );
          if ( n > 0 )
            w.WriteBytes( v.s.Get(), n );
          len = w.GetNumBytesWritten();
          break;
        }
        case HL2SB_NWTAG_ENTITY:
          w.WriteShort( v.hEnt.Get() ? v.hEnt.Get()->entindex() : 0 );
          len = w.GetNumBytesWritten();
          break;
        default:
          continue;
      }
      if ( len < 0 || len > 255 )
        continue;

      CSingleUserRecipientFilter filter( pPlayer );
      filter.MakeReliable();
      UserMessageBegin( filter, "HL2SB_NW" );
        WRITE_SHORT( entindex );
        WRITE_BYTE( ( unsigned char )iFamily );
        WRITE_STRING( pszName );
        WRITE_BYTE( ( unsigned char )v.tag );
        WRITE_BYTE( ( unsigned char )len );
        for ( int b = 0; b < len; ++b )
          WRITE_BYTE( payload[ b ] );
      MessageEnd();
      ++nSent;
    }

    Msg( "[HL2SB] NW resync family %d: %d variable(s)\n", iFamily, nSent );
  }

  lua_settop( L, iBase );		// unconditional restore
}
#endif // CLIENT_DLL

static int HL2SB_Lua_EntityNetworkVarSet (lua_State *L) {
  // Normalise the arguments to self, value (so the notify call below can build
  // its own frame regardless of what the caller passed).
  lua_settop( L, 2 );

  const char *pszType = lua_tostring( L, lua_upvalueindex( 1 ) );
  const char *pszKey = lua_tostring( L, lua_upvalueindex( 2 ) );
  const char *pszName = ( pszKey != NULL ) ? ( pszKey + strlen( "__hl2sb_nw_" ) ) : "";
  const HL2SB_NWStorage_t storage = ( HL2SB_NWStorage_t )lua_tointeger( L, lua_upvalueindex( 4 ) );

  const int iTable = HL2SB_NWPushStorageTable( L, 1 );   // [self, value, table?]

  // GMod fires NetworkVarNotify( name, fn ) as fn( name, old, new ); the old
  // value is read first, before the store below overwrites it.
  HL2SB_NWReadValue( L, 1, iTable, pszKey, storage, pszType );   // [.., old]

  HL2SB_NWStoreValue( L, 1, iTable, pszKey, storage, pszType );  // [.., new]

  const int iNew = lua_gettop( L );
  const int iOld = iNew - 1;

  if ( iTable != 0 ) {
    lua_pushstring( L, "__hl2sb_nwn" );                    // [.., key]
    lua_rawget( L, iTable );                               // [.., notify]
    if ( lua_istable( L, -1 ) ) {
      lua_pushstring( L, pszName );
      lua_rawget( L, -2 );                                 // [.., notify, fn]
      if ( lua_isfunction( L, -1 ) ) {
        lua_pushvalue( L, 1 );                             // self
        lua_pushstring( L, pszName );                      // name
        lua_pushvalue( L, iOld );                          // old
        lua_pushvalue( L, iNew );                          // new
        lua_call( L, 4, 0 );                               // [.., notify]
      } else {
        lua_pop( L, 1 );                                   // [.., notify]
      }
    }

    lua_pop( L, 1 );                                       // [.., table, old, new]
    lua_remove( L, iTable );                               // [self, value, old, new]
  }
#ifndef CLIENT_DLL
  // HL2SB (2026-09-21): REPLICATE.  Broadcast the new value to all clients;
  // the client handler stores it and the getter falls back to it when the
  // local (per-realm) storage has no value.  sent_ball's random BallColor is
  // the canonical case: rolled server-side in ENT:Initialize, read client-side
  // in ENT:Draw.
  const char *pszValueType = lua_tostring( L, lua_upvalueindex( 1 ) );
  char tag = HL2SB_NWTagForType( pszValueType );
  if ( tag != 0 && iTable != 0 ) {
    CBaseEntity *pSelf = lua_toentity( L, 1 );
    if ( pSelf == NULL && lua_istable( L, 1 ) ) {
      // HL2SB (2026-10-08): SetupDataTables runs with the entity's SCRIPT
      // TABLE as self (basescripted.cpp seeding), so the GMod-verbatim
      // defaults it sets -- env_skypaint's palette -- carried no entity and
      // this broadcast silently skipped, leaving the client on the type
      // default.  Resolve the owner through the table's .Entity field.
      lua_getfield( L, 1, "Entity" );
      pSelf = lua_toentity( L, -1 );
      lua_pop( L, 1 );
    }
    if ( pSelf != NULL && pSelf->entindex() > 0 ) {
      unsigned char payload[ 512 ];
      int len = HL2SB_NWSerialize( L, 2, tag, payload, sizeof( payload ) );
      if ( len >= 0 ) {
        // (Registered at openlibs on both realms -- see the client tail of
        // luasrc_openlibs.  A lazy first-use registration landed after the
        // local client's signon and the engine dropped every message.)

        CReliableBroadcastRecipientFilter filter;
        UserMessageBegin( filter, "HL2SB_NW" );
          WRITE_SHORT( pSelf->entindex() );
          WRITE_BYTE( ( unsigned char )HL2SB_NWFAMILY_NETWORKVAR );
          WRITE_STRING( pszName );
          WRITE_BYTE( ( unsigned char )tag );
          WRITE_BYTE( ( unsigned char )len );
          for ( int i = 0; i < len; ++i )
            WRITE_BYTE( payload[ i ] );
        MessageEnd();
      }
    }
  }
#endif

  return 0;
}

// Get<name>() for one entity.  Same upvalue layout as the setter.
static int HL2SB_Lua_EntityNetworkVarGet (lua_State *L) {
  const char *pszType = lua_tostring( L, lua_upvalueindex( 1 ) );
  const char *pszKey = lua_tostring( L, lua_upvalueindex( 2 ) );
  const HL2SB_NWStorage_t storage = ( HL2SB_NWStorage_t )lua_tointeger( L, lua_upvalueindex( 4 ) );

  const int iTable = HL2SB_NWPushStorageTable( L, 1 );

#ifdef CLIENT_DLL
  // HL2SB (2026-09-25): LOCAL first, replicated fallback -- paired with the
  // receiver in __MsgFunc_HL2SB_NW, which DROPS the local entry whenever a
  // server update for the key arrives.  That pairing is the GMod DT contract:
  // a realm-local write (client prediction, or a scripted ENT:Initialize
  // default) answers immediately, and the next server update overwrites it.
  //
  // An earlier build tried replicated-first here; that fixed the shadowing but
  // made every client-predicted weapon state fight the last known server
  // value -- fast grenade throws visibly jittered the throw animation.  The
  // receiver-side clear is the precise fix: the local entry cannot outlive the
  // server update that should replace it.
  bool bLocalSet = false;
  if ( iTable != 0 ) {
    lua_pushstring( L, pszKey );
    lua_rawget( L, iTable );
    bLocalSet = !lua_isnil( L, -1 );
    lua_pop( L, 1 );
  }

  if ( !bLocalSet ) {
    CBaseEntity *pSelf = lua_toentity( L, 1 );
    if ( pSelf != NULL && pSelf->entindex() > 0 ) {
      const char *pszName = ( pszKey != NULL ) ? ( pszKey + strlen( "__hl2sb_nw_" ) ) : "";
      if ( HL2SB_NWReplicatedPushFamily( L, HL2SB_NWFAMILY_NETWORKVAR,
                                       pSelf->entindex(), pszName ) ) {
        if ( iTable != 0 )
          lua_remove( L, iTable );
        return 1;
      }
    }
  }
#endif

  HL2SB_NWReadValue( L, 1, iTable, pszKey, storage, pszType );

  if ( iTable != 0 ) {
    lua_remove( L, iTable );
  }

  return 1;
}

/*
** HL2SB: the engine console command executor behind RunConsoleCommand's final
** fallback (lua/includes/extensions/gmod_globals.lua).  GMod Lua calls
** RunConsoleCommand for engine commands all the time -- gmod_camera fires
** "jpeg" to save a screenshot -- and those are neither a Lua concommand nor a
** ConVar, so the Lua-side fallback could not run them.  Server realm: the
** line executes on the server console; client realm: on the local console,
** which is the realm "jpeg" needs.
*/
// Parse the entire command stream, including commands after separators.
int lua_IsConCommandBlocked (lua_State *L) {
  const char *pszText = luaL_checkstring(L, 1);
  static const char *const blocked[] = {
    "crosshair_setup",
    "hud_fastswitch",
    "hud_quickinfo",
    "cl_crosshairstyle",
    "cl_crosshairusealpha",
    "cl_crosshairgap",
    "cl_crosshairsize",
    "cl_crosshairthickness",
    "cl_crosshairdot",
    "cl_crosshair_t",
    "cl_crosshaircolor_r",
    "cl_crosshaircolor_g",
    "cl_crosshaircolor_b",
    "cl_crosshairalpha",
    "cl_crosshair_drawoutline",
    "cl_crosshair_outlinethickness",
    "sensitivity",
    "cl_enable_loadingurl",
    "cl_defaultweapon",
    "cl_autowepswitch",
    "fps_max",
    "fps_max_menu",
    "fps_max_nofocus",
    "engine_no_focus_sleep",
    "cl_downloadfilter",
    "lookspring",
    "lookstrafe",
    "m_customaccel",
    "m_customaccel_exponent",
    "m_customaccel_scale",
    "m_customaccel_max",
    "m_mousespeed",
    "m_mouseaccel1",
    "m_mouseaccel2",
    "m_yaw",
    "m_pitch",
    "m_forward",
    "m_side",
    "cl_software_cursor",
    "cl_allowdownload",
    "cl_allowupload",
    "cl_logofile",
    "mat_color_projection",
    "mat_disable_d3d9ex",
    "voice_enable",
    "voice_scale",
    "voice_avggain",
    "voice_maxgain",
    "voice_gain_rate",
    "voice_gain_downward_multiplier",
    "voice_gain_target",
    "voice_gain_max",
    "voice_fadeouttime",
    "voice_forcemicrecord",
    "voice_inputfromfile",
    "voice_loopback",
    "voice_modenable",
    "voice_overdrive",
    "voice_overdrivefadetime",
    "voice_recordtofile",
    "volume",
    "volume_sfx",
    "suitvolume",
    "snd_musicvolume",
    "snd_gain",
    "snd_gain_max",
    "snd_gain_min",
    "snd_mixahead",
    "snd_pitchquality",
    "snd_ducktovolume",
    "snd_duckerattacktime",
    "snd_duckerreleasetime",
    "snd_duckerthreshold",
    "snd_mute_losefocus",
    "snd_surround_speakers",
    "dsp_slow_cpu",
    "dsp_enhance_stereo",
    "fov_desired",
    "crosshair",
    "cl_chatfilters",
    "con_enable",
    "con_bgalpha",
    "con_border",
    "con_filter_enable",
    "con_filter_dupe",
    "con_filter_text",
    "con_filter_text_out",
    "gmod_delete_temp_files",
    "host_writeconfig",
    "host_writeconfig_lua",
    "mat_savechanges",
    "cl_mouseenable",
    "skill",
    "cl_playerspraydisable",
    "mp_flashlight",
    "lua_strict",
    "closecaption",
    "cc_lang",
    "cc_subtitles",
    "cc_linger_time",
    "cc_predisplay_time",
    "mp_decals",
    "muzzleflash_light",
    "r_ambientboost",
    "r_ambientmin",
    "r_ambientfraction",
    "r_ambientfactor",
    "r_lightcachemodel",
    "r_drawlightcache",
    "zoom_sensitivity_ratio",
    "cl_rumblescale",
    "m_filter",
    "cl_chatfilter_version",
    "net_maxroutable",
    "tv_nochat",
    "windows_speaker_config",
    "xbox_autothrottle",
    "xbox_steering_deadzone",
    "xbox_throttlespoof",
    "xbox_throttlebias",
    "func_break_max_pieces",
    "cl_savescreenshotstosteam",
    "texture_budget_background_alpha",
    "texture_budget_panel_bottom_of_history_fraction",
    "texture_budget_panel_height",
    "texture_budget_panel_width",
    "texture_budget_panel_x",
    "texture_budget_panel_y",
    "budget_averages_window",
    "budget_background_alpha",
    "budget_bargraph_background_alpha",
    "budget_bargraph_range_ms",
    "budget_history_numsamplesvisible",
    "budget_history_range_ms",
    "budget_panel_bottom_of_history_fraction",
    "budget_panel_height",
    "budget_panel_width",
    "budget_panel_x",
    "budget_panel_y",
    "budget_peaks_window",
    "budget_show_averages",
    "budget_show_history",
    "budget_show_peaks",
    "net_graph",
    "net_scale",
    "net_graphheight",
    "net_graphmsecs",
    "net_graphpos",
    "net_graphproportionalfont",
    "net_graphshowinterp",
    "net_graphshowlatency",
    "net_graphsolid",
    "net_graphtext",
    "mat_forcehardwaresync",
    "mat_forceaniso",
    "mat_picmip",
    "mat_trilinear",
    "mat_antialias",
    "mat_aaquality",
    "mat_colorcorrection",
    "mat_hdr_level",
    "mat_vsync",
    "mat_reducefillrate",
    "mat_reduceparticles",
    "r_shadowrendertotexture",
    "r_rootlod",
    "r_waterforceexpensive",
    "r_waterforcereflectentities",
    "mat_forcemanagedtextureintohardware",
    "mat_managedtextures",
    "mat_monitorgamma",
    "mat_monitorgamma_tv_enabled",
    "mat_powersavingsmode",
    "mat_requires_rt_alloc_first",
    "mat_supportflashlight",
    "r_worldlights",
    "r_teeth",
    "r_shader_srgb",
    "mat_dump_rts",
    "sv_cheats",
    "mat_setvideomode",
    "mat_viewportscale",
    "mat_viewportupscale",
    "connect",
    "gm_video",
    "cancelselect",
    "gmod_language",
    "cef_credits",
    "gmod_tos",
    "gmod_privacy",
    "gmod_modding",
    "gmod_servers",
    "whereis",
    "path",
    "fs_tellmeyoursecrets",
    "fs_warning_level",
    "fs_printopenfiles ",
    "fs_report_sync_opens",
    "fs_monitor_read_from_pack",
    "gm_demo_to_video",
    "mem_force_flush",
    "scene_flush",
    "startdemos",
    "nextdemo",
    "startmovie",
    "playdemo",
    "editdemo",
    "timedemo",
    "spike",
    "timedemoquit",
    "demo_quitafterplayback",
    "contimes",
    "con_drawnotify",
    "developer",
    "exec",
    "bind",
    "bind_mac",
    "bindtoggle",
    "unbind",
    "unbind_mac",
    "unbindall",
    "alias",
    "ent_fire",
    "ent_setname",
    "name",
    "quit",
    "quti",
    "exit",
    "killserver",
    "lua_run",
    "lua_run_cl",
    "lua_open",
    "lua_cookieclear",
    "lua_showerrors_cl",
    "lua_showerrors_sv",
    "lua_openscript",
    "lua_openscript_cl",
    "lua_redownload",
    "lua_reloadents",
    "lua_reloadents_cl",
    "lua_error_url",
    "gamemode_reload",
    "gamemode_reload_cl",
    "clear",
    "rcon_password",
    "rcon_address",
    "echo",
    "test_randomchance",
    "test_startscript",
    "toggle",
    "incrementvar",
    "multvar",
    "gameui_hide",
    "gameui_preventescapetoshow",
    "mat_texture_limit",
    "mat_crosshair_explorer",
    "mat_dxlevel",
    "toggleconsole",
    "showconsole",
    "hideconsole",
    "plugin_load",
    "sv_logsdir",
    "sv_logfilename_format",
    "sv_log_storetime",
    "logaddress_add",
    "log",
    "askconnect_accept",
    "lightprobe",
    "buildcubemaps",
    "gameui_show_dialog",
    "menu_reload",
    "debug_dump",
    "servercfgfile",
    "lservercfgfile",
    "+voicerecord",
    "mat_texture_list_txlod_sync",
    "building_cubemaps",
    "setinfo",
    "cl_ignorepackets",
    "vcr_verbose",
    "tv_relay",
    "tv_retry",
    "+mat_texture_list",
    "mat_texture_list",
    "mat_texture_list_content_path",
    "mat_crosshair_edit",
    "mat_crosshair_explorer",
    "startupmenu",
    "movie_fixwave",
    "cl_cloud_settings",
    "stuffcmds",
    "mat_texture_save_fonts",
    "load",
    "physics_budget",
    "physics_highlight_active",
    "physics_report_active",
    "physics_constraints",
    "physics_debug_entity",
    "physics_select",
    "surfaceprop",
    "cl_particle_stats_start",
    "cl_particle_stats_stop",
    "cl_particle_stats_trigger_count",
    "filesystem_unbuffered_io",
    "filesystem_buffer_size",
    "filesystem_native",
    "filesystem_max_stdio_read",
    "filesystem_report_buffered_io",
    "hud_autoreloadscript",
    "hud_freezecamhide",
    "hud_reloadscheme",
    "mat_bufferprimitives",
    "sdl_displayindex",
    "snd_buildcache",
    "__screenshot_internal",
    "devshots_screenshot",
    "mem_max_heapsize",
    "datacachesize",
    "con_logfile",
    "vprof_record_start",
    "vprof_record_stop",
    "test_loop",
    "plugin_load",
    "sv_allow_wait_command",
  };
  CCommandBuffer buffer;
  buffer.SetWaitEnabled(false);
  buffer.AddText(pszText);
  buffer.BeginProcessingCommands(1);
  bool bBlocked = false;
  while ( buffer.DequeueNextCommand() ) {
    const CCommand &args = buffer.GetCommand();
    if ( args.ArgC() == 0 )
      continue;
    if ( (!gpGlobals || gpGlobals->maxClients < 2) && !Q_stricmp(args[0], "retry") )
      bBlocked = true;
    for ( int i = 0; i < ARRAYSIZE(blocked) && !bBlocked; ++i )
      if ( !Q_stricmp(args[0], blocked[i]) )
        bBlocked = true;
    for ( int i = 1; i < args.ArgC() && !bBlocked; ++i )
      if ( !Q_stricmp(args[i], "con_logfile") || !Q_stricmp(args[i], "quitnoconfirm") )
        bBlocked = true;
  }
  buffer.EndProcessingCommands();
  lua_pushboolean(L, bBlocked);
  return 1;
}

static int HL2SB_Lua_EngineCommand (lua_State *L) {
  const char *pszCommand = luaL_checkstring( L, 1 );
  if ( pszCommand == NULL || pszCommand[ 0 ] == '\0' )
    return 0;
#ifndef CLIENT_DLL
  engine->ServerCommand( pszCommand );
#else
  engine->ClientCmd( pszCommand );
#endif
  return 0;
}

static int HL2SB_Lua_RunConsoleCommand( lua_State *L )
{
  const char *name = luaL_checkstring( L, 1 );
  for ( const unsigned char *p = (const unsigned char *)name; *p; ++p )
  {
    if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
            ( *p >= '0' && *p <= '9' ) || *p == '!' || *p == '+' || *p == '-' ||
            *p == '.' || *p == '^' || *p == '_' || *p == '~' ) )
      return luaL_error( L, "RunConsoleCommand: Command has invalid characters! (%s)\n\tThe first parameter of this function should contain only the command, the second parameter should contain arguments.", name );
  }
  if ( Q_strlen( name ) < 2 )
    return luaL_error( L, "RunConsoleCommand: Command is too short, bailing! (%s)", name );

  char command[1024];
  Q_strncpy( command, name, sizeof( command ) );
  for ( int index = 2; index < 64 && index <= lua_gettop( L ); ++index )
  {
    const int type = lua_type( L, index );
    const char *value = lua_tostring( L, index );
    if ( !value ) break;
    if ( !Q_stricmp( value, "con_logfile" ) || !Q_stricmp( value, "quitnoconfirm" ) )
      return luaL_error( L, "RunConsoleCommand: Command argument is blocked! (%s %s)", name, value );

    char number[1024];
    if ( type == LUA_TNUMBER )
    {
      Q_snprintf( number, sizeof( number ), "%.2f", (double)lua_tonumber( L, index ) );
      value = number;
    }
    char argument[512];
    int length = 0;
    while ( value[length] && length < (int)sizeof( argument ) - 1 )
    {
      argument[length] = value[length] == '"' ? '\'' : ( value[length] == '\n' ? ' ' : value[length] );
      ++length;
    }
    argument[length] = '\0';
    Q_strncat( command, " \"", sizeof( command ) );
    Q_strncat( command, argument, sizeof( command ) );
    Q_strncat( command, "\"", sizeof( command ) );
  }
  Q_strncat( command, ";", sizeof( command ) );

  lua_pushcfunction( L, lua_IsConCommandBlocked );
  lua_pushstring( L, command );
  lua_call( L, 1, 1 );
  const bool blocked = lua_toboolean( L, -1 ) != 0;
  lua_pop( L, 1 );
  if ( blocked )
    return luaL_error( L, "RunConsoleCommand: Command is blocked! (%s)", name );
#ifdef CLIENT_DLL
  engine->ClientCmd( command );
#else
  engine->ServerCommand( command );
#endif
  return 0;
}

void HL2SB_RegisterConsoleGlobals( lua_State *state )
{
  lua_pushcfunction( state, lua_IsConCommandBlocked );
  lua_setglobal( state, "IsConCommandBlocked" );
  lua_pushcfunction( state, HL2SB_Lua_EngineCommand );
  lua_setglobal( state, "HL2SB_EngineCommand" );
  lua_pushcfunction( state, HL2SB_Lua_RunConsoleCommand );
  lua_setglobal( state, "RunConsoleCommand" );
}

// self:NetworkVar( type, slot, name [, options] )
static int HL2SB_Lua_EntityNetworkVar (lua_State *L) {
  const char *pszType = luaL_checkstring( L, 2 );
  const int iSlot = ( int )luaL_checknumber( L, 3 );
  const char *pszName = luaL_checkstring( L, 4 );

  // The accessors have to end up on the entity's Lua TABLE: that is what
  // CBaseEntity___index falls back to when a script calls Set<name>() on the
  // entity userdata (lbaseentity_shared.cpp:2196).
  const int iTable = HL2SB_NWPushStorageTable( L, 1 );
  if ( iTable == 0 ) {
    luaL_argerror( L, 1, "entity or entity table expected" );
  }

  char szKey[ 160 ];
  Q_snprintf( szKey, sizeof( szKey ), "__hl2sb_nw_%s", pszName );

  const HL2SB_NWStorage_t storage = HL2SB_NWStorageForName( pszName );

  char szSetter[ 160 ];
  char szGetter[ 160 ];
  Q_snprintf( szSetter, sizeof( szSetter ), "Set%s", pszName );
  Q_snprintf( szGetter, sizeof( szGetter ), "Get%s", pszName );

  lua_pushstring( L, pszType );
  lua_pushstring( L, szKey );
  lua_pushinteger( L, iSlot );
  lua_pushinteger( L, ( int )storage );

  lua_pushcclosure( L, HL2SB_Lua_EntityNetworkVarSet, 4 );
  lua_setfield( L, iTable, szSetter );

  lua_pushstring( L, pszType );
  lua_pushstring( L, szKey );
  lua_pushinteger( L, iSlot );
  lua_pushinteger( L, ( int )storage );

  lua_pushcclosure( L, HL2SB_Lua_EntityNetworkVarGet, 4 );
  lua_setfield( L, iTable, szGetter );

  // HL2SB (2026-10-08): record the DECLARED type beside the accessors.  The
  // sign-on resync (HL2SB_NWResyncClient) walks the raw storage keys and must
  // serialise each value with the wire tag its declaration promised -- a Lua
  // number alone cannot separate Float from Int.
  char szTypeKey[ 160 ];
  Q_snprintf( szTypeKey, sizeof( szTypeKey ), "__hl2sb_nwt_%s", pszName );
  lua_pushstring( L, pszType );
  lua_setfield( L, iTable, szTypeKey );

#ifndef CLIENT_DLL
  // Register the entity for the sign-on resync.  SetupDataTables can run with
  // the entity's script TABLE as self, so resolve the owner the same way the
  // setter's broadcast does.
  {
    CBaseEntity *pOwner = lua_toentity( L, 1 );
    if ( pOwner == NULL && lua_istable( L, 1 ) ) {
      lua_getfield( L, 1, "Entity" );
      pOwner = lua_toentity( L, -1 );
      lua_pop( L, 1 );
    }
    if ( pOwner != NULL && pOwner->entindex() > 0 ) {
      bool bKnown = false;
      for ( int i = 0; i < s_NWEntityRegistry.Count(); ++i ) {
        if ( s_NWEntityRegistry[ i ] == pOwner ) {
          bKnown = true;
          break;
        }
      }
      if ( !bKnown )
        s_NWEntityRegistry.AddToTail( pOwner );
    }
  }
#endif

  lua_remove( L, iTable );
  return 0;
}

// self:NetworkVarNotify( name, fn )  -- SERVER in GMod; callable anywhere here.
static int HL2SB_Lua_EntityNetworkVarNotify (lua_State *L) {
  const char *pszName = luaL_checkstring( L, 2 );
  luaL_checktype( L, 3, LUA_TFUNCTION );

  const int iTable = HL2SB_NWPushStorageTable( L, 1 );
  if ( iTable == 0 ) {
    // Nothing to attach the callback to (no entity / no scripted table); the
    // variable then simply never notifies, which is what the SWEP shim does.
    return 0;
  }

  lua_pushstring( L, "__hl2sb_nwn" );       // [.., key]
  lua_rawget( L, iTable );                  // [.., notify]
  if ( !lua_istable( L, -1 ) ) {
    lua_pop( L, 1 );
    lua_newtable( L );                      // [.., notify]
    lua_pushstring( L, "__hl2sb_nwn" );     // [.., notify, key]
    lua_pushvalue( L, -2 );                 // [.., notify, key, notify]
    lua_rawset( L, iTable );                // [.., notify]
  }

  lua_pushvalue( L, 3 );                    // [.., notify, fn]
  lua_setfield( L, -2, pszName );           // notify[ name ] = fn
  lua_pop( L, 1 );                          // [..]
  lua_remove( L, iTable );
  return 0;
}

//-----------------------------------------------------------------------------
// HL2SB: the caller's script file, GMod style.
//
// The reference's ILuaInterface::GetCurrentFile walks the LIVE Lua stack (frames
// 1..9) and answers with the first source that starts with '@', minus the '@'.
// That is what makes an argument-less AddCSLuaFile() name the file that is
// running, and what a relative name resolves against.
//-----------------------------------------------------------------------------
static bool HL2SB_LuaCurrentFile (lua_State *pL, char *pszOut, int nOut)
{
  lua_Debug ar;
  for ( int iLevel = 1; iLevel <= 9; ++iLevel )
  {
    if ( lua_getstack( pL, iLevel, &ar ) == 0 )
      break;
    lua_getinfo( pL, "S", &ar );
    if ( ar.source != NULL && ar.source[0] == '@' )
    {
      Q_strncpy( pszOut, ar.source + 1, nOut );
      return true;
    }
  }
  if ( nOut > 0 )
    pszOut[0] = '\0';
  return false;
}

//-----------------------------------------------------------------------------
// HL2SB: raise one of AddCSLuaFile's errors.  The reference reports through
// ILuaInterface::ErrorFromLua, which shows the message as written and then the
// stack; raising a plain string (instead of luaL_error, which prefixes
// "file:line:") keeps the message byte-identical to the reference.
//-----------------------------------------------------------------------------
static int HL2SB_CSRaise (lua_State *pL, const char *pszFmt, const char *pszArg)
{
  char szMsg[ MAX_PATH + 128 ];
  Q_snprintf( szMsg, sizeof( szMsg ), pszFmt, pszArg );
  lua_pushstring( pL, szMsg );
  return lua_error( pL );
}

//-----------------------------------------------------------------------------
// HL2SB: collapse "a/./b" and "a/../b" -- the literal composed path is not
// something the engine filesystem opens (same rule include() uses).
//-----------------------------------------------------------------------------
static void HL2SB_CSNormalizeDots (const char *pszIn, char *pszOut, int nOutSize)
{
  int nOut = 0;
  const char *p = pszIn;
  while ( *p && nOut < nOutSize - 2 )
  {
    if ( p[0] == '.' && ( p[1] == '/' || p[1] == '\\' ) ) { p += 2; continue; }
    if ( p[0] == '.' && p[1] == '.' && ( p[2] == '/' || p[2] == '\\' ) )
    {
      while ( nOut > 0 && pszOut[nOut - 1] != '/' && pszOut[nOut - 1] != '\\' ) --nOut;
      if ( nOut > 0 ) --nOut;
      p += 3;
      continue;
    }
    pszOut[nOut++] = *p++;
  }
  pszOut[nOut] = '\0';
}

// UI storage uses the same KeyValues files as the Sandbox content scripts.
static void HL2SB_UIPushKV(lua_State *L, KeyValues *pKV, int depth = 0)
{
  lua_newtable(L);
  if (depth >= 64) return;
  for (KeyValues *p = pKV->GetFirstSubKey(); p; p = p->GetNextKey()) {
    const char *name = p->GetName();
    char *end = NULL;
    long index = strtol(name, &end, 10);
    if (*name && end && !*end && index > 0) lua_pushinteger(L, index);
    else lua_pushstring(L, name);
    if (p->GetDataType() == KeyValues::TYPE_NONE) HL2SB_UIPushKV(L, p, depth + 1);
    else {
      const char *value = p->GetString();
      double number = strtod(value, &end);
      if (*value && end && !*end && IsFinite(number)) lua_pushnumber(L, number);
      else lua_pushstring(L, value);
    }
    lua_rawset(L, -3);
  }
}

static void HL2SB_UIBuildKV(lua_State *L, int table, KeyValues *pKV, int depth = 0)
{
  table = lua_absindex(L, table);
  if (depth >= 64) return;
  lua_pushnil(L);
  while (lua_next(L, table)) {
    lua_pushvalue(L, -2);
    const char *key = lua_tostring(L, -1);
    if (key) {
      KeyValues *p = pKV->FindKey(key, true);
      if (lua_istable(L, -2)) HL2SB_UIBuildKV(L, -2, p, depth + 1);
      else if (lua_isboolean(L, -2)) p->SetInt(NULL, lua_toboolean(L, -2));
      else if (lua_isstring(L, -2)) p->SetString(NULL, lua_tostring(L, -2));
    }
    lua_pop(L, 2);
  }
}

// Validate before allocating native trees so Lua errors cannot leak them.
static bool HL2SB_ValidateKVTable(lua_State *L, int table, CUtlVector<const void *> &ancestors)
{
  table = lua_absindex(L, table);
  const void *identity = lua_topointer(L, table);
  if (ancestors.Find(identity) != ancestors.InvalidIndex() || ancestors.Count() >= 256)
    return false;
  ancestors.AddToTail(identity);
  lua_pushnil(L);
  while (lua_next(L, table))
  {
    if (lua_istable(L, -2)) { lua_pop(L, 2); ancestors.Remove(ancestors.Count() - 1); return false; }
    if (lua_istable(L, -1) && !HL2SB_ValidateKVTable(L, -1, ancestors))
    { lua_pop(L, 2); ancestors.Remove(ancestors.Count() - 1); return false; }
    lua_pop(L, 1);
  }
  ancestors.Remove(ancestors.Count() - 1);
  return true;
}

static void HL2SB_BuildUtilityKV(lua_State *L, int table, KeyValues *kv)
{
  table = lua_absindex(L, table);
  lua_pushnil(L);
  while (lua_next(L, table))
  {
    const char *key = luaL_tolstring(L, -2, NULL);
    if (key)
    {
      // New children preserve separately named entries during serialization.
      KeyValues *child = new KeyValues(key);
      child->UsesEscapeSequences(true);
      kv->AddSubKey(child);
      if (lua_istable(L, -2)) HL2SB_BuildUtilityKV(L, -2, child);
      else
      {
        const char *value = luaL_tolstring(L, -2, NULL);
        child->SetString(NULL, value);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 2);
  }
}

static int HL2SB_BuildUtilityKVProtected(lua_State *L)
{
  HL2SB_BuildUtilityKV(L, 1, static_cast<KeyValues *>(lua_touserdata(L, lua_upvalueindex(1))));
  return 0;
}

static int HL2SB_UITableToKeyValues(lua_State *L)
{
  luaL_checktype(L, 1, LUA_TTABLE);
  const char *root = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : "TableToKeyValues";
  bool valid;
  {
    CUtlVector<const void *> ancestors;
    valid = HL2SB_ValidateKVTable(L, 1, ancestors);
  }
  if (!valid)
    return luaL_error(L, "Cannot convert recursive or invalid table to KeyValues");
  KeyValues *kv = new KeyValues(root);
  kv->UsesEscapeSequences(true);
  lua_pushlightuserdata(L, kv);
  lua_pushcclosure(L, HL2SB_BuildUtilityKVProtected, 1);
  lua_pushvalue(L, 1);
  if (lua_pcall(L, 1, 0, 0) != LUA_OK)
  {
    kv->deleteThis();
    return lua_error(L);
  }
  CUtlBuffer buffer(0, 0, CUtlBuffer::TEXT_BUFFER);
  kv->RecursiveSaveToFile(buffer, 0);
  lua_pushlstring(L, (const char *)buffer.Base(), buffer.TellPut());
  kv->deleteThis();
  return 1;
}

// Keep original key spelling locally; the engine symbol pool folds key case.
static bool HL2SB_KVToken(const char *&text, CUtlString &token, bool escapes, bool &brace)
{
  token = "";
  brace = false;
  for (;;)
  {
    while (*text && (unsigned char)*text <= ' ') ++text;
    if (text[0] != '/' || text[1] != '/') break;
    while (*text && *text != '\n') ++text;
  }
  if (!*text) return false;
  if (*text == '{' || *text == '}')
  {
    char value[2] = {*text++, 0}; token = value; brace = true; return true;
  }
  const bool quoted = *text == '"';
  if (quoted) ++text;
  while (*text)
  {
    char c = *text;
    if (quoted ? c == '"' : ((unsigned char)c <= ' ' || c == '{' || c == '}')) break;
    ++text;
    if (quoted && escapes && c == '\\' && *text)
    {
      c = *text++;
      if (c == 'n') c = '\n';
      else if (c == 't') c = '\t';
      else if (c != '"' && c != '\\') break;
    }
    char value[2] = {c, 0}; token += value;
  }
  if (quoted && *text == '"') ++text;
  return true;
}

static bool HL2SB_KVOriginalNames(const char *&text, KeyValues *kv,
    CUtlVector<CUtlString> &names, bool escapes, int depth = 0)
{
  if (depth >= 256) return false;
  for (KeyValues *child = kv->GetFirstSubKey(); child; child = child->GetNextKey())
  {
    CUtlString key, value;
    bool brace;
    if (!HL2SB_KVToken(text, key, escapes, brace) || brace || Q_stricmp(key.Get(), child->GetName()))
      return false;
    names.AddToTail(key);
    if (!HL2SB_KVToken(text, value, escapes, brace)) return false;
    if (brace && !Q_strcmp(value.Get(), "{"))
    {
      if (!HL2SB_KVOriginalNames(text, child, names, escapes, depth + 1)) return false;
    }
    else if (brace || child->GetDataType() == KeyValues::TYPE_NONE) return false;
  }
  CUtlString end;
  bool brace;
  return HL2SB_KVToken(text, end, escapes, brace) && brace && !Q_strcmp(end.Get(), "}");
}

static void HL2SB_PushUtilityKV(lua_State *L, KeyValues *kv, bool ordered, bool preserveCase,
    const CUtlVector<CUtlString> &names, int &nameIndex)
{
  lua_newtable(L);
  bool zeroNames = true;
  int count = 0;
  for (KeyValues *child = kv->GetFirstSubKey(); child; child = child->GetNextKey())
  {
    ++count;
    if (Q_strcmp(child->GetName(), "0")) zeroNames = false;
  }
  zeroNames = zeroNames && count != 1;
  int index = 1;
  for (KeyValues *child = kv->GetFirstSubKey(); child; child = child->GetNextKey(), ++index)
  {
    char key[512];
    Q_strncpy(key, nameIndex < names.Count() ? names[nameIndex].Get() : child->GetName(),
        ordered ? 256 : sizeof(key));
    ++nameIndex;
    if (!preserveCase) Q_strlower(key);
    if (ordered)
    {
      lua_newtable(L);
      if (zeroNames) Q_snprintf(key, sizeof(key), "%d", index);
      lua_pushstring(L, key);
      lua_setfield(L, -2, "Key");
    }
    if (child->GetDataType() == KeyValues::TYPE_NONE)
      HL2SB_PushUtilityKV(L, child, ordered, preserveCase, names, nameIndex);
    else if (child->GetDataType() == KeyValues::TYPE_INT)
      lua_pushnumber(L, ordered ? child->GetFloat() : child->GetInt());
    else if (child->GetDataType() == KeyValues::TYPE_FLOAT)
    {
      const float value = child->GetFloat();
      lua_pushnumber(L, ordered ? value : floorf(value * 10000.0f) * 0.0001f);
    }
    else
      lua_pushstring(L, child->GetString());
    if (ordered)
    {
      lua_setfield(L, -2, "Value");
      lua_rawseti(L, -2, index);
    }
    else
    {
      lua_pushstring(L, key);
      lua_insert(L, -2);
      lua_rawset(L, -3);
    }
  }
}

static int HL2SB_ReadUtilityKV(lua_State *L, bool ordered)
{
  const char *text = luaL_checkstring(L, 1);
  const bool escapes = lua_toboolean(L, 2) != 0;
  const bool preserveCase = lua_toboolean(L, 3) != 0;
  KeyValues *kv = new KeyValues(ordered ? "KeyValuesToTablePreserveOrder" : "KeyValuesToTable");
  kv->UsesEscapeSequences(escapes);
  kv->LoadFromBuffer("lua", text);
  CUtlVector<CUtlString> names;
  const char *cursor = text;
  CUtlString token;
  bool brace;
  if (!HL2SB_KVToken(cursor, token, escapes, brace) || brace ||
      !HL2SB_KVToken(cursor, token, escapes, brace) || !brace || Q_strcmp(token.Get(), "{") ||
      !HL2SB_KVOriginalNames(cursor, kv, names, escapes))
    names.Purge();
  int nameIndex = 0;
  HL2SB_PushUtilityKV(L, kv, ordered, preserveCase, names, nameIndex);
  kv->deleteThis();
  return 1;
}

static int HL2SB_UIKeyValuesToTable(lua_State *L)
{
  return HL2SB_ReadUtilityKV(L, false);
}

static int HL2SB_KeyValuesToTablePreserveOrder(lua_State *L)
{
  return HL2SB_ReadUtilityKV(L, true);
}

static int lua_ProtectedCall(lua_State *L)
{
  luaL_checktype(L, 1, LUA_TFUNCTION);
  const int arguments = lua_gettop(L) - 1;
  const int status = luasrc_pcall(L, arguments, 0, 0);
  lua_pushboolean(L, status == LUA_OK);
  return 1;
}

static int HL2SB_TimerCycle(lua_State *L)
{
  static CCycleCount previous;
  CCycleCount now, elapsed;
  now.Sample();
  CCycleCount::Sub(now, previous, elapsed);
  lua_pushnumber(L, elapsed.GetMillisecondsF());
  previous.Sample();
  return 1;
}

static int HL2SB_UIPopulateSpawnlists(lua_State *L)
{
  luaL_checktype(L, 1, LUA_TFUNCTION);
  for (int pass = 0; pass < 2; ++pass) {
    int loaded = 0;
    FileFindHandle_t handle;
    const char *name = filesystem->FindFirstEx("settings/spawnlist/*.txt", "DEFAULT_WRITE_PATH", &handle);
    while (name) {
      if (!Q_strstr(name, "Copy ")) {
        char path[MAX_PATH];
        Q_snprintf(path, sizeof(path), "settings/spawnlist/%s", name);
        KeyValues *pKV = new KeyValues("TableToKeyValues");
        if (pKV->LoadFromFile(filesystem, path, "DEFAULT_WRITE_PATH") && pKV->GetInt("version") == 3) {
          lua_pushvalue(L, 1);
          lua_pushstring(L, path);
          lua_pushstring(L, pKV->GetString("name", "Unnamed"));
          HL2SB_UIPushKV(L, pKV->FindKey("contents", true));
          lua_pushstring(L, pKV->GetString("icon"));
          lua_pushinteger(L, pKV->GetInt("id"));
          lua_pushinteger(L, pKV->GetInt("parentid"));
          lua_pushstring(L, pKV->GetString("needsapp"));
          int result = lua_pcall(L, 7, 0, 0);
          if (result != LUA_OK) {
            pKV->deleteThis();
            filesystem->FindClose(handle);
            return lua_error(L);
          }
          ++loaded;
        }
        pKV->deleteThis();
      }
      name = filesystem->FindNext(handle);
    }
    filesystem->FindClose(handle);
    if (loaded || pass) break;
    filesystem->CreateDirHierarchy("settings/spawnlist", "DEFAULT_WRITE_PATH");
    name = filesystem->FindFirstEx("settings/spawnlist_default/*.txt", "GAME", &handle);
    while (name) {
      char source[MAX_PATH], target[MAX_PATH];
      Q_snprintf(source, sizeof(source), "settings/spawnlist_default/%s", name);
      Q_snprintf(target, sizeof(target), "settings/spawnlist/%s", name);
      CUtlBuffer data;
      if (filesystem->ReadFile(source, "GAME", data)) filesystem->WriteFile(target, "DEFAULT_WRITE_PATH", data);
      name = filesystem->FindNext(handle);
    }
    filesystem->FindClose(handle);
  }
  return 0;
}

static void HL2SB_UIFileName(const char *name, char *out, int size)
{
  Q_strncpy(out, V_UnqualifiedFileName(name), size);
  int length = Q_strlen(out);
  if (length > 4 && !Q_stricmp(out + length - 4, ".txt")) out[length - 4] = 0;
  for (char *p = out; *p; ++p) if (strchr("/\\:*?\"<>|", *p)) *p = '_';
  if (!*out || !Q_strcmp(out, ".") || !Q_strcmp(out, "..")) Q_strncpy(out, "unnamed", size);
}

static int HL2SB_UISaveSpawnlists(lua_State *L)
{
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING)
      return luaL_error(L, "Spawnlists must contain filename and text pairs");
    lua_pop(L, 1);
  }
  FileFindHandle_t handle;
  const char *name = filesystem->FindFirstEx("settings/spawnlist/*.txt", "DEFAULT_WRITE_PATH", &handle);
  while (name) {
    char path[MAX_PATH]; Q_snprintf(path, sizeof(path), "settings/spawnlist/%s", name);
    filesystem->RemoveFile(path, "DEFAULT_WRITE_PATH");
    name = filesystem->FindNext(handle);
  }
  filesystem->FindClose(handle);
  filesystem->CreateDirHierarchy("settings/spawnlist", "DEFAULT_WRITE_PATH");
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    char clean[MAX_PATH], path[MAX_PATH];
    HL2SB_UIFileName(lua_tostring(L, -2), clean, sizeof(clean));
    Q_snprintf(path, sizeof(path), "settings/spawnlist/%s.txt", clean);
    size_t length; const char *text = lua_tolstring(L, -1, &length);
    CUtlBuffer data(text, length, CUtlBuffer::READ_ONLY);
    filesystem->WriteFile(path, "DEFAULT_WRITE_PATH", data);
    lua_pop(L, 1);
  }
  return 0;
}

static int HL2SB_UILoadPresets(lua_State *L)
{
  lua_newtable(L);
  FileFindHandle_t groups;
  const char *group = filesystem->FindFirstEx("settings/presets/*", "GAME", &groups);
  while (group) {
    if (filesystem->FindIsDirectory(groups) && group[0] != '.') {
      char pattern[MAX_PATH]; Q_snprintf(pattern, sizeof(pattern), "settings/presets/%s/*.txt", group);
      lua_newtable(L);
      FileFindHandle_t files;
      const char *file = filesystem->FindFirstEx(pattern, "GAME", &files);
      while (file) {
        char path[MAX_PATH]; Q_snprintf(path, sizeof(path), "settings/presets/%s/%s", group, file);
        KeyValues *pKV = new KeyValues("preset");
        if (pKV->LoadFromFile(filesystem, path, "GAME")) {
          HL2SB_UIPushKV(L, pKV);
          lua_setfield(L, -2, pKV->GetName());
        }
        pKV->deleteThis();
        file = filesystem->FindNext(files);
      }
      filesystem->FindClose(files);
      lua_setfield(L, -2, group);
    }
    group = filesystem->FindNext(groups);
  }
  filesystem->FindClose(groups);
  return 1;
}

static int HL2SB_UISavePresets(lua_State *L)
{
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    if (lua_type(L, -2) != LUA_TSTRING || !lua_istable(L, -1))
      return luaL_error(L, "Preset groups must contain names and tables");
    int group = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, group)) {
      if (lua_type(L, -2) != LUA_TSTRING || !lua_istable(L, -1))
        return luaL_error(L, "Presets must contain names and tables");
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
      char group[MAX_PATH], directory[MAX_PATH];
      HL2SB_UIFileName(lua_tostring(L, -2), group, sizeof(group));
      Q_snprintf(directory, sizeof(directory), "settings/presets/%s", group);
      filesystem->CreateDirHierarchy(directory, "DEFAULT_WRITE_PATH");
      char pattern[MAX_PATH]; Q_snprintf(pattern, sizeof(pattern), "%s/*.txt", directory);
      FileFindHandle_t handle;
      const char *file = filesystem->FindFirstEx(pattern, "DEFAULT_WRITE_PATH", &handle);
      while (file) {
        char path[MAX_PATH]; Q_snprintf(path, sizeof(path), "%s/%s", directory, file);
        filesystem->RemoveFile(path, "DEFAULT_WRITE_PATH");
        file = filesystem->FindNext(handle);
      }
      filesystem->FindClose(handle);
      int table = lua_gettop(L);
      lua_pushnil(L);
      while (lua_next(L, table)) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
          char name[MAX_PATH], path[MAX_PATH];
          HL2SB_UIFileName(lua_tostring(L, -2), name, sizeof(name));
          Q_snprintf(path, sizeof(path), "%s/%s.txt", directory, name);
          KeyValues *pKV = new KeyValues(lua_tostring(L, -2));
          HL2SB_UIBuildKV(L, -1, pKV);
          pKV->SaveToFile(filesystem, path, "DEFAULT_WRITE_PATH", false, true);
          pKV->deleteThis();
        }
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
  }
  return 0;
}

static void HL2SB_PushGlobalValues(lua_State *L) {
  lua_getglobal(L, "__HL2SB_GLOBALS");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "__HL2SB_GLOBALS");
  }
}

static int HL2SB_SetGlobalValue(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  luaL_checkany(L, 2);
  const int type = lua_type(L, 2);
  bool allowed = type == LUA_TNIL || type == LUA_TBOOLEAN ||
                 type == LUA_TNUMBER || type == LUA_TSTRING;
  if (type == LUA_TUSERDATA && lua_getmetatable(L, 2)) {
    lua_getfield(L, -1, "MetaID");
    const int id = (int)lua_tointeger(L, -1);
    allowed = id == LUA_TYPE_ENTITY || id == LUA_TYPE_VECTOR || id == LUA_TYPE_ANGLE;
    lua_pop(L, 2);
  }
  if (!allowed)
    return luaL_error(L, "SetNetworkedVar: Non-networkable type '%s' encountered!", luaL_typename(L, 2));
  if (type == LUA_TSTRING && lua_rawlen(L, 2) > 200)
    return luaL_error(L, "SetNetworkedVar: string exceeds 200 bytes");
  HL2SB_PushGlobalValues(L);
  lua_pushstring(L, name);
  lua_rawget(L, -2);
  const bool changed = !lua_compare(L, -1, 2, LUA_OPEQ);
  lua_pop(L, 1);
  lua_pushstring(L, name);
  lua_pushvalue(L, 2);
  lua_rawset(L, -3);
#ifndef CLIENT_DLL
  if (changed) HL2SB_GlobalSend(L, name, 2, NULL);
#endif
  return 0;
}

static int HL2SB_GetGlobalValue(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  lua_settop(L, 2);
  HL2SB_PushGlobalValues(L);
  lua_pushstring(L, name);
  lua_rawget(L, -2);
  if (!lua_isnil(L, -1)) return 1;
  lua_pop(L, 1);
  if (!lua_isnoneornil(L, 2)) { lua_pushvalue(L, 2); return 1; }
  const int fallback = (int)lua_tointeger(L, lua_upvalueindex(1));
  if (fallback == 1) lua_pushnumber(L, 0);
  else if (fallback == 2) lua_pushboolean(L, false);
  else if (fallback == 3) lua_pushliteral(L, "");
  else if (fallback == 4) lua_pushvector(L, Vector(0, 0, 0));
  else if (fallback == 5) lua_pushangle(L, QAngle(0, 0, 0));
  else if (fallback == 6) lua_pushentity(L, NULL);
  else lua_pushnil(L);
  return 1;
}

static void HL2SB_RegisterGlobalValues(lua_State *L) {
  HL2SB_PushGlobalValues(L);
  lua_pop(L, 1);
  const char *setters[] = {"SetGlobalVar", "SetGlobalInt", "SetGlobalFloat", "SetGlobalBool", "SetGlobalString", "SetGlobalVector", "SetGlobalAngle", "SetGlobalEntity"};
  const char *getters[] = {"GetGlobalVar", "GetGlobalInt", "GetGlobalFloat", "GetGlobalBool", "GetGlobalString", "GetGlobalVector", "GetGlobalAngle", "GetGlobalEntity"};
  const int defaults[] = {0, 1, 1, 2, 3, 4, 5, 6};
  for (int i = 0; i < ARRAYSIZE(setters); ++i) {
    lua_pushcfunction(L, HL2SB_SetGlobalValue);
    lua_setglobal(L, setters[i]);
    lua_pushinteger(L, defaults[i]);
    lua_pushcclosure(L, HL2SB_GetGlobalValue, 1);
    lua_setglobal(L, getters[i]);
  }
}

LUALIB_API void luasrc_openlibs (lua_State *L) {
  HL2SB_RefreshGameContentPaths();
  lua_pushcfunction( L, lua_global_RealTime ); lua_setglobal( L, "RealTime" );
  const luaL_Reg *lib = luasrclibs;
  for (; lib->func; lib++) {
    // HL2SB (2026-09-21): log the module name FIRST so a failure names its
    // library immediately.
    //
    // HL2SB (2026-09-21, later): lua_call here was UNPROTECTED - a Lua error in
    // any luaopen_* went straight to the atpanic handler and took the PROCESS
    // down ("进地图闪退"), with an empty traceback because the throw unwound
    // through the pure-C boundary.  All day 2026-09-21 produced exactly that,
    // with DIFFERENT messages per run ("attempt to call a nil value" / "index a
    // number value" / "index a nil value" / "index a string value") at
    // different libraries - flaky, undiagnosable, and fatal.  lua_pcall turns
    // the whole class into a named, non-fatal console error: the failing lib is
    // skipped, the state keeps initializing, the game boots, and the log says
    // exactly which lib to look at.  A half-opened lib can leave one binding
    // missing (addons see nil where the enum would be) - strictly better than
    // a hard crash; GMod makes the same tradeoff everywhere.
    lua_pushcfunction(L, lib->func);
    lua_pushstring(L, lib->name);
    if ( lua_pcall(L, 1, 0, 0) != LUA_OK ) {
      luasrc_LuaErrorMsgF( "[HL2SB] openlibs: library '%s' FAILED: %s -- skipped, state continues\n",
        ( lib->name && lib->name[0] ) ? lib->name : "(anon)",
        lua_tostring(L, -1) ? lua_tostring(L, -1) : "(no message)" );
      lua_pop(L, 1);
    }
  }

/* HL2SB GMod compat: the DEFINE_BASECLASS( name ) global (wiki: it is a
 * PREPROCESSOR keyword in GMod -- the loader's own GLua rewrite pass expands
 * call sites into `local BaseClass = baseclass.Get( name )`).  But addons
 * loaded through paths WITHOUT the rewrite (and scripts that reference the
 * name at runtime) still need the callable, and a Lua-side definition is
 * impossible: the rewrite pass replaces the bare token inside the definition
 * itself, which is exactly how gmod_globals.lua died on 2026-09-20.  Define
 * it here in C++ where the name is assembled from two literals -- the
 * rewriter never sees it. */
  {
    // NOTE: the baseclass module does NOT exist yet at openlibs time (it loads
    // with the modules/ pass) -- the closure must read it lazily at CALL time.
    // The first version gated the definition on baseclass being present, which
    // was always false and left DEFINE_BASECLASS nil (probed 2026-09-20 22:41).
    static const char szName[] = "DEFINE_BASE" "CLASS";
    lua_pushcclosure( L, []( lua_State *LS ) -> int {
      lua_getglobal( LS, "baseclass" );
      if ( !lua_istable( LS, -1 ) )
      {
        lua_pop( LS, 1 );
        lua_pushnil( LS );
        return 1;
      }
      lua_getfield( LS, -1, "Get" );
      if ( !lua_isfunction( LS, -1 ) )
      {
        lua_pop( LS, 2 );
        lua_pushnil( LS );
        return 1;
      }
      lua_remove( LS, -2 );
      lua_pushvalue( LS, 1 );
      // HL2SB (2026-09-21): baseclass.Get( name ) ERRORS on an unknown name
      // (GMod's own module does), and a lua_call from inside this closure is
      // UNPROTECTED - the error went straight to the atpanic handler and took
      // the process down.  pcall it and return nil on failure instead: the
      // script gets the same nil BaseClass it would have had, with the named
      // error in the log, instead of a 闪退.
      if ( lua_pcall( LS, 1, 1, 0 ) != LUA_OK ) {
        luasrc_LuaErrorMsgF( "[HL2SB] DEFINE_BASECLASS: %s\n",
          lua_tostring( LS, -1 ) ? lua_tostring( LS, -1 ) : "(no message)" );
        lua_pop( LS, 1 );
        lua_pushnil( LS );
        return 1;
      }
      // GMod's expansion also mirrors the result onto the global BaseClass,
      // so BaseClass:Initialize( self ) calls keep resolving.
      lua_setglobal( LS, "BaseClass" );
      lua_getglobal( LS, "BaseClass" );
      return 1;
    }, 0 );
    lua_setglobal( L, szName );

    // HL2SB: the gmod_globals.lua probe kept printing DEFINE_BASECLASS=nil
    // even with this closure in the binary, so record here -- at the exact
    // registration site -- what the state actually holds.  If this line says
    // "function" while the Lua probe still says nil, something between
    // openlibs and the extension scan REMOVES or shadows the global.
    lua_getglobal( L, szName );
    luasrc_LuaWarnMsgF( "[HL2SB] openlibs: DEFINE_BASECLASS registered as %s\n",
             luaL_typename( L, -1 ) );
    lua_pop( L, 1 );
  }

/* Every lib is open now, so the metatables exist and can be aliased. */
  luasrc_install_metatable_aliases(L);
  luasrc_install_type_names(L);
  luasrc_install_animation_methods(L, LUA_BASEPLAYERLIBNAME);
  luasrc_install_animation_methods(L, LUA_BASECOMBATWEAPONLIBNAME);
  // Player hands and other animating entity families use the same inherited
  // CBaseAnimating methods as GMod's Entity metatable.
  luasrc_install_animation_methods(L, LUA_BASEENTITYLIBNAME);
  lua_pushinteger(L, ActivityList_RegisterPrivateActivity("ACT_MP_SWIM_IDLE"));
  lua_setglobal(L, "ACT_MP_SWIM_IDLE");

  /* ...and the library tables themselves can be reachable under GMod's names. */
  luasrc_install_lib_aliases(L);

#ifdef CLIENT_DLL
  lua_getglobal(L, "render");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "DynamicLight");
    if (lua_isfunction(L, -1)) lua_setglobal(L, "DynamicLight");
    else lua_pop(L, 1);
  }
  lua_pop(L, 1);
#endif

  lua_pushcfunction(L, HL2SB_UILoadPresets); lua_setglobal(L, "LoadPresets");
  lua_pushcfunction(L, HL2SB_UISavePresets); lua_setglobal(L, "SavePresets");
  lua_newtable(L);
  lua_pushcfunction(L, HL2SB_UIPopulateSpawnlists); lua_setfield(L, -2, "PopulateFromTextFiles");
  lua_pushcfunction(L, HL2SB_UISaveSpawnlists); lua_setfield(L, -2, "SaveToTextFiles");
  lua_setglobal(L, "spawnmenu");
  lua_getglobal(L, "util");
  lua_pushcfunction(L, HL2SB_UITableToKeyValues); lua_setfield(L, -2, "TableToKeyValues");
  lua_pushcfunction(L, HL2SB_UIKeyValuesToTable); lua_setfield(L, -2, "KeyValuesToTable");
  lua_pushcfunction(L, HL2SB_KeyValuesToTablePreserveOrder); lua_setfield(L, -2, "KeyValuesToTablePreserveOrder");
  lua_pushcfunction(L, HL2SB_TimerCycle); lua_setfield(L, -2, "TimerCycle");
  lua_pushcfunction(L, lua_ProtectedCall); lua_setglobal(L, "ProtectedCall");
  lua_pop(L, 1);

  lua_pushinteger(L, 0); lua_setglobal(L, "TEAM_CONNECTING");
  lua_pushinteger(L, 1001); lua_setglobal(L, "TEAM_UNASSIGNED");
  lua_pushinteger(L, 1002); lua_setglobal(L, "TEAM_SPECTATOR");

  /* HL2SB: re-assert the realm globals LAST.
  **
  ** public/lenumerations_shared.cpp:573 pushes the entity flag FL_CLIENT (256)
  ** with the shortname "CLIENT", and lua_pushenum publishes shortnames as flat
  ** globals -- so it overwrote the boolean base_open() had set (luamanager.cpp:
  ** 235-251).  Result, measured in-game: CLIENT=256 on BOTH realms, i.e. truthy
  ** everywhere, so every `if ( CLIENT ) then` took the client branch on the
  ** server too (that is what made the server import derma/ + all of lua/vgui/
  ** and print 54 "nil value (global 'derma')" lines per level).
  **
  ** Fixing the macro alone was not enough -- the object that defines the FL_
  ** enum did not get rebuilt (luamanager.h is not in waf's header dependency
  ** set) and the value was still 256 afterwards.  Stamping them here, once,
  ** after every lib is open, cannot be skipped by a stale object: this file is
  ** the one that changed, so it always recompiles.
  */
#ifdef CLIENT_DLL
  lua_pushboolean(L, 1); lua_setglobal(L, "_CLIENT");
  lua_pushboolean(L, 0); lua_setglobal(L, "SERVER");
  lua_pushboolean(L, 1); lua_setglobal(L, "CLIENT");
#else
  lua_pushboolean(L, 1); lua_setglobal(L, "_GAME");
  lua_pushboolean(L, 1); lua_setglobal(L, "SERVER");
  lua_pushboolean(L, 0); lua_setglobal(L, "CLIENT");
  // HL2SB (2026-09-21): register the Lua NetworkVar replication message at
  // state init.  Source's user message table must exist before clients sign
  // on; a lazy first-use registration (the previous version) landed after the
  // local listen client had signed on and every message was dropped.
  if ( usermessages->LookupUserMessage( "HL2SB_NW" ) == -1 )
    usermessages->Register( "HL2SB_NW", -1 );
    // -1 = variable size.  The engine validates the DECLARED size at
    // MessageEnd and refuses the send outright on mismatch ("User Msg
    // 'HL2SB_NW': N bytes written, expected 256" -- 226 refusals in one
    // session), so the fixed 256 dropped every broadcast server-side.
  // HL2SB (2026-09-22): Weapon:CallOnClient carrier message -- registered
  // here for the same signon-table reason as HL2SB_NW above.
  if ( usermessages->LookupUserMessage( "SWEPCmd" ) == -1 )
    usermessages->Register( "SWEPCmd", -1 );
  // HL2SB (2026-09-25): ammo pickup popup carrier -- see CBasePlayer_GiveAmmo
  // (game/server/lua/lplayer.cpp), which sends it unless hidePopup is set.
  if ( usermessages->LookupUserMessage( "HL2SB_AMMO" ) == -1 )
    usermessages->Register( "HL2SB_AMMO", -1 );
#endif
#ifdef CLIENT_DLL
  // HL2SB (2026-09-21): Lua NetworkVar replication receiver (see the
  // HL2SB_NWTagForType block above).  Hooked once per client state.
  //
  // The Register BEFORE the hook matters: the engine's user message table is
  // synced to clients at signon, and messages a DLL registers lazily (our old
  // first-SetBallColor registration ran mid entity-init, after the local
  // client's signon) never reach the client's table -- the engine silently
  // dropped every "HL2SB_NW" send, which is why the client never saw the
  // ball's replicated BallColor.  Both realms create the entry here, at state
  // init, before any entity can exist.
  if ( usermessages->LookupUserMessage( "HL2SB_NW" ) == -1 )
    usermessages->Register( "HL2SB_NW", -1 );  // -1: variable size (see server
    // branch above -- a fixed declared size makes the engine refuse the send).
  usermessages->HookMessage( "HL2SB_NW", __MsgFunc_HL2SB_NW );
  // HL2SB (2026-09-22): Weapon:CallOnClient receiver -- same timing rules.
  if ( usermessages->LookupUserMessage( "SWEPCmd" ) == -1 )
    usermessages->Register( "SWEPCmd", -1 );
  usermessages->HookMessage( "SWEPCmd", __MsgFunc_SWEPCmd );
  // HL2SB (2026-09-25): ammo pickup popup receiver -> HUDAmmoPickedUp hook.
  if ( usermessages->LookupUserMessage( "HL2SB_AMMO" ) == -1 )
    usermessages->Register( "HL2SB_AMMO", -1 );
  usermessages->HookMessage( "HL2SB_AMMO", __MsgFunc_HL2SB_AMMO );
#endif

  /* HL2SB: four GMod globals this engine never had.  All four are LOAD-TIME
  ** prerequisites of files imported verbatim from GMod, and every one of them is
  ** needed on BOTH realms (client.dll and server.dll compile this file), so they
  ** are stamped here rather than in a Lua shim that would only exist in one:
  **
  **   AddCSLuaFile( path )  lua/includes/extensions/coroutine.lua:5 calls it at
  **                         load time -> "attempt to call a nil value (global
  **                         'AddCSLuaFile')".  GMod uses it to mark a file for
  **                         client download; this engine has no download list, so
  **                         it records nothing and returns the path like GMod.
  **   IncludeCS( path )     the client-side twin of the above; same reasoning.
  **   jit                   lua/includes/extensions/util.lua:397 indexes
  **                         jit.version.  Our Lua is 5.4, not LuaJIT, so there is
  **                         no jit table at all and the whole GMod util extension
  **                         failed to load.  The stub carries a version string so
  **                         the feature check runs instead of throwing.
  **   LoadPresets()         lua/includes/modules/presets.lua:6 calls it at load.
  **   SENSORBONE            lua/includes/modules/motionsensor.lua:16 indexes it.
  */
  /* AddCSLuaFile( file = "current file" ) -- wiki Global.AddCSLuaFile.
  **
  ** Reference behaviour (GMod's engine-side binding; the four message strings
  ** below are verbatim from it):
  **   - the argument is OPTIONAL: when no value is supplied the binding asks the
  **     Lua interface for the file that is currently running, which walks the
  **     live stack and answers with the first chunk whose source starts with '@';
  **   - the client binding only re-syncs the Lua state and returns 0 values, so
  **     "does nothing on the client" is exact;
  **   - the name is validated (leading '/', '\\', '.', '~', '@' are refused; ':' ,
  **     ' ' and ".." anywhere are refused; the extension must be .lua), then
  **     resolved (calling script's directory first, then the lua search root,
  **     then the game root) and filed into the client download pack;
  **   - a name that cannot be found or an empty file raises a Lua error instead
  **     of returning quietly -- that is what a probe such as
  **         print( AddCSLuaFile( "autorun/client/probe.lua" ) )
  **     shows, and it is why the call never returns a value.
  ** The delivery half (shipping the recorded file to joining clients) is still
  ** not implemented in this tree; the server keeps its registration list so the
  ** call contract and its error paths match. */
  lua_pushcfunction( L, []( lua_State *pL ) -> int {
    // The download list is server state.  The realm is read from the SERVER
    // global instead of a preprocessor branch: this tree's compiler rejects
    // #ifdef inside a lambda body (C2146/C2065).
    bool bServerRealm = false;
    lua_getglobal( pL, "SERVER" );
    bServerRealm = lua_toboolean( pL, -1 ) != 0;
    lua_pop( pL, 1 );
    if ( !bServerRealm )
      return 0;

    char szName[ MAX_PATH ] = { 0 };
    const bool bExplicit = ( lua_type( pL, 1 ) != LUA_TNONE );
    if ( bExplicit )
    {
      const char *pszArg = luaL_checkstring( pL, 1 );
      Q_strncpy( szName, pszArg, sizeof( szName ) );
    }
    else
    {
      HL2SB_LuaCurrentFile( pL, szName, sizeof( szName ) );
    }

    // Validation, in the reference order: a leading dodgy symbol, then ':', ' '
    // and ".." anywhere, then the .lua extension.
    if ( szName[0] == '\\' || szName[0] == '/' || szName[0] == '.' ||
         szName[0] == '~' || szName[0] == '@' )
      return HL2SB_CSRaise( pL, "Refusing to AddCSLuaFile '%s' because its name contains dodgy symbols", szName );
    for ( const char *p = szName; *p; ++p )
    {
      if ( *p == ':' || *p == ' ' || ( p[0] == '.' && p[1] == '.' ) )
        return HL2SB_CSRaise( pL, "Refusing to AddCSLuaFile '%s' because its name contains dodgy symbols", szName );
    }
    const char *pszExt = strrchr( szName, '.' );
    if ( pszExt == NULL || Q_stricmp( pszExt, ".lua" ) != 0 )
      return HL2SB_CSRaise( pL, "AddCSLuaFile: Invalid filename '%s'", szName );

    // The reference rewrites '\\' to '/' before it resolves and reports the name.
    for ( char *p = szName; *p; ++p )
    {
      if ( *p == '\\' )
        *p = '/';
    }

    char szCaller[ MAX_PATH ] = { 0 };
    HL2SB_LuaCurrentFile( pL, szCaller, sizeof( szCaller ) );

    char szResolved[ MAX_PATH ] = { 0 };
    char szCandidate[ MAX_PATH ];
    char szNorm[ MAX_PATH ];

    // 1. Relative to the calling script (the reference asks for the current
    //    script's directory first).  Chunk names are MOD-relative and already
    //    carry "lua/" or the addon tree prefix, so they need no rewriting.
    if ( szCaller[0] != '\0' )
    {
      char szDir[ MAX_PATH ];
      Q_strncpy( szDir, szCaller, sizeof( szDir ) );
      char *pszSlash = strrchr( szDir, '/' );
      if ( pszSlash != NULL )
      {
        pszSlash[1] = '\0';
        Q_snprintf( szCandidate, sizeof( szCandidate ), "%s%s", szDir, szName );
        HL2SB_CSNormalizeDots( szCandidate, szNorm, sizeof( szNorm ) );
        if ( g_pFullFileSystem != NULL && g_pFullFileSystem->FileExists( szNorm, "MOD" ) )
          Q_strncpy( szResolved, szNorm, sizeof( szResolved ) );
      }
    }

    // Gamemode send manifests name files relative to the gamemodes root.
    if ( szResolved[0] == '\0' )
    {
      Q_snprintf( szCandidate, sizeof( szCandidate ), "gamemodes/%s", szName );
      HL2SB_CSNormalizeDots( szCandidate, szNorm, sizeof( szNorm ) );
      if ( g_pFullFileSystem != NULL && g_pFullFileSystem->FileExists( szNorm, "MOD" ) )
        Q_strncpy( szResolved, szNorm, sizeof( szResolved ) );
    }

    // 2. The lua search root, then 3. the game root.
    if ( szResolved[0] == '\0' )
    {
      Q_snprintf( szCandidate, sizeof( szCandidate ), "lua/%s", szName );
      HL2SB_CSNormalizeDots( szCandidate, szNorm, sizeof( szNorm ) );
      if ( g_pFullFileSystem != NULL && g_pFullFileSystem->FileExists( szNorm, "MOD" ) )
        Q_strncpy( szResolved, szNorm, sizeof( szResolved ) );
    }
    if ( szResolved[0] == '\0' && g_pFullFileSystem != NULL &&
         g_pFullFileSystem->FileExists( szName, "MOD" ) )
    {
      Q_strncpy( szResolved, szName, sizeof( szResolved ) );
    }

    if ( szResolved[0] == '\0' )
    {
      // A no-argument call names the chunk that is running, so the file is known
      // to exist even when the composed candidates miss (a file inside a mounted
      // pack is not addressable from the MOD root).  An explicit name has no such
      // guarantee and gets the reference error.
      if ( !bExplicit && szCaller[0] != '\0' )
        Q_strncpy( szResolved, szCaller, sizeof( szResolved ) );
      else
        return HL2SB_CSRaise( pL, "AddCSLuaFile: Couldn't find '%s'", szName );
    }

    if ( g_pFullFileSystem != NULL )
    {
      FileHandle_t hFile = g_pFullFileSystem->Open( szResolved, "rb", "MOD" );
      if ( hFile )
      {
        const int nSize = g_pFullFileSystem->Size( hFile );
        g_pFullFileSystem->Close( hFile );
        if ( nSize == 0 )
          return HL2SB_CSRaise( pL, "AddCSLuaFile: Empty file '%s'", szResolved );
      }
    }

    static char s_rgchCSFiles[ 8192 ][ 128 ];
    static int s_nCSFileCount = 0;
    int i = 0;
    for ( ; i < s_nCSFileCount; ++i )
    {
      if ( !Q_stricmp( s_rgchCSFiles[i], szResolved ) )
        break;
    }
    if ( i == s_nCSFileCount && s_nCSFileCount < (int)V_ARRAYSIZE( s_rgchCSFiles ) &&
         Q_strlen( szResolved ) < (int)sizeof( s_rgchCSFiles[0] ) )
    {
      Q_strncpy( s_rgchCSFiles[ s_nCSFileCount ], szResolved, sizeof( s_rgchCSFiles[0] ) );
      ++s_nCSFileCount;
      Msg( "[lua] AddCSLuaFile: %s (client delivery pending)\n", szResolved );
    }
    return 0;
  } );
  lua_setglobal( L, "AddCSLuaFile" );

  if ( luaL_loadstring( L,
    "AddCSLuaFile = AddCSLuaFile or function( path ) return path end\n"
    "IncludeCS = IncludeCS or function( path ) return path end\n"
    // HL2SB (sbrust): arch field (GMod jit.arch values: x86/x64/arm/arm64) --
    // util.IsBinaryModuleInstalled derives the suffix from it; 32/64-bit no longer share one slot.
#if defined( __aarch64__ ) || defined( _M_ARM64 ) || defined( _M_ARM64EC )
    "jit = jit or { version = 'Lua 5.4 (no LuaJIT)', version_num = 50400, arch = 'arm64' }\n"
#elif defined( _WIN64 ) || defined( __x86_64__ )
    "jit = jit or { version = 'Lua 5.4 (no LuaJIT)', version_num = 50400, arch = 'x64' }\n"
#elif defined( __arm__ )
    "jit = jit or { version = 'Lua 5.4 (no LuaJIT)', version_num = 50400, arch = 'arm' }\n"
#else
    "jit = jit or { version = 'Lua 5.4 (no LuaJIT)', version_num = 50400, arch = 'x86' }\n"
#endif
    //---------------------------------------------------------------------
    // NPC_STATE_*: GMod exposes Source's own NPC_STATE enum as globals
    // (wiki NPC:GetNPCState "see the NPC_STATE enum").  scp173 compares
    //     currVictim:GetNPCState() != NPC_STATE_DEAD
    // serverside; with the global nil the comparison was "state != nil",
    // always true, so a dead NPC still counted as a watcher.  Values are
    // game/server/ai_npcstate.h's enum verbatim.
    //---------------------------------------------------------------------
    "NPC_STATE_INVALID = -1 NPC_STATE_NONE = 0 NPC_STATE_IDLE = 1 NPC_STATE_ALERT = 2 "
    "NPC_STATE_COMBAT = 3 NPC_STATE_SCRIPT = 4 NPC_STATE_PLAYDEAD = 5 NPC_STATE_PRONE = 6 "
    "NPC_STATE_DEAD = 7\n"
    //---------------------------------------------------------------------
    // player: only the SERVER DLL ever created this table
    // (game/server/lua/lplayer.cpp:652 does lua_setglobal( L, "player" ));
    // the client's luaopen_CBasePlayer (game/client/lua/lc_baseplayer.cpp)
    // registers the metatable and LocalPlayer but never the library, so on
    // the client `player` was nil and two imported GMod files died:
    //
    //     extensions/player.lua:265        attempt to index a nil value (global player)
    //     extensions/entity_iter.lua:14    (cascade: player.lua never ran)
    //
    // This snippet runs AFTER the library loop in luasrc_openlibs, so the
    // server's real table is kept and only the client gets an empty one, which
    // is what GMod's own extensions/player.lua then fills in.
    //---------------------------------------------------------------------
    "player = player or {}\n"
    "LoadPresets = LoadPresets or function() end\n"
    "SENSORBONE = SENSORBONE or {}\n"
    // sql used to be a dostring stub here (IsStub, swallow-everything Query).
    // It is the real SQLite-backed library now: C side registered above in
    // luasrclibs (lsql.cpp).  The stub block is gone; the Lua shell is
    // attached right below.
    ) == 0 )
  {
    lua_pcall( L, 0, 0, 0 );
  }
  else
  {
    // A syntax error here is a bug in this snippet, not in the game.
    luasrc_LuaWarnMsgF( "[HL2SB] lsrcinit: GMod global stubs failed to compile: %s\n", lua_tostring( L, -1 ) );
    lua_pop( L, 1 );
  }

  //-----------------------------------------------------------------------------
  // HL2SB: attach GMod's Lua shell for the sql library right here, at
  // openlibs time.  Both realms run the extensions and modules folder passes
  // BEFORE lua/includes/init.lua, and those passes consume sql.TableExists
  // at load time (extensions/player.lua playerpdata, modules/cookie.lua) --
  // so SQLStr / TableExists / IndexExists / QueryRow / QueryValue / Begin /
  // Commit / LastError / the global SQLStr must exist here, not at
  // init.lua time.  The old stub lived in this exact spot for the same
  // reason.  init.lua still does include( "util/sql.lua" ) later, which just
  // re-runs the file (it only redefines the same functions).
  //-----------------------------------------------------------------------------
  luasrc_dofile_includes( L, "util/sql.lua" );

  //-----------------------------------------------------------------------------
  // HL2SB: GMod's `game` table.
  //
  // modules/... no -- extensions/game.lua:24 EXTENDS it (`function
  // game.AddAmmoType( tbl )`), so it died on
  //     extensions/game.lua:24: attempt to index a nil value (global 'game')
  // because this engine has no game library at all (no LUA_GAMELIBNAME
  // anywhere).  Creating the table here lets that file load; the individual
  // game.* functions it expects from the engine are still a follow-up.
  //
  // `or {}` style: if a game library ever appears, its table wins.
  //-----------------------------------------------------------------------------
  if ( lua_getglobal( L, "game" ) == LUA_TNIL )
  {
    lua_pop( L, 1 );
    lua_newtable( L );
    // GMod global GetHostName(): "Returns the name of the current server.
    // GetHostName returns information from ConVar hostname" (wiki, Global.GetHostName).
    // Missing entirely in this tree, so gamemodes/base/gamemode/init.lua:92 raised
    // "attempt to call global 'GetHostName' (a nil value)" once per level.
    lua_pushcfunction( L, []( lua_State *pL ) -> int {
        ConVarRef hostname( "hostname" );
        lua_pushstring( pL, hostname.IsValid() ? hostname.GetString() : "" );
        return 1;
    } );
    lua_setglobal( L, "GetHostName" );

    lua_setglobal( L, "game" );
  }
  else
  {
    lua_pop( L, 1 );
  }

  // The engine-backed members (lua_game_* above).  GMod's extensions/game.lua
  // only ADDS to this table, and the Lua shims in extensions/gmod_compat.lua
  // redefine GetMap/SinglePlayer later with the same behaviour, so anything
  // installed here may be overridden by script.
  lua_getglobal( L, "game" );
  if ( lua_istable( L, -1 ) )
  {
    lua_pushcfunction( L, lua_game_AddParticles ); lua_setfield( L, -2, "AddParticles" );
    lua_pushcfunction( L, lua_game_AddDecal );     lua_setfield( L, -2, "AddDecal" );
    lua_pushcfunction( L, lua_game_GetMap );       lua_setfield( L, -2, "GetMap" );
    lua_pushcfunction( L, lua_game_GetWorld );     lua_setfield( L, -2, "GetWorld" );
    lua_pushcfunction( L, lua_game_SinglePlayer ); lua_setfield( L, -2, "SinglePlayer" );
    lua_pushcfunction( L, lua_game_MaxPlayers );   lua_setfield( L, -2, "MaxPlayers" );
    lua_pushcfunction( L, lua_game_IsDedicated );  lua_setfield( L, -2, "IsDedicated" );
    lua_pushcfunction( L, lua_game_GetAmmoName );  lua_setfield( L, -2, "GetAmmoName" );
    lua_pushcfunction( L, lua_game_GetAmmoID );    lua_setfield( L, -2, "GetAmmoID" );
  }
  lua_pop( L, 1 );

  //-----------------------------------------------------------------------------
  // HL2SB: Source's StudioRender flags, exposed with the ENGINE's own values
  // (public/model_types.h:16-18) rather than literals that could drift.
  //
  // modules/halo.lua:14 is
  //     bit.bor( STUDIO_RENDER, STUDIO_SKIP_DECALS or 0 )
  // and failed with "bad argument #1 to 'bor' (number expected, got nil)".
  // STUDIO_SKIP_DECALS is genuinely not defined in this engine's model_types.h,
  // and halo.lua already tolerates that with "or 0", so it is NOT faked here.
  //-----------------------------------------------------------------------------
  // Values from public/model_types.h:16-18.  The #ifndef fallbacks matter: the
  // first version of this block was wrapped in #ifdef STUDIO_RENDER and the
  // whole thing compiled OUT, because model_types.h is not in this translation
  // unit's include chain -- so the flags were never pushed and modules/halo.lua
  // kept failing with "bad argument #1 to 'bor' (number expected, got nil)".
#ifndef STUDIO_RENDER
#define STUDIO_RENDER 0x00000001
#endif
#ifndef STUDIO_VIEWXFORMATTACHMENTS
#define STUDIO_VIEWXFORMATTACHMENTS 0x00000002
#endif
#ifndef STUDIO_DRAWTRANSLUCENTSUBMODELS
#define STUDIO_DRAWTRANSLUCENTSUBMODELS 0x00000004
#endif
// HL2SB (2026-10-03): the shadow/depth draw flags that reference addons test
// inside RenderOverride (First Person Body gates its projected-shadow copies
// with bit.band on these); values from public/model_types.h:31,34.
#ifndef STUDIO_SSAODEPTHTEXTURE
#define STUDIO_SSAODEPTHTEXTURE 0x08000000
#endif
#ifndef STUDIO_SHADOWDEPTHTEXTURE
#define STUDIO_SHADOWDEPTHTEXTURE 0x40000000
#endif

  lua_pushinteger( L, STUDIO_RENDER );                   lua_setglobal( L, "STUDIO_RENDER" );
  lua_pushinteger( L, STUDIO_VIEWXFORMATTACHMENTS );     lua_setglobal( L, "STUDIO_VIEWXFORMATTACHMENTS" );
  lua_pushinteger( L, STUDIO_DRAWTRANSLUCENTSUBMODELS ); lua_setglobal( L, "STUDIO_DRAWTRANSLUCENTSUBMODELS" );
  lua_pushinteger( L, STUDIO_SSAODEPTHTEXTURE );         lua_setglobal( L, "STUDIO_SSAODEPTHTEXTURE" );
  lua_pushinteger( L, STUDIO_SHADOWDEPTHTEXTURE );       lua_setglobal( L, "STUDIO_SHADOWDEPTHTEXTURE" );

  //-----------------------------------------------------------------------------
  // HL2SB (2026-10-16): GMod's BONE_* enum family.  GMod publishes the whole
  // group as globals (client.dll .rdata carries the contiguous name block
  // BONE_CALCULATE_MASK .. BONE_USED_BY_BONE_MERGE right before USE_* and
  // TRANSMIT_*, the same enum tables this file already mirrors for the latter
  // two), and the shipped bone-manipulation widget reads one of them on both
  // realms: lua/entities/widget_bones.lua:154
  //     if ( !ent:BoneHasFlag( k, BONE_USED_BY_VERTEX_LOD0 ) ) then continue end
  // That widget's Setup runs from the bone_manipulate property's server
  // Receive chain, so without the global the server-side receive raised
  // "bad argument #3 to 'BoneHasFlag' (number expected, got nil)" and the
  // widget was never built.  Values from public/studio.h:415-441 (same
  // numbers GMod ships).  The #ifndef fallbacks follow the STUDIO block above:
  // studio.h is not in this translation unit's include chain.
  //-----------------------------------------------------------------------------
#ifndef BONE_CALCULATE_MASK
#define BONE_CALCULATE_MASK 0x1F
#endif
#ifndef BONE_PHYSICALLY_SIMULATED
#define BONE_PHYSICALLY_SIMULATED 0x01
#endif
#ifndef BONE_PHYSICS_PROCEDURAL
#define BONE_PHYSICS_PROCEDURAL 0x02
#endif
#ifndef BONE_ALWAYS_PROCEDURAL
#define BONE_ALWAYS_PROCEDURAL 0x04
#endif
#ifndef BONE_SCREEN_ALIGN_SPHERE
#define BONE_SCREEN_ALIGN_SPHERE 0x08
#endif
#ifndef BONE_SCREEN_ALIGN_CYLINDER
#define BONE_SCREEN_ALIGN_CYLINDER 0x10
#endif
#ifndef BONE_USED_MASK
#define BONE_USED_MASK 0x0007FF00
#endif
#ifndef BONE_USED_BY_ANYTHING
#define BONE_USED_BY_ANYTHING 0x0007FF00
#endif
#ifndef BONE_USED_BY_HITBOX
#define BONE_USED_BY_HITBOX 0x00000100
#endif
#ifndef BONE_USED_BY_ATTACHMENT
#define BONE_USED_BY_ATTACHMENT 0x00000200
#endif
#ifndef BONE_USED_BY_VERTEX_MASK
#define BONE_USED_BY_VERTEX_MASK 0x0003FC00
#endif
#ifndef BONE_USED_BY_VERTEX_LOD0
#define BONE_USED_BY_VERTEX_LOD0 0x00000400
#endif
#ifndef BONE_USED_BY_VERTEX_LOD1
#define BONE_USED_BY_VERTEX_LOD1 0x00000800
#endif
#ifndef BONE_USED_BY_VERTEX_LOD2
#define BONE_USED_BY_VERTEX_LOD2 0x00001000
#endif
#ifndef BONE_USED_BY_VERTEX_LOD3
#define BONE_USED_BY_VERTEX_LOD3 0x00002000
#endif
#ifndef BONE_USED_BY_VERTEX_LOD4
#define BONE_USED_BY_VERTEX_LOD4 0x00004000
#endif
#ifndef BONE_USED_BY_VERTEX_LOD5
#define BONE_USED_BY_VERTEX_LOD5 0x00008000
#endif
#ifndef BONE_USED_BY_VERTEX_LOD6
#define BONE_USED_BY_VERTEX_LOD6 0x00010000
#endif
#ifndef BONE_USED_BY_VERTEX_LOD7
#define BONE_USED_BY_VERTEX_LOD7 0x00020000
#endif
#ifndef BONE_USED_BY_BONE_MERGE
#define BONE_USED_BY_BONE_MERGE 0x00040000
#endif

  lua_pushinteger( L, BONE_CALCULATE_MASK );         lua_setglobal( L, "BONE_CALCULATE_MASK" );
  lua_pushinteger( L, BONE_PHYSICALLY_SIMULATED );   lua_setglobal( L, "BONE_PHYSICALLY_SIMULATED" );
  lua_pushinteger( L, BONE_PHYSICS_PROCEDURAL );     lua_setglobal( L, "BONE_PHYSICS_PROCEDURAL" );
  lua_pushinteger( L, BONE_ALWAYS_PROCEDURAL );      lua_setglobal( L, "BONE_ALWAYS_PROCEDURAL" );
  lua_pushinteger( L, BONE_SCREEN_ALIGN_SPHERE );    lua_setglobal( L, "BONE_SCREEN_ALIGN_SPHERE" );
  lua_pushinteger( L, BONE_SCREEN_ALIGN_CYLINDER );  lua_setglobal( L, "BONE_SCREEN_ALIGN_CYLINDER" );
  lua_pushinteger( L, BONE_USED_MASK );              lua_setglobal( L, "BONE_USED_MASK" );
  lua_pushinteger( L, BONE_USED_BY_ANYTHING );       lua_setglobal( L, "BONE_USED_BY_ANYTHING" );
  lua_pushinteger( L, BONE_USED_BY_HITBOX );         lua_setglobal( L, "BONE_USED_BY_HITBOX" );
  lua_pushinteger( L, BONE_USED_BY_ATTACHMENT );     lua_setglobal( L, "BONE_USED_BY_ATTACHMENT" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_MASK );    lua_setglobal( L, "BONE_USED_BY_VERTEX_MASK" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_LOD0 );    lua_setglobal( L, "BONE_USED_BY_VERTEX_LOD0" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_LOD1 );    lua_setglobal( L, "BONE_USED_BY_VERTEX_LOD1" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_LOD2 );    lua_setglobal( L, "BONE_USED_BY_VERTEX_LOD2" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_LOD3 );    lua_setglobal( L, "BONE_USED_BY_VERTEX_LOD3" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_LOD4 );    lua_setglobal( L, "BONE_USED_BY_VERTEX_LOD4" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_LOD5 );    lua_setglobal( L, "BONE_USED_BY_VERTEX_LOD5" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_LOD6 );    lua_setglobal( L, "BONE_USED_BY_VERTEX_LOD6" );
  lua_pushinteger( L, BONE_USED_BY_VERTEX_LOD7 );    lua_setglobal( L, "BONE_USED_BY_VERTEX_LOD7" );
  lua_pushinteger( L, BONE_USED_BY_BONE_MERGE );     lua_setglobal( L, "BONE_USED_BY_BONE_MERGE" );

  // HL2SB (2026-10-08): GMod's Enums/TRANSMIT family, answered by
  // ENTITY:UpdateTransmitState (basescripted.cpp maps the script's return
  // through SetTransmitState).  Wiki values: TRANSMIT_ALWAYS = 0,
  // TRANSMIT_NEVER = 1, TRANSMIT_PVS = 2.
  lua_pushinteger( L, 0 );  lua_setglobal( L, "TRANSMIT_ALWAYS" );
  lua_pushinteger( L, 1 );  lua_setglobal( L, "TRANSMIT_NEVER" );
  lua_pushinteger( L, 2 );  lua_setglobal( L, "TRANSMIT_PVS" );

  // HL2SB (2026-10-05): footstep-timing enum family (game/shared/
  // baseplayer_shared.h stepsoundtimes_t) that GM:PlayerStepSoundTime
  // dispatches with; the ported gamemode method branches on these names.
  lua_pushinteger( L, 0 );  lua_setglobal( L, "STEPSOUNDTIME_NORMAL" );
  lua_pushinteger( L, 1 );  lua_setglobal( L, "STEPSOUNDTIME_ON_LADDER" );
  lua_pushinteger( L, 2 );  lua_setglobal( L, "STEPSOUNDTIME_WATER_KNEE" );
  lua_pushinteger( L, 3 );  lua_setglobal( L, "STEPSOUNDTIME_WATER_FOOT" );

#ifdef CLIENT_DLL
  // HL2SB (2026-10-13): GMod's Enums/TEXT_FILTER family (client-only in GMod --
  // the names live in the client binary).  util.FilterText takes these as its
  // filter context; the base gamemode's GM:OnPlayerChat branches on
  // bit.band( cl_chatfilters, 64 ) to pick between GAME_CONTENT and CHAT.
  // Values from the GMod wiki enum page.
  lua_pushinteger( L, 0 );  lua_setglobal( L, "TEXT_FILTER_UNKNOWN" );
  lua_pushinteger( L, 1 );  lua_setglobal( L, "TEXT_FILTER_GAME_CONTENT" );
  lua_pushinteger( L, 2 );  lua_setglobal( L, "TEXT_FILTER_CHAT" );
  lua_pushinteger( L, 3 );  lua_setglobal( L, "TEXT_FILTER_NAME" );
#endif

  HL2SB_RegisterGlobalValues(L);
  luaL_register(L, "_G", lua_metatable_funcs);
  lua_pop(L, 1);

  /* HL2SB: GMod globals that the engine owns (see the lua_Msg / lua_SuppressHost
  ** Events definitions above).  Registered after the library pass so nothing
  ** overwrites them. */
  lua_pushcfunction( L, lua_Msg );
  lua_setglobal( L, "Msg" );
  lua_pushcfunction( L, lua_MsgN );
  lua_setglobal( L, "MsgN" );
  /* HL2SB (sbrust): GMod 的 MsgC -- 真彩色控制台输出。内容仓的
  ** hl2sb_gmod_msgc.lua 垫片只在 C 绑定缺失时兜底，此处注册后它自动让位。 */
  lua_pushcfunction( L, lua_MsgC );
  lua_setglobal( L, "MsgC" );

  /* HL2SB: GMod's bare Warning() -- orange on the console and collected in
  ** hl2sb_lua.log, i.e. the same treatment as everything else Lua prints. */
  lua_pushcfunction( L, lua_Warning );
  lua_setglobal( L, "Warning" );

  /* HL2SB: GMod's non-fatal error prints (see lua_ErrorNoHalt above). */
  lua_pushcfunction( L, lua_ErrorNoHalt );
  lua_setglobal( L, "ErrorNoHalt" );
  lua_pushcfunction( L, lua_ErrorNoHaltWithStack );
  lua_setglobal( L, "ErrorNoHaltWithStack" );

  lua_pushcfunction( L, lua_IsFirstTimePredicted );
  lua_setglobal( L, "IsFirstTimePredicted" );

#ifndef CLIENT_DLL
  lua_pushcfunction( L, lua_AddOriginToPVS );
  lua_setglobal( L, "AddOriginToPVS" );
#endif

  HL2SB_RegisterConsoleGlobals( L );

  /* HL2SB: GMod's `achievements` table, as a STUB.
  **
  ** GMod's achievements library lives in gamemodes/base/gamemode/cl_achievements.lua
  ** and is not part of what was imported here, so the global was simply absent.
  ** GMod's stock sent_ball names it in ENT:Use:
  **     activator:SendLua( "achievements.EatBall()" )
  **
  ** NOTE: This stub does NOT fix that call, and nothing here claims it does:
  ** Entity:SendLua is a no-op in this fork -- it warns once and returns
  ** (lbaseentity_shared.cpp:2433-2441, "this engine has no client Lua-channel")
  ** -- so the string is never evaluated on either realm and a missing
  ** `achievements` table would never have thrown from this entity.  The
  ** previous comment claimed a console error every time a ball was eaten; that
  ** was wrong, and so is any "fixes the error path" claim.
  **
  ** It is kept because it is harmless and gives GMod scripts/addons that index
  ** the global directly something to call, exactly as the `sql` stub above does.
  ** It records nothing: there is no achievement store and no overlay to award
  ** anything in.  Script may replace the whole table.
  */
  if ( luaL_loadstring( L,
    "achievements = achievements or {}\n"
    "setmetatable( achievements, { __index = function() return function() end end } )\n" ) == 0 )
  {
    lua_pcall( L, 0, 0, 0 );
  }
  else
  {
    luasrc_LuaWarnMsgF( "[HL2SB] lsrcinit: achievements stub failed to compile: %s\n", lua_tostring( L, -1 ) );
    lua_pop( L, 1 );
  }

  // The original Lua bootstrap installs named type reporting over the raw primitive.

#ifndef CLIENT_DLL
  lua_pushcfunction( L, lua_SuppressHostEvents );
  lua_setglobal( L, "SuppressHostEvents" );
#endif
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB (2026-09-21): server AI enum globals -- HULL_* and SCHED_*.
//
// GMod exposes Source's own AI enums as flat globals and scripted SNPCs use
// them directly: npc_scp173's Initialize calls
//     self:SetHullType( HULL_MEDIUM_TALL )
//     self:CapabilitiesAdd( bit.bor( CAP_MOVE_GROUND, ... ) )
//     self:SetSchedule( SCHED_CHASE_ENEMY )
// The values come from the engine's own enums (ai_default.h / ai_hull.h) so
// they can never drift from what SetSchedule's local schedule ids mean.
// lua_pushenum publishes both _E.HULL.MEDIUM_TALL and the flat HULL_MEDIUM_TALL.
//-----------------------------------------------------------------------------
LUALIB_API int luaopen_ServerAIEnums( lua_State *L )
{
  LUA_SET_ENUM_LIB_BEGIN( L, "HULL" );
  lua_pushenum( L, HULL_HUMAN, "HUMAN" );
  lua_pushenum( L, HULL_SMALL_CENTERED, "SMALL_CENTERED" );
  lua_pushenum( L, HULL_WIDE_HUMAN, "WIDE_HUMAN" );
  lua_pushenum( L, HULL_TINY, "TINY" );
  lua_pushenum( L, HULL_WIDE_SHORT, "WIDE_SHORT" );
  lua_pushenum( L, HULL_MEDIUM, "MEDIUM" );
  lua_pushenum( L, HULL_TINY_CENTERED, "TINY_CENTERED" );
  lua_pushenum( L, HULL_LARGE, "LARGE" );
  lua_pushenum( L, HULL_LARGE_CENTERED, "LARGE_CENTERED" );
  lua_pushenum( L, HULL_MEDIUM_TALL, "MEDIUM_TALL" );
  LUA_SET_ENUM_LIB_END( L );

  LUA_SET_ENUM_LIB_BEGIN( L, "SCHED" );
  lua_pushenum( L, SCHED_NONE, "NONE" );
  lua_pushenum( L, SCHED_IDLE_STAND, "IDLE_STAND" );
  lua_pushenum( L, SCHED_IDLE_WALK, "IDLE_WALK" );
  lua_pushenum( L, SCHED_IDLE_WANDER, "IDLE_WANDER" );
  lua_pushenum( L, SCHED_WAKE_ANGRY, "WAKE_ANGRY" );
  lua_pushenum( L, SCHED_ALERT_FACE, "ALERT_FACE" );
  lua_pushenum( L, SCHED_ALERT_FACE_BESTSOUND, "ALERT_FACE_BESTSOUND" );
  lua_pushenum( L, SCHED_ALERT_REACT_TO_COMBAT_SOUND, "ALERT_REACT_TO_COMBAT_SOUND" );
  lua_pushenum( L, SCHED_ALERT_SCAN, "ALERT_SCAN" );
  lua_pushenum( L, SCHED_ALERT_STAND, "ALERT_STAND" );
  lua_pushenum( L, SCHED_ALERT_WALK, "ALERT_WALK" );
  lua_pushenum( L, SCHED_INVESTIGATE_SOUND, "INVESTIGATE_SOUND" );
  lua_pushenum( L, SCHED_COMBAT_FACE, "COMBAT_FACE" );
  lua_pushenum( L, SCHED_COMBAT_SWEEP, "COMBAT_SWEEP" );
  lua_pushenum( L, SCHED_FEAR_FACE, "FEAR_FACE" );
  lua_pushenum( L, SCHED_COMBAT_STAND, "COMBAT_STAND" );
  lua_pushenum( L, SCHED_COMBAT_WALK, "COMBAT_WALK" );
  lua_pushenum( L, SCHED_CHASE_ENEMY, "CHASE_ENEMY" );
  lua_pushenum( L, SCHED_CHASE_ENEMY_FAILED, "CHASE_ENEMY_FAILED" );
  lua_pushenum( L, SCHED_VICTORY_DANCE, "VICTORY_DANCE" );
  lua_pushenum( L, SCHED_TARGET_FACE, "TARGET_FACE" );
  lua_pushenum( L, SCHED_TARGET_CHASE, "TARGET_CHASE" );
  lua_pushenum( L, SCHED_SMALL_FLINCH, "SMALL_FLINCH" );
  lua_pushenum( L, SCHED_BIG_FLINCH, "BIG_FLINCH" );
  lua_pushenum( L, SCHED_BACK_AWAY_FROM_ENEMY, "BACK_AWAY_FROM_ENEMY" );
  lua_pushenum( L, SCHED_MOVE_AWAY_FROM_ENEMY, "MOVE_AWAY_FROM_ENEMY" );
  lua_pushenum( L, SCHED_BACK_AWAY_FROM_SAVE_POSITION, "BACK_AWAY_FROM_SAVE_POSITION" );
  lua_pushenum( L, SCHED_TAKE_COVER_FROM_ENEMY, "TAKE_COVER_FROM_ENEMY" );
  lua_pushenum( L, SCHED_TAKE_COVER_FROM_BEST_SOUND, "TAKE_COVER_FROM_BEST_SOUND" );
  lua_pushenum( L, SCHED_FLEE_FROM_BEST_SOUND, "FLEE_FROM_BEST_SOUND" );
  lua_pushenum( L, SCHED_TAKE_COVER_FROM_ORIGIN, "TAKE_COVER_FROM_ORIGIN" );
  lua_pushenum( L, SCHED_FAIL_TAKE_COVER, "FAIL_TAKE_COVER" );
  lua_pushenum( L, SCHED_RUN_FROM_ENEMY, "RUN_FROM_ENEMY" );
  lua_pushenum( L, SCHED_RUN_FROM_ENEMY_FALLBACK, "RUN_FROM_ENEMY_FALLBACK" );
  lua_pushenum( L, SCHED_MOVE_TO_WEAPON_RANGE, "MOVE_TO_WEAPON_RANGE" );
  lua_pushenum( L, SCHED_ESTABLISH_LINE_OF_FIRE, "ESTABLISH_LINE_OF_FIRE" );
  lua_pushenum( L, SCHED_ESTABLISH_LINE_OF_FIRE_FALLBACK, "ESTABLISH_LINE_OF_FIRE_FALLBACK" );
  lua_pushenum( L, SCHED_PRE_FAIL_ESTABLISH_LINE_OF_FIRE, "PRE_FAIL_ESTABLISH_LINE_OF_FIRE" );
  lua_pushenum( L, SCHED_FAIL_ESTABLISH_LINE_OF_FIRE, "FAIL_ESTABLISH_LINE_OF_FIRE" );
  lua_pushenum( L, SCHED_SHOOT_ENEMY_COVER, "SHOOT_ENEMY_COVER" );
  lua_pushenum( L, SCHED_COWER, "COWER" );
  lua_pushenum( L, SCHED_MELEE_ATTACK1, "MELEE_ATTACK1" );
  lua_pushenum( L, SCHED_MELEE_ATTACK2, "MELEE_ATTACK2" );
  lua_pushenum( L, SCHED_RANGE_ATTACK1, "RANGE_ATTACK1" );
  lua_pushenum( L, SCHED_RANGE_ATTACK2, "RANGE_ATTACK2" );
  lua_pushenum( L, SCHED_SPECIAL_ATTACK1, "SPECIAL_ATTACK1" );
  lua_pushenum( L, SCHED_SPECIAL_ATTACK2, "SPECIAL_ATTACK2" );
  lua_pushenum( L, SCHED_STANDOFF, "STANDOFF" );
  lua_pushenum( L, SCHED_ARM_WEAPON, "ARM_WEAPON" );
  lua_pushenum( L, SCHED_DISARM_WEAPON, "DISARM_WEAPON" );
  lua_pushenum( L, SCHED_HIDE_AND_RELOAD, "HIDE_AND_RELOAD" );
  lua_pushenum( L, SCHED_RELOAD, "RELOAD" );
  lua_pushenum( L, SCHED_AMBUSH, "AMBUSH" );
  lua_pushenum( L, SCHED_DIE, "DIE" );
  lua_pushenum( L, SCHED_DIE_RAGDOLL, "DIE_RAGDOLL" );
  lua_pushenum( L, SCHED_WAIT_FOR_SCRIPT, "WAIT_FOR_SCRIPT" );
  lua_pushenum( L, SCHED_AISCRIPT, "AISCRIPT" );
  lua_pushenum( L, SCHED_SCRIPTED_WALK, "SCRIPTED_WALK" );
  lua_pushenum( L, SCHED_SCRIPTED_RUN, "SCRIPTED_RUN" );
  lua_pushenum( L, SCHED_SCRIPTED_CUSTOM_MOVE, "SCRIPTED_CUSTOM_MOVE" );
  lua_pushenum( L, SCHED_SCRIPTED_WAIT, "SCRIPTED_WAIT" );
  lua_pushenum( L, SCHED_SCRIPTED_FACE, "SCRIPTED_FACE" );
  lua_pushenum( L, SCHED_SCENE_GENERIC, "SCENE_GENERIC" );
  lua_pushenum( L, SCHED_NEW_WEAPON, "NEW_WEAPON" );
  lua_pushenum( L, SCHED_NEW_WEAPON_CHEAT, "NEW_WEAPON_CHEAT" );
  lua_pushenum( L, SCHED_SWITCH_TO_PENDING_WEAPON, "SWITCH_TO_PENDING_WEAPON" );
  lua_pushenum( L, SCHED_GET_HEALTHKIT, "GET_HEALTHKIT" );
  lua_pushenum( L, SCHED_WAIT_FOR_SPEAK_FINISH, "WAIT_FOR_SPEAK_FINISH" );
  lua_pushenum( L, SCHED_MOVE_AWAY, "MOVE_AWAY" );
  lua_pushenum( L, SCHED_MOVE_AWAY_FAIL, "MOVE_AWAY_FAIL" );
  lua_pushenum( L, SCHED_MOVE_AWAY_END, "MOVE_AWAY_END" );
  lua_pushenum( L, SCHED_FORCED_GO, "FORCED_GO" );
  lua_pushenum( L, SCHED_FORCED_GO_RUN, "FORCED_GO_RUN" );
  lua_pushenum( L, SCHED_NPC_FREEZE, "NPC_FREEZE" );
  lua_pushenum( L, SCHED_PATROL_WALK, "PATROL_WALK" );
  lua_pushenum( L, SCHED_COMBAT_PATROL, "COMBAT_PATROL" );
  lua_pushenum( L, SCHED_PATROL_RUN, "PATROL_RUN" );
  lua_pushenum( L, SCHED_RUN_RANDOM, "RUN_RANDOM" );
  lua_pushenum( L, SCHED_FALL_TO_GROUND, "FALL_TO_GROUND" );
  lua_pushenum( L, SCHED_DROPSHIP_DUSTOFF, "DROPSHIP_DUSTOFF" );
  lua_pushenum( L, SCHED_FLINCH_PHYSICS, "FLINCH_PHYSICS" );
  LUA_SET_ENUM_LIB_END( L );

  // CAP_*: the capability BITS from basecombatcharacter.h's Capability_t,
  // written out as literals so this shared file does not need the server
  // player header.  These are Source's own bits (CAP_MOVE_GROUND = 0x1) --
  // what CAI_BaseNPC::CapabilitiesAdd consumes raw.
  LUA_SET_ENUM_LIB_BEGIN( L, "CAP" );
  lua_pushenum( L, 0x00000001, "MOVE_GROUND" );
  lua_pushenum( L, 0x00000002, "MOVE_JUMP" );
  lua_pushenum( L, 0x00000004, "MOVE_FLY" );
  lua_pushenum( L, 0x00000008, "MOVE_CLIMB" );
  lua_pushenum( L, 0x00000010, "MOVE_SWIM" );
  lua_pushenum( L, 0x00000020, "MOVE_CRAWL" );
  lua_pushenum( L, 0x00000040, "MOVE_SHOOT" );
  lua_pushenum( L, 0x00000080, "SKIP_NAV_GROUND_CHECK" );
  lua_pushenum( L, 0x00000100, "USE" );
  lua_pushenum( L, 0x00000400, "AUTO_DOORS" );
  lua_pushenum( L, 0x00000800, "OPEN_DOORS" );
  lua_pushenum( L, 0x00001000, "TURN_HEAD" );
  lua_pushenum( L, 0x00002000, "WEAPON_RANGE_ATTACK1" );
  lua_pushenum( L, 0x00004000, "WEAPON_RANGE_ATTACK2" );
  lua_pushenum( L, 0x00008000, "WEAPON_MELEE_ATTACK1" );
  lua_pushenum( L, 0x00010000, "WEAPON_MELEE_ATTACK2" );
  lua_pushenum( L, 0x00020000, "INNATE_RANGE_ATTACK1" );
  lua_pushenum( L, 0x00040000, "INNATE_RANGE_ATTACK2" );
  lua_pushenum( L, 0x00080000, "INNATE_MELEE_ATTACK1" );
  lua_pushenum( L, 0x00100000, "INNATE_MELEE_ATTACK2" );
  lua_pushenum( L, 0x00200000, "USE_WEAPONS" );
  lua_pushenum( L, 0x02000000, "FRIENDLY_DMG_IMMUNE" );
  LUA_SET_ENUM_LIB_END( L );

  return 0;
}
#endif

