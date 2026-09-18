// hl2sb_model_config.cpp
// HL2SB Player Model Configuration System
// HL2SB_MAX_MODELS raised to 128 (gmod player model pack registers ~81 configs).

#include "cbase.h"
#include "hl2sb_model_config.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "utlvector.h"
#include "fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

HL2SB_ModelConfig_t g_HL2SB_ModelConfigs[HL2SB_MAX_MODELS];
int g_nHL2SB_ModelConfigCount = 0;
// Guard so the table is only scanned once on each side unless someone explicitly
// reloads.  Prevents console commands from re-reading the cfg dir every call.
static bool g_bModelConfigsLoaded = false;

// HL2SB: the szConfigFile marker of an entry that came from Lua
// (HL2SB_AddRuntimeModelConfig) rather than from cfg/playermodel/<name>.cfg.
// It is how a rescan tells the two apart - see HL2SB_LoadAllModelConfigs.
#define HL2SB_RUNTIME_CONFIG_FILE	"<lua>"

//-----------------------------------------------------------------------------
// Purpose: Lazy-load the model config table if it hasn't been populated yet.
//-----------------------------------------------------------------------------
void HL2SB_EnsureModelConfigsLoaded( void )
{
	if ( g_bModelConfigsLoaded )
		return;

	HL2SB_LoadAllModelConfigs();
	g_bModelConfigsLoaded = true;
}

