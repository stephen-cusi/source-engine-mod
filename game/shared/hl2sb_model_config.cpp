// hl2sb_model_config.cpp
// HL2SB Player Model Configuration System

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

#if !defined( CLIENT_DLL )
	// Server: precache all models referenced by this config
	if ( config.szPlayerModel[0] )
	{
		CBaseEntity::PrecacheModel( config.szPlayerModel );
	}
	if ( config.szHandsModel[0] )
	{
		CBaseEntity::PrecacheModel( config.szHandsModel );
	}
#endif

	Msg( "[HL2SB] Loaded: %s -> %s\n", config.szName, config.szPlayerModel );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Load all model configs from cfg/playermodel/
//-----------------------------------------------------------------------------
void HL2SB_LoadAllModelConfigs( void )
{
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
		return;
	}

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

	Msg( "[HL2SB] Loaded %d configs\n", g_nHL2SB_ModelConfigCount );
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
	if ( pConfig && pConfig->szHandsModel[0] )
		return pConfig->szHandsModel;

	return NULL;
}