//-----------------------------------------------------------------------------
// Purpose: precache one config's models (server only - no-op on the client)
//-----------------------------------------------------------------------------
static void HL2SB_PrecacheModelConfig( const HL2SB_ModelConfig_t &config )
{
#if !defined( CLIENT_DLL )
	if ( config.szPlayerModel[0] )
	{
		CBaseEntity::PrecacheModel( config.szPlayerModel );
	}

	if ( config.szHandsModel[0] )
	{
		// The hands value may encode skin/body after the path:
		//   "models/weapons/c_arms_citizen.mdl|2|0000000"
		// Precache the bare model path only.
		char szBare[ HL2SB_MAX_MODEL_PATH ];
		Q_strncpy( szBare, config.szHandsModel, sizeof(szBare) );
		char *pPipe = strchr( szBare, '|' );
		if ( pPipe )
			*pPipe = '\0';
		CBaseEntity::PrecacheModel( szBare );
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Load a single model config from KeyValues file
//-----------------------------------------------------------------------------
bool HL2SB_LoadModelConfigFromKV( const char *pszFilePath, const char *pszConfigName )
{
	if ( g_nHL2SB_ModelConfigCount >= HL2SB_MAX_MODELS )
		return false;

	KeyValues *pKV = new KeyValues( pszConfigName );
	if ( !pKV->LoadFromFile( filesystem, pszFilePath, "MOD" ) )
	{
		Warning( "[HL2SB] Failed to load config: %s\n", pszFilePath );
		pKV->deleteThis();
		return false;
	}

	// Try to find root section, or use KV root directly
	KeyValues *pRoot = pKV->GetFirstTrueSubKey();
	if ( !pRoot )
	{
		// No sub-key, try reading from root directly
		pRoot = pKV;
	}

	HL2SB_ModelConfig_t &config = g_HL2SB_ModelConfigs[g_nHL2SB_ModelConfigCount];
	Q_strncpy( config.szConfigFile, pszFilePath, sizeof(config.szConfigFile) );
	// Use config file name as the lookup name (not display name)
	Q_strncpy( config.szName, pszConfigName, sizeof(config.szName) );

	// Read player model (required)
	const char *pszPlayerModel = pRoot->GetString( "model", "" );
	if ( !pszPlayerModel[0] )
	{
		Warning( "[HL2SB] Config '%s' has no 'model' key, skipping\n", pszConfigName );
		pKV->deleteThis();
		return false;
	}
	Q_strncpy( config.szPlayerModel, pszPlayerModel, sizeof(config.szPlayerModel) );

	// Read hands model (optional)
	const char *pszHandsModel = pRoot->GetString( "hands", "" );
	Q_strncpy( config.szHandsModel, pszHandsModel, sizeof(config.szHandsModel) );

	pKV->deleteThis();

	g_nHL2SB_ModelConfigCount++;

	HL2SB_PrecacheModelConfig( config );

	Msg( "[HL2SB] Loaded: %s -> %s\n", config.szName, config.szPlayerModel );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Load all model configs from cfg/playermodel/
//-----------------------------------------------------------------------------
void HL2SB_LoadAllModelConfigs( void )
{
	// HL2SB: entries registered from Lua (HL2SB_AddRuntimeModelConfig) are not
	// files in cfg/playermodel/, and they have to SURVIVE this rescan.  Lua runs
	// at level init while this scan runs from the server's Precache or the
	// client's first menu open, so the order is not fixed - dropping a user's
	// playermodel here would show up as "the addon works randomly".
	static HL2SB_ModelConfig_t s_RuntimeConfigs[HL2SB_MAX_MODELS];
	int nRuntime = 0;

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; ++i )
	{
		if ( !Q_stricmp( g_HL2SB_ModelConfigs[i].szConfigFile, HL2SB_RUNTIME_CONFIG_FILE ) )
			s_RuntimeConfigs[nRuntime++] = g_HL2SB_ModelConfigs[i];
	}

	g_nHL2SB_ModelConfigCount = 0;
	g_bModelConfigsLoaded = false;

	Msg( "[HL2SB] Loading model configs...\n" );

	const char *pszPath = "cfg/playermodel";
	const char *pszPattern = "*.cfg";

	char szSearchPath[MAX_PATH];
	Q_snprintf( szSearchPath, sizeof(szSearchPath), "%s/%s", pszPath, pszPattern );

	FileFindHandle_t findHandle;
	const char *pszFilename = filesystem->FindFirst( szSearchPath, &findHandle );

	if ( !pszFilename )
	{
		Msg( "[HL2SB] No model configs found in %s/\n", pszPath );
	}
	else
	{
		while ( pszFilename && g_nHL2SB_ModelConfigCount < HL2SB_MAX_MODELS )
		{
			char szFullPath[MAX_PATH];
			Q_snprintf( szFullPath, sizeof(szFullPath), "%s/%s", pszPath, pszFilename );

			char szConfigName[HL2SB_MAX_MODEL_NAME];
			Q_strncpy( szConfigName, pszFilename, sizeof(szConfigName) );
			int len = Q_strlen( szConfigName );
			if ( len > 4 && !Q_stricmp( &szConfigName[len - 4], ".cfg" ) )
			{
				szConfigName[len - 4] = '\0';
			}

			Msg( "[HL2SB] Loading: %s\n", szFullPath );
			HL2SB_LoadModelConfigFromKV( szFullPath, szConfigName );

			pszFilename = filesystem->FindNext( findHandle );
		}

		filesystem->FindClose( findHandle );
	}

	// HL2SB: put the runtime entries back.  ⚠️ The duplicate check must NOT go
	// through HL2SB_GetModelConfigByName(): that helper lazy-loads, and we are
	// inside the load - it would recurse forever.
	int nRestored = 0;

	for ( int i = 0; i < nRuntime && g_nHL2SB_ModelConfigCount < HL2SB_MAX_MODELS; ++i )
	{
		bool bDefinedByCfg = false;
		for ( int j = 0; j < g_nHL2SB_ModelConfigCount; ++j )
		{
			if ( !Q_stricmp( g_HL2SB_ModelConfigs[j].szName, s_RuntimeConfigs[i].szName ) )
			{
				bDefinedByCfg = true;
				break;
			}
		}

		// A cfg file with the same name wins: it is the explicit, file-driven
		// declaration of that model.
		if ( bDefinedByCfg )
			continue;

		HL2SB_ModelConfig_t &config = g_HL2SB_ModelConfigs[g_nHL2SB_ModelConfigCount++];
		config = s_RuntimeConfigs[i];
		HL2SB_PrecacheModelConfig( config );
		++nRestored;
	}

	Msg( "[HL2SB] Loaded %d configs (%d registered from Lua)\n", g_nHL2SB_ModelConfigCount, nRestored );
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB: register or update a model at RUNTIME, i.e. from Lua.
//
// Garry's Mod playermodel addons do not ship a cfg/playermodel/<name>.cfg - they
// call `player_manager.AddValidModel( name, model )` (and AddValidHands) from a
// lua/autorun file.  The menu (hl2sb.GetPlayerModels), the validation inside
// hl2sb.SetPlayerModel(), the server precache and the c_hands lookup all read
// THIS table, so those registrations have to land here or the addon is invisible.
//
//   pszName        - the key, spelled exactly like a cfg entry's file name: it is
//                    what cl_playermodel / hl2sb.SetPlayerModel() take.
//   pszPlayerModel - model path; NULL/"" keeps whatever the entry already has.
//   pszHandsModel  - "" for none, NULL to keep; the cfg encoding
//                    "path|skin|bodygroups" is accepted.
//
// Updating an existing entry is the whole point: AddValidModel runs first and
// AddValidHands immediately after it, so the hands always arrive in a second call.
//-----------------------------------------------------------------------------
void HL2SB_AddRuntimeModelConfig( const char *pszName, const char *pszPlayerModel, const char *pszHandsModel )
{
	if ( !pszName || !pszName[0] )
		return;

	HL2SB_EnsureModelConfigsLoaded();

	HL2SB_ModelConfig_t *pConfig = NULL;

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; ++i )
	{
		if ( !Q_stricmp( g_HL2SB_ModelConfigs[i].szName, pszName ) )
		{
			pConfig = &g_HL2SB_ModelConfigs[i];
			break;
		}
	}

	if ( !pConfig )
	{
		if ( g_nHL2SB_ModelConfigCount >= HL2SB_MAX_MODELS )
		{
			Warning( "[HL2SB] Runtime model '%s' ignored: table full (%d)\n", pszName, HL2SB_MAX_MODELS );
			return;
		}

		pConfig = &g_HL2SB_ModelConfigs[g_nHL2SB_ModelConfigCount++];
		Q_memset( pConfig, 0, sizeof( *pConfig ) );
		Q_strncpy( pConfig->szConfigFile, HL2SB_RUNTIME_CONFIG_FILE, sizeof( pConfig->szConfigFile ) );
		Q_strncpy( pConfig->szName, pszName, sizeof( pConfig->szName ) );
	}

	if ( pszPlayerModel && pszPlayerModel[0] )
		Q_strncpy( pConfig->szPlayerModel, pszPlayerModel, sizeof( pConfig->szPlayerModel ) );

	if ( pszHandsModel )
		Q_strncpy( pConfig->szHandsModel, pszHandsModel, sizeof( pConfig->szHandsModel ) );

	HL2SB_PrecacheModelConfig( *pConfig );

	Msg( "[HL2SB] Runtime model '%s' -> %s (hands: '%s')\n",
		 pConfig->szName, pConfig->szPlayerModel,
		 pConfig->szHandsModel[0] ? pConfig->szHandsModel : "none" );
}

//-----------------------------------------------------------------------------
// Purpose: Load a specific model config by name
//-----------------------------------------------------------------------------
bool HL2SB_LoadModelConfig( const char *pszConfigName )
{
	char szPath[MAX_PATH];
	Q_snprintf( szPath, sizeof(szPath), "cfg/playermodel/%s.cfg", pszConfigName );

	// Check if already loaded
	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		if ( !Q_stricmp( g_HL2SB_ModelConfigs[i].szName, pszConfigName ) ||
			 !Q_stricmp( g_HL2SB_ModelConfigs[i].szConfigFile, szPath ) )
		{
			return true; // Already loaded
		}
	}

	return HL2SB_LoadModelConfigFromKV( szPath, pszConfigName );
}

//-----------------------------------------------------------------------------
// Purpose: Get model config by index
//-----------------------------------------------------------------------------
const HL2SB_ModelConfig_t *HL2SB_GetModelConfig( int nIndex )
{
	HL2SB_EnsureModelConfigsLoaded();

	if ( nIndex < 0 || nIndex >= g_nHL2SB_ModelConfigCount )
		return NULL;

	return &g_HL2SB_ModelConfigs[nIndex];
}

//-----------------------------------------------------------------------------
// Purpose: Get model config by name
//-----------------------------------------------------------------------------
const HL2SB_ModelConfig_t *HL2SB_GetModelConfigByName( const char *pszName )
{
	HL2SB_EnsureModelConfigsLoaded();

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		if ( !Q_stricmp( g_HL2SB_ModelConfigs[i].szName, pszName ) )
			return &g_HL2SB_ModelConfigs[i];
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Find model config by player model path
//-----------------------------------------------------------------------------
const HL2SB_ModelConfig_t *HL2SB_FindModelConfigByPath( const char *pszPlayerModelPath )
{
	HL2SB_EnsureModelConfigsLoaded();

	if ( !pszPlayerModelPath || !pszPlayerModelPath[0] )
		return NULL;

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		if ( !Q_stricmp( g_HL2SB_ModelConfigs[i].szPlayerModel, pszPlayerModelPath ) )
			return &g_HL2SB_ModelConfigs[i];
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Get hands model for a given player model
//-----------------------------------------------------------------------------
const char *HL2SB_GetHandsModelForPlayer( const char *pszPlayerModelPath )
{
	HL2SB_EnsureModelConfigsLoaded();

	const HL2SB_ModelConfig_t *pConfig = HL2SB_FindModelConfigByPath( pszPlayerModelPath );
	if ( pConfig && pConfig->szHandsModel[0] && Q_stricmp( pConfig->szHandsModel, "none" ) != 0 )
		return pConfig->szHandsModel;

	// HL2SB: GMod gives EVERY playermodel arms - a model with no hands entry of its own
	// uses the default citizen arms.  Returning NULL here is what left the first-person
	// view with no arms at all for everything the models/player scan registered (the
	// "hands: none" entries in the log, 2026-09-17), so fall back instead of answering
	// "nothing".
	return "models/weapons/c_arms_citizen.mdl";
}

//-----------------------------------------------------------------------------
// Purpose: Print the loaded model config list to the console.
//
// HL2SB: the console commands that called this (hl2sb_listmodels / hl2sb_setmodel /
// hl2sb_modelmenu) are gone - the player model selector is the interface now
// (lua/game/client/hl2sb_playermodel_gmod.lua) - so this is diagnostic only.
//-----------------------------------------------------------------------------
void HL2SB_PrintModelList( void )
{
	HL2SB_EnsureModelConfigsLoaded();

	Msg( "=== HL2SB Player Models ===\n" );
	Msg( "Pick one in the player model selector (open_playermodel_selector).\n" );
	Msg( "Config Count: %d\n", g_nHL2SB_ModelConfigCount );

	Msg( "\n  Available models:\n" );
	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		const HL2SB_ModelConfig_t &config = g_HL2SB_ModelConfigs[i];
		Msg( "  [%d] %s\n", i + 1, config.szName );
		Msg( "      Model: %s\n", config.szPlayerModel );
		if ( config.szHandsModel[0] )
		{
			Msg( "      Hands: %s\n", config.szHandsModel );
		}
		Msg( "\n" );
	}

	if ( g_nHL2SB_ModelConfigCount == 0 )
	{
		Msg( "  (none found - check cfg/playermodel/)\n" );
	}
}
